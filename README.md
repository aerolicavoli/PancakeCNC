
# PancakeCNC

Source code, mechanical designs, and electrical schematics developed for the purpose of printing a pancake.

## Table of Contents

- [About the Project](#about-the-project)
- [System Architecture](#system-architecture)
  - [CNC Structure](#cnc-structure)
  - [Controller Box](#controller-box)
- [Generic Instruction Protocol](#generic-instruction-protocol)
  - [Message Layout](#message-layout)
  - [Opcodes](#opcodes)
  - [Execution Rules](#execution-rules)
  - [Example Payload](#example-payload)

## About the Project

Pancake CNC was built as a learning platform for system design, PCB design, low-level coding, kinematics, and control theory.

## System Architecture

This repository contains many, but not all, aspects of the machine's design.

### CNC Structure

The CNC structure includes the motors, gearing, hinge, and pump mechanisms.

#### Printed Parts

Files for the following peristaltic pump components are included:

- `PumpBase.stl`
- `PumpTop.stl`
- `PumpSpinner.stl`

The hinge components (`UpperHinge.stl`, `LowerHinge.stl`) are not currently tracked in the repository.

### Controller Box

The controller box houses the compute module, motor drivers, and user interface elements.

#### ControllerPCB

The ControllerPCB includes an ESP32-S3, power conditioning, 5 V level shifting, and general GPIO breakout.

## Generic Instruction Protocol

The **Generic Instruction Protocol** defines how commands are encoded and transmitted to the CNC system.

### Message Layout

Each message payload has the following byte structure:

| Byte Index | Field Name             | Size      | Description                                |
|------------|------------------------|-----------|--------------------------------------------|
| `[0]`      | **Op Code**            | 1 byte    | Identifies the instruction type            |
| `[1]`      | **Instruction Length** | 1 byte    | Number of bytes in the instruction payload |
| `[2..n+1]` | **Instruction Data**   | *n* bytes | Instruction payload                        |

Indices are zero-based.

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

### Example Payload

An example message encoding `0x04` (CNC Spiral Command) with 3 bytes of instruction data:

0x04 0x03 0xAA 0xBB 0xCC

- `0x04` → Spiral Command opcode  
- `0x03` → Length of instruction payload (`n = 3`)  
- `0xAA 0xBB 0xCC` → Instruction-specific data  
