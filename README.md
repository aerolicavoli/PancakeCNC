
# PancakeCNC

Source code, mechanical designs, and electrical schematics developed for the purpose of printing a pancake.

## Table of Contents

- [About the Project](#about-the-project)
- [System Architecture](#system-architecture)
- [Generic Instruction Protocol](#generic-instruction-protocol)
- [Run File Designer](#run-file-designer)

---

## About the Project

Pancake CNC was built to learn and expand my capabilities in system design, PCB design, low level code, kinematics, and control.

## System Architecture

Many, but not all facets of this design are controlled in this repository.

### CNC Structure

The motors, gearing, hinge, and pump mechanisms.

#### Printed Parts

Checked into this repository are the following components

- Peristaltic pump components
  - PumpBase.stl
  - PumpTop.stl
  - PumpSpinner.stl
- Hinge Components
  - (Not checked in) UpperHinge.stl
  - (Not checked in) LowerHinge.stl

### Controller Box

The controller box houses the compute, motor driver, and user interface elements.

#### ControllerPCB

Houses an ESP32-s3, power conditioning, 5v level shifting, and general GPIO breakout.

## Generic Instruction Protocol

The **Generic Instruction Protocol** defines how commands are encoded and transmitted to the CNC system.  

Each message payload follows this structure:

---

### Message Layout

| Byte Index | Field Name             | Size      | Description                                |
|------------|------------------------|-----------|--------------------------------------------|
| `[0]`      | **Op Code**            | 1 byte    | Identifies the instruction type            |
| `[1]`      | **Instruction Length** | 1 byte    | Number of bytes in the instruction payload |
| `[2..n+1]` | **Instruction Data**   | *n* bytes | Instruction payload                        |

Message Layout:
[0] : Op Code (identifies the instruction type)
[1] : Length of the instruction payload (n)
[2..] : Instruction data (n bytes)


---

### Opcodes

| Op Code | Meaning                  |
|---------|--------------------------|
| `0x01`  | CNC Emergency Stop       |
| `0x02`  | CNC Resume               |
| `0x03`  | CNC Wait Command         |
| `0x04`  | CNC Spiral Command       |
| `0x05`  | CNC Jog Command          |
| `0x06`–`0x44` | **Reserved / Unused** |
| `0x45`  | Echo Payload             |

---

### Execution Rules

- **Immediate Execution**  
  The following opcodes are executed immediately when received, bypassing the CNC command queue:
  - `0x01` (Emergency Stop)  
  - `0x02` (Resume)  
  - `0x03` (Wait Command)  
  - `0x45` (Echo Payload)  

- **Queued Execution**  
  All other opcodes are pushed onto the **CNC Command Queue**.  
  - The queue ensures **sequential execution**: only one instruction is active at a time.  
  - A new instruction will not begin until the previous one has fully completed (e.g., CNC finishes its motion).  
  - This guarantees **deterministic command flow** and prevents overlapping motions.  

---

### Example Payload

An example message encoding `0x04` (CNC Spiral Command) with 3 bytes of instruction data:

0x04 0x03 0xAA 0xBB 0xCC

- `0x04` → Spiral Command opcode
- `0x03` → Length of instruction payload (`n = 3`)
- `0xAA 0xBB 0xCC` → Instruction-specific data

## Run File Designer

`run_file_designer.py` provides a desktop editor for constructing the
newline-delimited programs consumed by the controller's `run_file` command. The
Tkinter UI exposes a gridded representation of the griddle and lets you add and
reshape jogs, arcs, spirals and wait locations. Between every programmed feature
the tool automatically inserts a pump-off travel jog so frosting flow stops
during repositioning.

Key capabilities:

- Drag handles on each feature to move endpoints, arc centers, and spiral size.
- Edit numeric parameters (speed, radius, timeouts, etc.) via the property
  panel to dial-in precise geometry.
- Define wait locations that send the toolhead to a point and pause for a
  specified duration.
- Export a ready-to-run text file where each line is a controller command.

Launch the designer with:

```bash
python run_file_designer.py
```

The only dependency is the standard Tkinter module that ships with Python.
