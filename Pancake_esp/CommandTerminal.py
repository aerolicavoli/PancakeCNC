#!/usr/bin/env python3
"""Interactive terminal for sending commands to an InfluxDB command bucket.

Usage:
    $ python command_terminal.py
    > \E Hello World

Type ``help`` or ``?`` to list available opcodes. The first character after the
backslash selects the opcode. For example, ``\E`` uses opcode ``0x69`` which
causes the firmware to echo the following message. Any other opcode uses the
ASCII value of the character.

Connection parameters are taken from the following environment variables:
    INFLUXDB_URL        Base URL of the InfluxDB instance (e.g. http://localhost:8086)
    INFLUXDB_TOKEN      API token with write permission to the command bucket
    INFLUXDB_ORG        InfluxDB organisation
    INFLUXDB_CMD_BUCKET Bucket to which commands should be written

Commands are encoded as binary ``opcode``/``length``/``message`` packets and
base64-encoded before being written so they can be stored safely in InfluxDB.
"""

from __future__ import annotations

import base64
import os
import sys
import time
from typing import Optional, Dict, Tuple

import requests


INFLUXDB_URL = os.environ.get("INFLUXDB_URL")
INFLUXDB_TOKEN = os.environ.get("INFLUXDB_TOKEN")
INFLUXDB_ORG = os.environ.get("INFLUXDB_ORG")
INFLUXDB_CMD_BUCKET = os.environ.get("INFLUXDB_CMD_BUCKET")


OPCODES: Dict[str, Tuple[str, int]] = {
    "E": ("Echo", 0x69),
}


def print_help() -> None:
    print("Available opcodes:")
    for key, (name, code) in OPCODES.items():
        print(f"  \\{key} <msg> - {name} (0x{code:02X})")
    print("  \\X <msg> - Use ASCII value of X as opcode")


def _require(env: Optional[str], name: str) -> str:
    if not env:
        print(f"Environment variable {name} must be set", file=sys.stderr)
        sys.exit(1)
    return env


def _build_packet(opcode_char: str, message: str) -> bytes:
    opcode = OPCODES.get(opcode_char, ("", ord(opcode_char)))[1]
    data = message.encode("utf-8")
    if len(data) > 255:
        raise ValueError("message too long for single-byte length field")
    return bytes([opcode, len(data)]) + data


def _write_packet(packet: bytes) -> None:
    url = f"{INFLUXDB_URL}/api/v2/write"  # type: ignore[operator]
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
            if not line.startswith("\\") or len(line) < 2:
                print("Commands must start with \\X text", file=sys.stderr)
                continue
            opcode_char = line[1]
            # skip optional space after opcode
            msg = line[3:] if len(line) > 2 and line[2] == " " else line[2:]
            try:
                pkt = _build_packet(opcode_char, msg)
                _write_packet(pkt)
            except Exception as exc:  # pragma: no cover - interactive
                print(f"Error: {exc}", file=sys.stderr)
    except (EOFError, KeyboardInterrupt):
        pass


if __name__ == "__main__":  # pragma: no cover
    main()
