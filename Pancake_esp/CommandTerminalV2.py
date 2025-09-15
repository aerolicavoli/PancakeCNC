#!/usr/bin/env python3
"""Command-line terminal for Pancake CNC with telemetry log and command ACKs.

This iteration drops the graphical interface from the previous version and
returns to a simple console program while keeping the quality-of-life features:

* Short argument aliases (``sc=0.2`` instead of ``SpiralConstant_mprad=0.2``)
  and flexible ``key value`` or ``key:value`` token forms.
* Log output from the telemetry bucket printed in cyan.
* Commands are tagged with a short hash and acknowledgements show a green check
  mark when a matching hash arrives from the ESP32 firmware.
"""

from __future__ import annotations

import base64
import csv
import hashlib
import os
import shlex
import threading
import time
from typing import Dict, List, Optional

import requests
import readline  # noqa: F401 - enables input history and editing

# Reuse helpers from the original terminal
from CommandTerminal import (
    _require,
    _build_echo_packet,
    _build_cnc_payload,
    IMMEDIATE_OPCODES,
    _canonical_cmd_name,
    _parse_kv_tokens,
)


# Environment configuration -------------------------------------------------
INFLUXDB_URL = os.environ.get("INFLUXDB_URL")
INFLUXDB_TOKEN = os.environ.get("INFLUXDB_TOKEN")
INFLUXDB_ORG = os.environ.get("INFLUXDB_ORG")
INFLUXDB_CMD_BUCKET = os.environ.get("INFLUXDB_CMD_BUCKET")
INFLUXDB_TLM_BUCKET = os.environ.get("INFLUXDB_TLM_BUCKET", INFLUXDB_CMD_BUCKET)

# ANSI colors
RESET = "\033[0m"
CYAN = "\033[36m"
GREEN = "\033[32m"
RED = "\033[31m"


# Short key aliases for less typing ----------------------------------------
SHORT_KEYS: Dict[str, Dict[str, str]] = {
    "cnc_spiral": {
        "sc": "SpiralConstant_mprad",
        "sr": "SpiralRate_radps",
        "ls": "LinearSpeed_mps",
        "cx": "CenterX_m",
        "cy": "CenterY_m",
        "mr": "MaxRadius_m",
    },
    "cnc_sine": {
        "a": "Amplitude_deg",
        "f": "Frequency_hz",
    },
    "cnc_constant_speed": {
        "s0": "S0Speed_degps",
        "s1": "S1Speed_degps",
    },
    "wait": {"t": "timeout_ms"},
    "set_motor_limits": {
        "m": "motor",
        "a": "accel",
        "s": "speed",
    },
    "set_pump_constant": {"pc": "pumpConstant_degpm"},
    "cnc_jog": {
        "x": "TargetX_m",
        "y": "TargetY_m",
        "ls": "LinearSpeed_mps",
        "p": "PumpOn",
    },
    "cnc_arc": {
        "st": "StartTheta_rad",
        "et": "EndTheta_rad",
        "r": "Radius_m",
        "ls": "LinearSpeed_mps",
        "cx": "CenterX_m",
        "cy": "CenterY_m",
    },
    "pump_purge": {
        "ps": "pumpSpeed_degps",
        "d": "duration_ms",
    },
}


def _apply_short_keys(cmd: str, tokens: List[str]) -> List[str]:
    """Expand short key tokens to full ``k=v`` strings."""

    mapping = SHORT_KEYS.get(cmd, {})
    out: List[str] = []
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        if "=" in tok:
            k, v = tok.split("=", 1)
        elif ":" in tok:
            k, v = tok.split(":", 1)
        else:
            if i + 1 >= len(tokens):
                raise ValueError(f"Missing value for {tok}")
            k, v = tok, tokens[i + 1]
            i += 1
        k = mapping.get(k, k)
        out.append(f"{k}={v}")
        i += 1
    return out


def _build_command_packet(line: str) -> Optional[bytes]:
    """Build a command packet from a user-entered line."""

    parts = shlex.split(line)
    if not parts:
        return None
    cmd_raw = parts[0]
    cmd = _canonical_cmd_name(cmd_raw)

    if cmd == "e":
        msg = line[len(parts[0]) :].lstrip()
        return _build_echo_packet(msg)
    if cmd in IMMEDIATE_OPCODES:
        return bytes([IMMEDIATE_OPCODES[cmd], 0])

    arg_tokens = _apply_short_keys(cmd, parts[1:])
    arg_map = _parse_kv_tokens(arg_tokens)
    opcode, payload = _build_cnc_payload(cmd, arg_map)
    if len(payload) > 255:
        raise ValueError("payload too long")
    return bytes([opcode, len(payload)]) + payload


def _write_packet(packet: bytes, cmd_hash: str) -> None:
    """Write the packet to the InfluxDB command bucket with a hash tag."""

    url = f"{INFLUXDB_URL}/api/v2/write"
    params = {"org": INFLUXDB_ORG, "bucket": INFLUXDB_CMD_BUCKET, "precision": "ms"}
    headers = {"Authorization": f"Token {INFLUXDB_TOKEN}", "Content-Type": "text/plain"}
    b64 = base64.b64encode(packet).decode("ascii")
    ts = int(time.time() * 1000)
    line = f"cmd,hash={cmd_hash} data=\"{b64}\" {ts}"
    resp = requests.post(url, params=params, data=line, headers=headers, timeout=5)
    resp.raise_for_status()


def _run_query(flux: str) -> List[Dict[str, str]]:
    """Run a Flux query and return rows as dicts."""

    url = f"{INFLUXDB_URL}/api/v2/query"
    params = {"org": INFLUXDB_ORG}
    headers = {
        "Authorization": f"Token {INFLUXDB_TOKEN}",
        "Content-Type": "application/vnd.flux",
        "Accept": "application/csv",
    }
    resp = requests.post(url, params=params, data=flux, headers=headers, timeout=10)
    resp.raise_for_status()
    text = resp.text
    return list(csv.DictReader(line for line in text.splitlines() if not line.startswith("#")))


# Telemetry loops -----------------------------------------------------------
def _log_loop() -> None:
    seen = set()
    flux = (
        f'from(bucket:"{INFLUXDB_TLM_BUCKET}") |> range(start:-5m) '
        '|> filter(fn: (r) => r._measurement == "logs") '
        '|> sort(columns:["_time"]) |> tail(n:20)'
    )
    while True:
        try:
            rows = _run_query(flux)
            for row in rows:
                uid = row.get("_time", "") + row.get("_value", "")
                if uid in seen:
                    continue
                seen.add(uid)
                msg = row.get("_value", "")
                print(f"{CYAN}{msg}{RESET}")
        except Exception as exc:  # pragma: no cover - network issues
            print(f"{RED}Log error: {exc}{RESET}")
        time.sleep(5)


def _ack_loop(pending: Dict[str, str]) -> None:
    seen = set()
    flux = (
        f'from(bucket:"{INFLUXDB_TLM_BUCKET}") |> range(start:-5m) '
        '|> filter(fn: (r) => r._measurement == "cmd_ack") '
        '|> sort(columns:["_time"]) |> tail(n:50)'
    )
    while True:
        try:
            rows = _run_query(flux)
            for row in rows:
                h = row.get("hash") or row.get("_value")
                if not h or h in seen:
                    continue
                seen.add(h)
                cmd = pending.pop(h, None)
                if cmd:
                    print(f"{GREEN}✓ {cmd}{RESET}")
        except Exception:  # pragma: no cover - network issues
            pass
        time.sleep(2)


# --------------------------------------------------------------------------
def main() -> None:  # pragma: no cover - interactive
    _require(INFLUXDB_URL, "INFLUXDB_URL")
    _require(INFLUXDB_TOKEN, "INFLUXDB_TOKEN")
    _require(INFLUXDB_ORG, "INFLUXDB_ORG")
    _require(INFLUXDB_CMD_BUCKET, "INFLUXDB_CMD_BUCKET")
    _require(INFLUXDB_TLM_BUCKET, "INFLUXDB_TLM_BUCKET")

    pending: Dict[str, str] = {}
    threading.Thread(target=_log_loop, daemon=True).start()
    threading.Thread(target=_ack_loop, args=(pending,), daemon=True).start()

    while True:
        try:
            line = input("> ").strip()
        except EOFError:
            break
        if not line:
            continue
        try:
            packet = _build_command_packet(line)
            if packet is None:
                continue
            h = hashlib.sha1(packet).hexdigest()[:8]
            _write_packet(packet, h)
            pending[h] = line
            print(f"{h} sent")
        except Exception as exc:
            print(f"{RED}Error: {exc}{RESET}")


if __name__ == "__main__":  # pragma: no cover
    main()

