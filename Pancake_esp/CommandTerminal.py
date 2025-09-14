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
import re
import glob

# ANSI colors
RESET = "\033[0m"
DIM = "\033[2m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
CYAN = "\033[36m"


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
    "SetMotorLimits": 0x16,
    "SetPumpConstant": 0x17,
}

# Immediate control opcodes
IMMEDIATE_OPCODES: Dict[str, int] = {
    "Pause": 0x01,
    "Resume": 0x02,
    "Stop": 0x03,
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
    print(f"{CYAN}Commands:{RESET}")
    print("  E <message>")
    print("  CNC_Spiral k=v [k=v] [...] or comma-separated")
    print("  CNC_Sine k=v [k=v] [...] or comma-separated")
    print("  CNC_ConstantSpeed k=v [k=v] [...] or comma-separated")
    print("  Wait timeout_ms=<int>")
    print("  SetMotorLimits motor=<S0|S1|Pump|All> accel=<degps2> speed=<degps>")
    print("  SetPumpConstant pumpConstant_degpm=<val>")
    print("  Pause | Resume | Stop")
    print("  run_file <path> [delay_ms]")
    print("")
    print(f"{DIM}Tip: '<Cmd> help' shows command-specific options.{RESET}")


# Command-specific help text
COMMAND_HELP: Dict[str, str] = {
    "E": "E <message> — Echo a text message.",
    "CNC_Spiral": (
        "CNC_Spiral keys:\n"
        "  SpiralConstant_mprad: float (default 0.2)\n"
        "  SpiralRate_radps:    float (default 1.0)\n"
        "  LinearSpeed_mps:     float (default 0.05)\n"
        "  CenterX_m:           float\n"
        "  CenterY_m:           float\n"
        "  MaxRadius_m:         float\n"
        "Synonyms: Center_X_m -> CenterX_m, Center_Y_m -> CenterY_m"
    ),
    "CNC_Sine": (
        "CNC_Sine keys:\n"
        "  Amplitude_deg: float\n"
        "  Frequency_hz:  float"
    ),
    "CNC_ConstantSpeed": (
        "CNC_ConstantSpeed keys:\n"
        "  S0Speed_degps: float\n"
        "  S1Speed_degps: float"
    ),
    "Wait": (
        "Wait keys:\n"
        "  timeout_ms: int"
    ),
    "SetMotorLimits": (
        "SetMotorLimits keys:\n"
        "  motor: S0 | S1 | Pump | All\n"
        "  accel (Accel_degps2): float\n"
        "  speed (Speed_degps):  float"
    ),
    "SetPumpConstant": (
        "SetPumpConstant keys:\n"
        "  pumpConstant_degpm (k): float"
    ),
    "Pause": "Pause — immediately zero speeds and hold state.",
    "Resume": "Resume — clear pause and continue.",
    "Stop": "Stop — idle and clear queued CNC commands.",
}


def print_command_help(cmd: str) -> bool:
    txt = COMMAND_HELP.get(cmd)
    if txt:
        print(txt)
        return True
    return False


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
        allowed = {"SpiralConstant_mprad", "SpiralRate_radps", "LinearSpeed_mps", "CenterX_m", "CenterY_m", "MaxRadius_m"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for CNC_Spiral: {', '.join(sorted(unknown))}")
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
        allowed = {"Amplitude_deg", "Frequency_hz"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for CNC_Sine: {', '.join(sorted(unknown))}")
        payload = struct.pack(
            "<ff",
            float(merged.get("Amplitude_deg")),
            float(merged.get("Frequency_hz")),
        )
        return op, payload
    elif cmd == "CNC_ConstantSpeed":
        allowed = {"S0Speed_degps", "S1Speed_degps"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for CNC_ConstantSpeed: {', '.join(sorted(unknown))}")
        payload = struct.pack(
            "<ff",
            float(merged.get("S0Speed_degps")),
            float(merged.get("S1Speed_degps")),
        )
        return op, payload
    elif cmd == "Wait":
        allowed = {"timeout_ms"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for Wait: {', '.join(sorted(unknown))}")
        payload = struct.pack("<i", int(merged.get("timeout_ms")))
        return op, payload
    elif cmd == "SetMotorLimits":
        # Expect: motor=S0|S1|Pump|All accel=... speed=...
        motor_map = {"S0": 0, "S1": 1, "Pump": 2, "All": 255}
        mkey = str(args.get("motor", "All"))
        motor_id = motor_map.get(mkey, 255)
        accel = float(args.get("accel", args.get("Accel_degps2", 0.0)))
        speed = float(args.get("speed", args.get("Speed_degps", 0.0)))
        allowed = {"motor", "accel", "speed", "Accel_degps2", "Speed_degps"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for SetMotorLimits: {', '.join(sorted(unknown))}")
        payload = struct.pack("<Bff", motor_id, accel, speed)
        return op, payload
    elif cmd == "SetPumpConstant":
        allowed = {"pumpConstant_degpm", "k"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for SetPumpConstant: {', '.join(sorted(unknown))}")
        val = float(args.get("pumpConstant_degpm", args.get("k", 0.0)))
        payload = struct.pack("<f", val)
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
    if len(parts) >= 2 and parts[1].lower() in {"help", "-h", "?"}:
        # handled by caller
        return None
    if cmd == "run_file":
        return None
    if cmd == "E":
        msg = line[len(cmd):].lstrip()
        return _build_echo_packet(msg)

    if cmd in IMMEDIATE_OPCODES:
        return bytes([IMMEDIATE_OPCODES[cmd], 0])

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
                    # Show file-driven lines in a muted style
                    print(f"{DIM}↳ {s}{RESET}")
                    _write_packet(pkt)
                    time.sleep(delay_ms / 1000.0)
                
        return True

    # Command-specific help
    if len(parts) >= 2 and parts[1].lower() in {"help", "-h", "?"}:
        if not print_command_help(parts[0]):
            print_help()
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

    # Optional: readline for history + tab completion
    completer_installed = False
    try:
        import readline  # type: ignore

        COMMANDS = [
            "E",
            "CNC_Spiral",
            "CNC_Sine",
            "CNC_ConstantSpeed",
            "Wait",
            "SetMotorLimits",
            "SetPumpConstant",
            "Pause",
            "Resume",
            "Stop",
            "run_file",
            "help",
            "?",
            "quit",
            "exit",
        ]
        COMMAND_KEYS: Dict[str, List[str]] = {
            "CNC_Spiral": [
                "SpiralConstant_mprad",
                "SpiralRate_radps",
                "LinearSpeed_mps",
                "CenterX_m",
                "CenterY_m",
                "MaxRadius_m",
                # synonyms
                "Center_X_m",
                "Center_Y_m",
            ],
            "CNC_Sine": ["Amplitude_deg", "Frequency_hz"],
            "CNC_ConstantSpeed": ["S0Speed_degps", "S1Speed_degps"],
            "Wait": ["timeout_ms"],
            "SetMotorLimits": ["motor", "accel", "speed", "Accel_degps2", "Speed_degps"],
            "SetPumpConstant": ["pumpConstant_degpm", "k"],
        }

        def _complete(text, state):
            buf = readline.get_line_buffer()
            beg = readline.get_begidx()
            # Determine which token we're in
            left = buf[:beg]
            tokens = left.split()
            candidates: List[str] = []
            if not tokens:
                candidates = [c for c in COMMANDS if c.startswith(text)]
            else:
                cmd = tokens[0]
                if cmd == 'run_file':
                    # Complete path for *.txt files (respect simple dir prefixes)
                    dirpart, base = os.path.split(text)
                    search_dir = dirpart if dirpart else '.'
                    try:
                        files = glob.glob(os.path.join(search_dir, '*.txt'))
                    except Exception:
                        files = []
                    items: List[str] = []
                    for f in files:
                        name = os.path.basename(f)
                        disp = os.path.join(dirpart, name) if dirpart else name
                        items.append(disp)
                    candidates = [s for s in sorted(items) if s.startswith(text)]
                else:
                    # After a command, offer keys= completions
                    keys = COMMAND_KEYS.get(cmd, [])
                    if '=' in text:
                        # Complete values for motor=
                        if text.startswith('motor='):
                            opts = ['motor=S0', 'motor=S1', 'motor=Pump', 'motor=All']
                            candidates = [o for o in opts if o.startswith(text)]
                        else:
                            candidates = []
                    else:
                        candidates = [k + '=' for k in keys if k.startswith(text)]
            try:
                return candidates[state]
            except IndexError:
                return None

        readline.set_completer(_complete)
        # macOS often uses libedit; choose proper binding
        try:
            if hasattr(readline, '__doc__') and readline.__doc__ and 'libedit' in readline.__doc__:
                readline.parse_and_bind('bind ^I rl_complete')
            else:
                readline.parse_and_bind('tab: complete')
        except Exception:
            # Fallback
            readline.parse_and_bind('tab: complete')
        # History
        hist = os.path.expanduser('~/.pancake_cmd_history')
        try:
            readline.read_history_file(hist)
        except Exception:
            pass

        def _save_history():
            try:
                readline.write_history_file(hist)
            except Exception:
                pass

        completer_installed = True
    except Exception:
        completer_installed = False

    # Wrap non-printing ANSI codes so readline can compute correct lengths
    def _rl_safe_prompt(s: str) -> str:
        return re.sub(r'(\x1b\[[0-9;]*m)', r'\001\1\002', s)

    prompt = f"{CYAN}> {RESET}"
    if completer_installed:
        prompt = _rl_safe_prompt(prompt)

    try:
        while True:
            line = input(prompt)
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
                    print("Unknown or malformed command", file=sys.stderr)
                    print_help()
            except Exception as exc:
                print(f"Error: {exc}", file=sys.stderr)
                parts = shlex.split(line)
                if parts and not print_command_help(parts[0]):
                    print_help()
    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        if completer_installed:
            try:
                import readline  # type: ignore
                readline.set_completer(None)
                # Save history
                hist = os.path.expanduser('~/.pancake_cmd_history')
                try:
                    readline.write_history_file(hist)
                except Exception:
                    pass
            except Exception:
                pass


if __name__ == "__main__":  # pragma: no cover
    main()
