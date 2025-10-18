#!/usr/bin/env python3
"""Interactive terminal for sending commands to an InfluxDB command bucket.

Simplified syntax (no leading / or -a):
  e Hello World
  cnc_spiral CenterX_m=0.2 LinearSpeed_mps=0.05
  wait timeout_ms=500
  cnc_sine Amplitude_deg=10 Frequency_hz=0.25
  cnc_constant_speed S0Speed_degps=0 S1Speed_degps=45

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
    "e": ("Echo", 0x69),
}

# CNC opcode mapping (matches current firmware CNCOpCodes.h values)
CNC_OPCODES: Dict[str, int] = {
    "cnc_spiral": 0x11,
    "cnc_jog": 0x12,
    "wait": 0x13,
    "cnc_sine": 0x14,
    "cnc_constant_speed": 0x15,
    "set_motor_limits": 0x16,
    "set_pump_constant": 0x17,
    "cnc_arc": 0x18,
    "pump_purge": 0x19,
    "set_accel_scale": 0x1A,
}

# Immediate control opcodes
IMMEDIATE_OPCODES: Dict[str, int] = {
    "pause": 0x01,
    "resume": 0x02,
    "stop": 0x03,
}

# Defaults for command arguments
DEFAULTS = {
    "cnc_spiral": {
        "SpiralConstant_mprad": 0.2,   # user-requested default
        "SpiralRate_radps": 1.0,
        "LinearSpeed_mps": 0.05,
        "CenterX_m": 0.0,
        "CenterY_m": 0.0,
        "MaxRadius_m": 0.0,
    },
    "cnc_sine": {
        "Amplitude_deg": 0.0,
        "Frequency_hz": 0.0,
    },
    "cnc_constant_speed": {
        "S0Speed_degps": 0.0,
        "S1Speed_degps": 0.0,
    },
    "wait": {
        "timeout_ms": 0,
    },
}


def print_help() -> None:
    print(f"{CYAN}Commands:{RESET}")
    print("  e <message>")
    print("  cnc_spiral k=v [k=v] [...] or comma-separated")
    print("  cnc_sine k=v [k=v] [...] or comma-separated")
    print("  cnc_constant_speed k=v [k=v] [...] or comma-separated")
    print("  cnc_jog TargetX_m=<m> TargetY_m=<m> LinearSpeed_mps=<m/s> PumpOn=<0|1>")
    print("  cnc_arc StartTheta_rad=<rad> EndTheta_rad=<rad> Radius_m=<m> LinearSpeed_mps=<m/s> CenterX_m=<m> CenterY_m=<m>")
    print("  pump_purge pumpSpeed_degps=<deg/s> duration_ms=<ms>")
    print("  wait timeout_ms=<int>")
    print("  set_motor_limits motor=<S0|S1|Pump|All> accel=<degps2> speed=<degps>")
    print("  set_pump_constant pumpConstant_degpm=<val>")
    print("  set_accel_scale accelScale=<ratio>")
    print("  pause | resume | stop")
    print("  ask_to_continue [message]")
    print("  terminal_wait duration_ms=<int>")
    print("  run_file <path> [delay_ms]")
    print("")
    print(f"{DIM}Tip: '<Cmd> help' shows command-specific options.{RESET}")


# Command-specific help text
COMMAND_HELP: Dict[str, str] = {
    "e": "e <message> — Echo a text message.",
    "cnc_spiral": (
        "cnc_spiral keys:\n"
        "  SpiralConstant_mprad: float (default 0.2)\n"
        "  SpiralRate_radps:    float (default 1.0)\n"
        "  LinearSpeed_mps:     float (default 0.05)\n"
        "  CenterX_m:           float\n"
        "  CenterY_m:           float\n"
        "  MaxRadius_m:         float"
    ),
    "cnc_sine": (
        "cnc_sine keys:\n"
        "  Amplitude_deg: float\n"
        "  Frequency_hz:  float"
    ),
    "cnc_constant_speed": (
        "cnc_constant_speed keys:\n"
        "  S0Speed_degps: float\n"
        "  S1Speed_degps: float"
    ),
    "cnc_jog": (
        "cnc_jog keys:\n"
        "  TargetX_m:          float\n"
        "  TargetY_m:          float\n"
        "  LinearSpeed_mps:    float\n"
        "  PumpOn:             0|1"
    ),
    "cnc_arc": (
        "cnc_arc keys:\n"
        "  StartTheta_rad:     float\n"
        "  EndTheta_rad:       float\n"
        "  Radius_m:           float\n"
        "  LinearSpeed_mps:    float\n"
        "Note: Arc center is derived from current position as the start point on the arc."
    ),
    "wait": (
        "wait keys:\n"
        "  timeout_ms: int"
    ),
    "set_motor_limits": (
        "set_motor_limits keys:\n"
        "  motor: S0 | S1 | Pump | All\n"
        "  accel: float\n"
        "  speed: float"
    ),
    "set_pump_constant": (
        "set_pump_constant keys:\n"
        "  pumpConstant_degpm: float"
    ),
    "set_accel_scale": (
        "set_accel_scale keys:\n"
        "  accelScale: float — fraction of accel limit to apply"
    ),
    "pump_purge": (
        "pump_purge keys:\n"
        "  pumpSpeed_degps: float (deg/s)\n"
        "  duration_ms:     int (ms)"
    ),
    "pause": "pause — immediately zero speeds and hold state.",
    "resume": "resume — clear pause and continue.",
    "stop": "stop — idle and clear queued CNC commands.",
    "ask_to_continue": (
        "ask_to_continue [message]\n"
        "  Prompts the user to continue (y/n). Not sent to device."
    ),
    "terminal_wait": (
        "terminal_wait keys:\n"
        "  duration_ms: int — local delay before next command."
    ),
}

# Legacy command names mapped to canonical snake_case names
CMD_ALIASES: Dict[str, str] = {
    "E": "e",
    "Wait": "wait",
    "Pause": "pause",
    "Resume": "resume",
    "Stop": "stop",
    "CNC_Spiral": "cnc_spiral",
    "CNC_Sine": "cnc_sine",
    "CNC_ConstantSpeed": "cnc_constant_speed",
    "CNC_Jog": "cnc_jog",
    "CNC_Arc": "cnc_arc",
    "SetMotorLimits": "set_motor_limits",
    "SetPumpConstant": "set_pump_constant",
    "SetAccelScale": "set_accel_scale",
    "PumpPurge": "pump_purge",
}

def _canonical_cmd_name(cmd: str) -> str:
    return CMD_ALIASES.get(cmd, cmd)


def print_command_help(cmd: str) -> bool:
    txt = COMMAND_HELP.get(_canonical_cmd_name(cmd))
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
    opcode = OPCODES["e"][1]
    data = message.encode("utf-8")
    if len(data) > 255:
        raise ValueError("message too long for single-byte length field")
    return bytes([opcode, len(data)]) + data


def _build_cnc_payload(cmd: str, args: Dict[str, Any]) -> Tuple[int, bytes]:
    if cmd not in CNC_OPCODES:
        raise ValueError(f"Unknown CNC command: {cmd}")
    op = CNC_OPCODES[cmd]

    merged = {**DEFAULTS.get(cmd, {}), **args}

    if cmd == "cnc_spiral":
        allowed = {"SpiralConstant_mprad", "SpiralRate_radps", "LinearSpeed_mps", "CenterX_m", "CenterY_m", "MaxRadius_m"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for cnc_spiral: {', '.join(sorted(unknown))}")
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
    elif cmd == "cnc_sine":
        allowed = {"Amplitude_deg", "Frequency_hz"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for cnc_sine: {', '.join(sorted(unknown))}")
        payload = struct.pack(
            "<ff",
            float(merged.get("Amplitude_deg")),
            float(merged.get("Frequency_hz")),
        )
        return op, payload
    elif cmd == "cnc_constant_speed":
        allowed = {"S0Speed_degps", "S1Speed_degps"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for cnc_constant_speed: {', '.join(sorted(unknown))}")
        payload = struct.pack(
            "<ff",
            float(merged.get("S0Speed_degps")),
            float(merged.get("S1Speed_degps")),
        )
        return op, payload
    elif cmd == "wait":
        allowed = {"timeout_ms"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for wait: {', '.join(sorted(unknown))}")
        payload = struct.pack("<i", int(merged.get("timeout_ms")))
        return op, payload
    elif cmd == "set_motor_limits":
        # Expect: motor=S0|S1|Pump|All accel=... speed=...
        motor_map = {"S0": 0, "S1": 1, "Pump": 2, "All": 255}
        mkey = str(args.get("motor", "All"))
        motor_id = motor_map.get(mkey, 255)
        accel = float(args.get("accel", 0.0))
        speed = float(args.get("speed", 0.0))
        allowed = {"motor", "accel", "speed"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for set_motor_limits: {', '.join(sorted(unknown))}")
        payload = struct.pack("<Bff", motor_id, accel, speed)
        return op, payload
    elif cmd == "set_pump_constant":
        allowed = {"pumpConstant_degpm"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for set_pump_constant: {', '.join(sorted(unknown))}")
        val = float(args.get("pumpConstant_degpm", 0.0))
        payload = struct.pack("<f", val)
        return op, payload
    elif cmd == "set_accel_scale":
        allowed = {"accelScale"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for set_accel_scale: {', '.join(sorted(unknown))}")
        val = float(args.get("accelScale", 0.0))
        payload = struct.pack("<f", val)
        return op, payload
    elif cmd == "cnc_jog":
        allowed = {"TargetX_m", "TargetY_m", "LinearSpeed_mps", "PumpOn"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for cnc_jog: {', '.join(sorted(unknown))}")
        tx = float(args.get("TargetX_m"))
        ty = float(args.get("TargetY_m"))
        sp = float(args.get("LinearSpeed_mps", 0.05))
        pump = int(args.get("PumpOn", 0))
        payload = struct.pack("<fffI", tx, ty, sp, pump)
        return op, payload
    elif cmd == "cnc_arc":
        allowed = {"StartTheta_rad", "EndTheta_rad", "Radius_m", "LinearSpeed_mps", "CenterX_m", "CenterY_m"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for cnc_arc: {', '.join(sorted(unknown))}")
        start = float(args.get("StartTheta_rad"))
        end = float(args.get("EndTheta_rad"))
        r = float(args.get("Radius_m"))
        sp = float(args.get("LinearSpeed_mps"))
        cx = float(args.get("CenterX_m"))
        cy = float(args.get("CenterY_m"))
        payload = struct.pack("<ffffff", start, end, r, sp, cx, cy)
        return op, payload
    elif cmd == "pump_purge":
        allowed = {"pumpSpeed_degps", "duration_ms"}
        unknown = set(args.keys()) - allowed
        if unknown:
            raise ValueError(f"Unknown keys for pump_purge: {', '.join(sorted(unknown))}")
        spd = float(args.get("pumpSpeed_degps"))
        dur = int(args.get("duration_ms"))
        payload = struct.pack("<fi", spd, dur)
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
    cmd_raw = parts[0]
    cmd = cmd_raw
    if len(parts) >= 2 and parts[1].lower() in {"help", "-h", "?"}:
        # handled by caller
        return None
    if cmd == "run_file":
        return None
    # Normalize command names to snake_case
    cmd = _canonical_cmd_name(cmd_raw)
    if cmd == "e":
        msg = line[len(parts[0]):].lstrip()
        return _build_echo_packet(msg)

    if cmd in IMMEDIATE_OPCODES:
        return bytes([IMMEDIATE_OPCODES[cmd], 0])

    arg_map = _parse_kv_tokens(parts[1:])

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
                # Local-only handling for file-driven commands
                parts2 = shlex.split(s)
                cmd2 = parts2[0] if parts2 else ''
                if cmd2 == 'ask_to_continue':
                    msg = s[len(cmd2):].strip()
                    prompt = msg if msg else 'Continue? (y/n): '
                    # Show the script line
                    print(f"{DIM}↳ {s}{RESET}")
                    while True:
                        resp = input(prompt).strip().lower()
                        if resp in {'y', 'yes'}:
                            break
                        if resp in {'n', 'no'}:
                            print("Aborted by user.")
                            return True
                        print("Please respond with 'y' or 'n'.")
                    continue
                if cmd2 == 'terminal_wait':
                    kv = _parse_kv_tokens(parts2[1:])
                    if 'duration_ms' not in kv:
                        raise ValueError("terminal_wait requires duration_ms=<int>")
                    dur = int(kv['duration_ms'])
                    print(f"{DIM}↳ {s}{RESET}")
                    time.sleep(max(0, dur) / 1000.0)
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

    # Local-only commands (interactive)
    if parts[0] == 'ask_to_continue':
        msg = line[len(parts[0]):].strip()
        prompt = msg if msg else 'Continue? (y/n): '
        while True:
            resp = input(prompt).strip().lower()
            if resp in {'y', 'yes'}:
                break
            if resp in {'n', 'no'}:
                print("Aborted by user.")
                return True
            print("Please respond with 'y' or 'n'.")
        return True
    if parts[0] == 'terminal_wait':
        kv = _parse_kv_tokens(parts[1:])
        if 'duration_ms' not in kv:
            raise ValueError("terminal_wait requires duration_ms=<int>")
        dur = int(kv['duration_ms'])
        time.sleep(max(0, dur) / 1000.0)
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
            "e",
            "cnc_spiral",
            "cnc_sine",
            "cnc_constant_speed",
            "wait",
            "set_motor_limits",
            "set_pump_constant",
            "set_accel_scale",
            "cnc_jog",
            "cnc_arc",
            "pump_purge",
            "ask_to_continue",
            "terminal_wait",
            "pause",
            "resume",
            "stop",
            "run_file",
            "help",
            "?",
            "quit",
            "exit",
        ]
        COMMAND_KEYS: Dict[str, List[str]] = {
            "cnc_spiral": [
                "SpiralConstant_mprad",
                "SpiralRate_radps",
                "LinearSpeed_mps",
                "CenterX_m",
                "CenterY_m",
                "MaxRadius_m",
            ],
            "cnc_sine": ["Amplitude_deg", "Frequency_hz"],
            "cnc_constant_speed": ["S0Speed_degps", "S1Speed_degps"],
            "wait": ["timeout_ms"],
            "set_motor_limits": ["motor", "accel", "speed"],
            "set_pump_constant": ["pumpConstant_degpm"],
            "set_accel_scale": ["accelScale"],
            "cnc_jog": ["TargetX_m", "TargetY_m", "LinearSpeed_mps", "PumpOn"],
            "cnc_arc": ["StartTheta_rad", "EndTheta_rad", "Radius_m", "LinearSpeed_mps", "CenterX_m", "CenterY_m"],
            "pump_purge": ["pumpSpeed_degps", "duration_ms"],
            "terminal_wait": ["duration_ms"],
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
                    keys = COMMAND_KEYS.get(_canonical_cmd_name(cmd), [])
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
