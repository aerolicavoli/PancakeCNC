## Pancake_esp Command Protocol and CLI

This firmware consumes commands from an InfluxDB “command” bucket. Commands are binary packets encoded as:

- Byte 0: opcode
- Byte 1: payload length (n)
- Bytes 2..(n+1): payload bytes

### Opcodes (firmware)

- 0x69: Echo (E <message>)
- 0x01: Emergency Stop (immediate)
- 0x02: Resume (immediate)
- 0x11: CNC_Spiral
- 0x12: CNC_Jog (reserved)
- 0x13: Wait
- 0x14: CNC_Sine
- 0x15: CNC_ConstantSpeed

Payloads are little-endian C structs matching the guidance configs (see headers under `main/`).

### Python Command Terminal

The included `CommandTerminal.py` sends commands using a simplified syntax:

- `E Hello World`
- `CNC_Spiral CenterX_m=0.2 LinearSpeed_mps=0.05` (defaults apply; e.g., `SpiralConstant_mprad=0.2`)
- `Wait timeout_ms=500`
- `CNC_Sine Amplitude_deg=10 Frequency_hz=0.25`
- `CNC_ConstantSpeed S0Speed_degps=0 S1Speed_degps=45`
- `run_file TestProgram.txt` (newline-delimited commands)

Notes:

- Key-values may be space or comma separated. Synonyms `Center_X_m`/`Center_Y_m` are accepted.
- Environment variables required: `INFLUXDB_URL`, `INFLUXDB_TOKEN`, `INFLUXDB_ORG`, `INFLUXDB_CMD_BUCKET`.

### Round-trip Testing

Use `RoundtripTest.py` to send a command and query it back from InfluxDB. It attempts to source `Secret.sh` if env vars are missing.
