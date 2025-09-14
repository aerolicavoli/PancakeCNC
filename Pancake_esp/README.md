## Pancake_esp Command Protocol and CLI

This firmware consumes commands from an InfluxDB “command” bucket. Commands are binary packets encoded as:

- Byte 0: opcode
- Byte 1: payload length (n)
- Bytes 2..(n+1): payload bytes

### Opcodes (firmware)

- 0x69: Echo (`e <message>`)
- 0x01: `pause` (immediate)
- 0x02: `resume` (immediate)
- 0x03: `stop` (immediate; clears CNC queue)
- 0x11: `cnc_spiral`
- 0x12: `cnc_jog`
- 0x13: `wait`
- 0x14: `cnc_sine`
- 0x15: `cnc_constant_speed`
 - 0x16: `set_motor_limits` (motor limits for S0/S1/Pump)
 - 0x17: `set_pump_constant` (pumpConstant_degpm)
 - 0x18: `cnc_arc`
 - 0x19: `pump_purge`

Payloads are little-endian C structs matching the guidance configs (see headers under `main/`).

### Python Command Terminal

The included `CommandTerminal.py` sends commands using a simplified syntax:

- `e Hello World`
- `cnc_spiral CenterX_m=0.2 LinearSpeed_mps=0.05` (defaults apply; e.g., `SpiralConstant_mprad=0.2`)
- `wait timeout_ms=500`
- `cnc_sine Amplitude_deg=10 Frequency_hz=0.25`
- `cnc_constant_speed S0Speed_degps=0 S1Speed_degps=45`
- `pause` (immediate)
- `resume` (clear stop and continue)
- `stop` (immediate; clears queued CNC commands)
- `run_file TestProgram.txt` (newline-delimited commands)
 - `set_motor_limits motor=<S0|S1|Pump|All> accel=<degps2> speed=<degps>`
 - `set_pump_constant pumpConstant_degpm=<val>`
 - `pump_purge pumpSpeed_degps=<deg/s> duration_ms=<ms>`

Notes:

- Key-values may be space or comma separated. Synonyms are no longer supported; use the exact key names shown above.
- Environment variables required: `INFLUXDB_URL`, `INFLUXDB_TOKEN`, `INFLUXDB_ORG`, `INFLUXDB_CMD_BUCKET`.

### Round-trip Testing

Use `RoundtripTest.py` to send a command and query it back from InfluxDB. It attempts to source `Secret.sh` if env vars are missing.
