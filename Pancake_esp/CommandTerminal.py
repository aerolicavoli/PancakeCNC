#!/usr/bin/env python3
"""Interactive terminal for sending commands to an InfluxDB command bucket.

Simplified syntax (no leading / or -a):
  E Hello World
  CNC_Spiral CenterX_m=0.2 LinearSpeed_mps=0.05
  Wait timeout_ms=500
  CNC_Sine Amplitude_deg=10 Frequency_hz=0.25
  CNC_ConstantSpeed S0Speed_degps=0 S1Speed_degps=45

Run a newline-delimited program file:
  run_file TestProgram.txt

Env vars: INFLUXDB_URL, INFLUXDB_TOKEN, INFLUXDB_ORG, INFLUXDB_CMD_BUCKET

Commands are encoded as binary [opcode][length][payload] and base64-encoded before being written.
"""

from __future__ import annotations

import base64
import os
import sys
import time
from typing import Optional, Dict, Tuple, Any, List
import shlex
import struct

import requests


INFLUXDB_URL = os.environ.get("INFLUXDB_URL")
INFLUXDB_TOKEN = os.environ.get("INFLUXDB_TOKEN")
INFLUXDB_ORG = os.environ.get("INFLUXDB_ORG")
INFLUXDB_CMD_BUCKET = os.environ.get("INFLUXDB_CMD_BUCKET")


OPCODES: Dict[str, Tuple[str, int]] = {  # echo mapping
    "E": ("Echo", 0x69),
}

# CNC opcode mapping (matches current firmware SerialParser.h values)
CNC_OPCODES: Dict[str, int] = {
    "CNC_Spiral": 0x11,
    "CNC_Jog": 0x12,           # not used yet
    "Wait": 0x13,
    "CNC_Sine": 0x14,
    "CNC_ConstantSpeed": 0x15,
}

# Defaults for command arguments
DEFAULTS = {
    "CNC_Spiral": {
        "SpiralConstant_mprad": 0.2,   # user-requested default
        "SpiralRate_radps": 1.0,
        "LinearSpeed_mps": 0.05,
        "CenterX_m": 0.0,
        "CenterY_m": 0.0,
        "MaxRadius_m": 0.0,
    },
    "CNC_Sine": {
        "Amplitude_deg": 0.0,
        "Frequency_hz": 0.0,
    },
    "CNC_ConstantSpeed": {
        "S0Speed_degps": 0.0,
        "S1Speed_degps": 0.0,
    },
    "Wait": {
        "timeout_ms": 0,
    },
}


def print_help() -> None:
    print("Commands:")
    print("  E <message>")
    print("  CNC_Spiral k=v [k=v] [...] or comma-separated")
    print("  CNC_Sine k=v [k=v] [...] or comma-separated")
    print("  CNC_ConstantSpeed k=v [k=v] [...] or comma-separated")
    print("  Wait timeout_ms=<int>")
    print("  run_file <path>")


def _require(env: Optional[str], name: str) -> str:
    if not env:
        print(f"Environment variable {name} must be set", file=sys.stderr)
        sys.exit(1)
    return env


def _build_echo_packet(message: str) -> bytes:
    opcode = OPCODES["E"][1]
    data = message.encode("utf-8")
    if len(data) > 255:
        raise ValueError("message too long for single-byte length field")
    return bytes([opcode, len(data)]) + data


def _build_cnc_payload(cmd: str, args: Dict[str, Any]) -> Tuple[int, bytes]:
    if cmd not in CNC_OPCODES:
        raise ValueError(f"Unknown CNC command: {cmd}")
    op = CNC_OPCODES[cmd]

    merged = {**DEFAULTS.get(cmd, {}), **args}

    if cmd == "CNC_Spiral":
        payload = struct.pack(
            "<ffffff",
            float(merged.get("SpiralConstant_mprad")),
            float(merged.get("SpiralRate_radps")),
            float(merged.get("LinearSpeed_mps")),
            float(merged.get("CenterX_m")),
            float(merged.get("CenterY_m")),
            float(merged.get("MaxRadius_m")),
        )
        return op, payload
    elif cmd == "CNC_Sine":
        payload = struct.pack(
            "<ff",
            float(merged.get("Amplitude_deg")),
            float(merged.get("Frequency_hz")),
        )
        return op, payload
    elif cmd == "CNC_ConstantSpeed":
        payload = struct.pack(
            "<ff",
            float(merged.get("S0Speed_degps")),
            float(merged.get("S1Speed_degps")),
        )
        return op, payload
    elif cmd == "Wait":
        payload = struct.pack("<i", int(merged.get("timeout_ms")))
        return op, payload
    else:
        raise ValueError(f"No payload builder for {cmd}")


def _parse_kv_tokens(tokens: List[str]) -> Dict[str, Any]:
    flat: List[str] = []
    for tok in tokens:
        for piece in tok.split(','):
            s = piece.strip()
            if s:
                flat.append(s)
    out: Dict[str, Any] = {}
    for item in flat:
        if '=' not in item:
            raise ValueError(f"Bad arg: {item}")
        k, v = item.split('=', 1)
        k = k.strip()
        v = v.strip()
        try:
            if v.lower().startswith(('0x', '-0x')):
                out[k] = int(v, 16)
            else:
                out[k] = int(v)
        except ValueError:
            try:
                out[k] = float(v)
            except ValueError:
                out[k] = v
    return out

def _build_command_packet(line: str) -> Optional[bytes]:
    parts = shlex.split(line)
    if not parts:
        return None
    cmd = parts[0]
    if cmd == "run_file":
        return None
    if cmd == "E":
        msg = line[len(cmd):].lstrip()
        return _build_echo_packet(msg)

    arg_map = _parse_kv_tokens(parts[1:])
    # Synonyms: Center_X_m -> CenterX_m, Center_Y_m -> CenterY_m
    if "Center_X_m" in arg_map and "CenterX_m" not in arg_map:
        arg_map["CenterX_m"] = arg_map.pop("Center_X_m")
    if "Center_Y_m" in arg_map and "CenterY_m" not in arg_map:
        arg_map["CenterY_m"] = arg_map.pop("Center_Y_m")

    opcode, payload = _build_cnc_payload(cmd, arg_map)
    if len(payload) > 255:
        raise ValueError("payload too long")
    return bytes([opcode, len(payload)]) + payload


def _write_packet(packet: bytes) -> None:
    url = f"{INFLUXDB_URL}/api/v2/write"
    params = {
        "org": INFLUXDB_ORG,
        "bucket": INFLUXDB_CMD_BUCKET,
        "precision": "ms",
    }
    headers = {
        "Authorization": f"Token {INFLUXDB_TOKEN}",
        "Content-Type": "text/plain",
    }
    b64 = base64.b64encode(packet).decode("ascii")
    timestamp = int(time.time() * 1000)
    line = f"cmd data=\"{b64}\" {timestamp}"
    resp = requests.post(url, params=params, data=line, headers=headers, timeout=5)
    resp.raise_for_status()


def _send_command(line: str) -> bool:
    parts = shlex.split(line)
    if not parts:
        return False
    if parts[0] == "run_file":
        if len(parts) < 2:
            raise ValueError("usage: run_file <path> [delay_ms]")
        path = parts[1]
        delay_ms = 0
        if len(parts) >= 3:
            try:
                delay_ms = int(parts[2])
            except ValueError:
                raise ValueError("delay_ms must be integer milliseconds")
        if delay_ms <= 0:
            delay_ms = int(os.environ.get('CT_RUNFILE_DELAY_MS', '800'))
        with open(path, 'r', encoding='utf-8') as f:
            for raw in f:
                s = raw.strip()
                if not s or s.startswith('#'):
                    continue
                pkt = _build_command_packet(s)

                if pkt is not None:
                    _write_packet(pkt)
                    print(pkt)
                    time.sleep(delay_ms / 1000.0)
                
        return True

    pkt = _build_command_packet(line)
    if pkt is None:
        return False
    _write_packet(pkt)
    return True


def main() -> None:
    _require(INFLUXDB_URL, "INFLUXDB_URL")
    _require(INFLUXDB_TOKEN, "INFLUXDB_TOKEN")
    _require(INFLUXDB_ORG, "INFLUXDB_ORG")
    _require(INFLUXDB_CMD_BUCKET, "INFLUXDB_CMD_BUCKET")

    try:
        while True:
            line = input("> ")
            if not line:
                continue
            if line.lower() in {"quit", "exit"}:
                break
            if line in {"help", "?", "\\?"}:
                print_help()
                continue
            try:
                handled = _send_command(line)
                if not handled:
                    print("Unknown command", file=sys.stderr)
            except Exception as exc:
                print(f"Error: {exc}", file=sys.stderr)
    except (EOFError, KeyboardInterrupt):
        pass


if __name__ == "__main__":  # pragma: no cover
    main()
