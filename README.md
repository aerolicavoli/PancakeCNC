
# PancakeCNC

Source code, mechanical designs, and electrical schematics developed for the purpose of printing a pancake.

## Table of Contents

- [About the Project](#about-the-project)
- [System Architecture](#system-architecture)

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
