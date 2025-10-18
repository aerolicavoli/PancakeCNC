# PancakeCNC

A fully custom, pancake-printing CNC platform. This repository collects the mechanical CAD, controller electronics, firmware, ground station tooling, and reference simulations that work together to lay down batter in precise paths.

## Table of Contents
- [Project Overview](#project-overview)
- [Repository Layout](#repository-layout)
- [Mechanical Platform](#mechanical-platform)
- [Electronics and PCB](#electronics-and-pcb)
- [Firmware (`Pancake_esp`)](#firmware-pancake_esp)
- [Ground Station Tools](#ground-station-tools)
- [Simulation & Analysis](#simulation--analysis)
- [Design Documentation](#design-documentation)
- [Command & Telemetry Protocol](#command--telemetry-protocol)
- [Development Environment](#development-environment)
- [License](#license)

## Project Overview
PancakeCNC is a learning platform for end-to-end mechatronics: mechanical design, PCB layout, embedded firmware, networking, and motion control. The goal is to dispense pancake batter using precise CNC motion profiles driven from a custom controller and cloud-connected command stream.

## Repository Layout
```
ControllerPCB/   KiCad project files for the controller board and manufacturing assets.
DesignDocs/      Engineering notes, diagrams, and BOMs.
GroundStation/   Python-based tooling for sending CNC commands and replaying G-code.
PancakeSim/      MATLAB scripts for kinematic studies and controller validation.
Pancake_esp/     ESP-IDF firmware for the ESP32-S3 based controller.
```

## Mechanical Platform
Motion is handled by a double-jointed planar arm that provides two rotational degrees of freedom so the dispenser can reach any point in the intended print envelope. Batter is metered with a peristaltic pump mounted at the arm's end effector.

The printable CAD for the hinges and pump assembly is currently tracked outside this repository; the focus here is on the control stack that drives the mechanism.

## Electronics and PCB
The `ControllerPCB` directory captures the KiCad design for the controller enclosure. Highlights include:

- ESP32-S3 compute module, GPIO breakout, and headers that feed external step/dir motor drivers sourced from the BOM.
- Power conditioning and 5 V level shifting for peripheral interfaces.
- Ready-to-fabricate assets: schematic (`ControllerPCB.kicad_sch`), layout (`ControllerPCB.kicad_pcb`), and generated BOM/gerbers.

## Firmware (`Pancake_esp`)
Firmware is built with ESP-IDF and lives under `Pancake_esp/`. Key modules:

- **Command Handling** (`CommandHandler.*`, `CNCOpCodes.h`): parses high-level instructions into motion primitives.
- **Motion Control** (`MotorControl.*`, `StepperMotor.*`, `TrapezoidalJog.*`, `ArchimedeanSpiral.*`): generates coordinated movements for the S0/S1 axes and pump.
- **Networking & Telemetry** (`InfluxDBCmdAndTlm.*`, `Telemetry.*`, `WifiHandler.*`): streams commands from InfluxDB and publishes runtime state.
- **Safety & UI** (`Safety.*`, `UI.*`): implements interlocks and emergency stop logic. A dedicated human interface is still a work in progress—commands flow through the InfluxDB bridge today.

To build the firmware you will need the ESP-IDF toolchain (v5.x recommended). After configuring credentials in `sdkconfig`/`Secret.h`, standard `idf.py build flash monitor` targets apply.

## Ground Station Tools
`GroundStation/CommandTerminal.py` is a CLI utility for sending commands through InfluxDB using a human-readable syntax. Example interactions:

```bash
python GroundStation/CommandTerminal.py e "Hello Pancake"
python GroundStation/CommandTerminal.py cnc_spiral CenterX_m=0.2 LinearSpeed_mps=0.05
python GroundStation/CommandTerminal.py run_file GroundStation/GCode/TestProgram.txt
```

Environment variables must be set to connect to InfluxDB:

- `INFLUXDB_URL`
- `INFLUXDB_TOKEN`
- `INFLUXDB_ORG`
- `INFLUXDB_CMD_BUCKET`

`GroundStation/GCode/` contains sample programs for testing complex pours.

## Simulation & Analysis
`PancakeSim/KinematicTestBed.m` is a MATLAB script for exercising the inverse kinematics and closed-loop control algorithms before they are deployed to hardware.

## Design Documentation
`DesignDocs/` aggregates system-level context:

- `DesignDiagrams.drawio` — block diagrams and signal flows.
- `BillOfMaterials.md` — hardware inventory and sourcing notes.
- `Channelization.md` — signal/channel mapping across subsystems.

## Command & Telemetry Protocol
Commands are binary packets exchanged through InfluxDB and consumed by the firmware.

### Frame Layout
| Byte Index | Field | Size | Description |
|------------|-------|------|-------------|
| `[0]` | **Op Code** | 1 byte | Instruction identifier |
| `[1]` | **Length** | 1 byte | Number of payload bytes (*n*) |
| `[2..n+1]` | **Payload** | *n* bytes | Instruction-specific data |

### Firmware Opcodes
Immediate commands bypass the CNC queue:

- `0x01` — `pause`
- `0x02` — `resume`
- `0x03` — `stop`
- `0x69` — `echo`

Queued motion & configuration commands include:

- `0x11` — `cnc_spiral`
- `0x12` — `cnc_jog`
- `0x13` — `wait`
- `0x14` — `cnc_sine`
- `0x15` — `cnc_constant_speed`
- `0x16` — `set_motor_limits`
- `0x17` — `set_pump_constant`
- `0x18` — `cnc_arc`
- `0x19` — `pump_purge`

Payloads are little-endian C structs (refer to headers under `Pancake_esp/main/`). The CLI automatically translates key-value inputs into the correct binary layouts.

### Round-Trip Testing
`GroundStation/RoundtripTest.py` can send a command and fetch the recorded response, verifying connectivity and serialization. If environment variables are missing it will attempt to source `Secret.sh`.

## Development Environment
- **Firmware**: ESP-IDF (requires Python, CMake, Ninja). Install dependencies per Espressif documentation.
- **Ground Station**: Python 3.10+. Install requirements (if a `requirements.txt` is added) or run directly with the standard library and `influxdb-client`.
- **Mechanical/Electrical CAD**: KiCad 7.x for PCB edits, CAD/CAM packages for STL manipulation.

## License
This project is currently distributed without an explicit license; treat the assets as freely reusable unless noted otherwise in specific subdirectories.
