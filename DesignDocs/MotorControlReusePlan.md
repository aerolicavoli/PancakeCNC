# Motor Control Complexity and Reuse Improvement Plan

## Purpose

`MotorControlTask` currently combines queue handling, command decoding, guidance selection, motion solving, pump control, stop handling, motor output, limit switch handling, and telemetry publication in one long control loop. The goal of this plan is to keep the timing-critical loop small and predictable while moving reusable policies into focused components that can be tested without FreeRTOS or motor hardware.

## Current Complexity Drivers

1. **Many responsibilities in one task**
   - The task owns immediate command handling, queued configuration commands, motion opcode dispatch, target generation, cartesian-to-angle conversion, speed limiting, pump decisions, telemetry copies, and limit switch reactions.
   - Safety hold behavior is repeated in multiple branches, including explicit stop handling and out-of-bounds handling.

2. **Implicit shared state**
   - Loop state is spread across local variables (`instructionComplete`, `pumpThisMode`, `cmdViaAngle`, purge fields, target angles, target position, commanded speeds) and globals (`Pos_m`, `Vel_mps`, `Target_m`, telemetry data, `CNCEnabled`, `EStopActive`).
   - It is difficult to determine which fields must be reset when a command completes, pauses, stops, faults, or trips a limit switch.

3. **Opcode dispatch is repetitive**
   - Each guidance command repeats the same payload-length validation, `memcpy`, `ApplyConfig`, `currentGuidance` assignment, pump flag handling, and logging pattern.
   - Adding a new motion type requires editing the central switch and risks changing unrelated behavior.

4. **Hardware and policy are tightly coupled**
   - Speed computation, pump flow policy, and limit switch behavior are interleaved with direct `StepperMotor` calls and FreeRTOS queues.
   - This limits reuse from host-side tests and makes regression tests harder to write.

5. **Mixed coordinate command paths**
   - Cartesian guidance and angle guidance share state but diverge late in the loop.
   - Angle-specific completion and speed limiting for go-to-angle is embedded in the task instead of being a reusable angle-mode controller.

## Target Design

Refactor toward a small orchestration loop backed by reusable components:

```text
MotorControlTask
  ├── MotorControlState          // all mutable loop state in one struct
  ├── MotorCommandRouter         // now queue + cnc queue classification
  ├── GuidanceRegistry           // opcode -> typed guidance adapter
  ├── MotionPlanner              // target -> motor angle/speed command
  ├── PumpController             // print-flow and purge policy
  ├── SafetyHoldController       // pause/stop/fault/limit switch holds
  ├── MotorDriverSet             // S0/S1/pump StepperMotor facade
  └── TelemetryPublisher         // copies a state snapshot to telemetry
```

The end state should let `MotorControlTask` read telemetry, route commands, ask the active guidance for a target, ask the planner and pump controller for commands, apply safety overrides, write motor outputs, publish telemetry, and sleep.

## Behavior That Must Be Preserved

- Jog commands must remain able to request motion either with the pump on or with the pump off. The refactor should keep `CNC_JOG_OPCODE`'s `PumpOn` payload field as an explicit per-command policy, not collapse jog into a single default pump behavior.

## Refactoring Principles

- **Keep behavior-preserving steps small.** Each phase should compile and run tests before moving to the next phase.
- **Introduce seams before changing behavior.** First move existing code into functions/classes with equivalent inputs and outputs; then improve internals.
- **Prefer data ownership over parallel variables.** Replace groups of related locals with named structs and reset/apply helper functions.
- **Make policies host-testable.** Components that do not require ESP-IDF or GPIO should live behind simple interfaces and have unit tests under `Tests/`.
- **Use adapters for legacy guidance classes.** Avoid rewriting all guidance classes at once; wrap them in a common registry/factory pattern. Adapter metadata should preserve command-specific options such as jog's pump-on/pump-off selection.
- **Centralize stop/hold semantics.** All pause, stop, fault, unreachable-target, and limit-switch paths should share one hold command application path.

## Phased Plan

### Phase 1: Name and isolate loop state

1. Add a `MotorControlState` struct that owns the current position, velocity, target position, target angles, commanded speeds, active guidance pointer, completion flag, pump mode, purge state, command mode, pause state, and force-update flag.
2. Move repeated application of `MotionHoldCommand` into a single `ApplyHoldCommand(MotorControlState&, const MotionHoldCommand&)` helper.
3. Replace scattered local resets with state methods such as `IdleAtCurrentPosition(...)`, `StopPurge()`, and `CompleteInstruction()`.
4. Keep the existing globals as compatibility aliases initially, but update them from the state snapshot in one place.

**Success criteria**
- The main task has fewer independent local variables.
- Stop and out-of-bounds paths call the same hold-application helper.
- Existing behavior remains unchanged.

### Phase 2: Extract command routing and configuration handling

1. Create a `MotorCommandRouter` with methods for:
   - consuming immediate pause/resume/stop commands;
   - draining queued CNC commands when requested;
   - consuming non-motion configuration commands at the head of the CNC queue;
   - returning the next motion command only when the controller is ready.
2. Move config-command handlers into named methods (`ApplyMotorLimits`, `ApplyPumpConstant`, `ApplyAccelScale`, `StartPumpPurge`) with explicit payload validation.
3. Define a small `MotorControlConfig` struct for tunable values such as `pumpConstant_degpm`, `accelScale`, and position tolerance.
4. Validate payload lengths against command-specific expected sizes rather than open-ended checks where possible, unless a command intentionally supports extension.

**Success criteria**
- The task loop no longer contains queue peeking/parsing details.
- Config behavior is independently testable with synthetic payloads.
- Unknown or malformed commands produce one consistent error path.

### Phase 3: Replace central opcode switch with a guidance registry

1. Introduce a `GuidanceRegistry` that owns the existing guidance instances and maps each opcode to a descriptor:
   - opcode;
   - expected payload size;
   - pump policy source (`AlwaysOff`, `AlwaysOn`, or `FromPayload`);
   - typed apply function;
   - pointer/reference to the guidance object.
2. Factor the repeated validate/copy/apply/start pattern into one generic command loader.
3. Preserve special cases, such as jog's `PumpOn` flag and go-to-angle's angle-command mode, in the descriptor/adaptor instead of the main loop.
4. Add a lightweight unit test that submits representative opcode payloads and verifies the selected guidance, command mode, and pump policy. Include both `PumpOn=0` and `PumpOn=1` jog payloads.

**Success criteria**
- Adding a guidance type requires adding a descriptor and guidance instance, not editing a large switch.
- Payload validation and logging are uniform for all guidance commands.
- `MotorControlTask` only asks the registry to start a motion command.

### Phase 4: Extract motion planning

1. Create a `MotionPlanner` with explicit input and output structs:
   - inputs: current motor telemetry, current cartesian state, target position, guidance command mode, active guidance metadata, limits, accel scale;
   - outputs: target angles, S0/S1 commanded speeds, instruction-complete override, hold/fault request.
2. Move `WrapAngleDeltaDeg` and `ComputeDecelLimitedSpeedDegps` into the planner or a reusable motion math module.
3. Split planning by mode:
   - `PlanCartesianTarget` performs `CartToAng`, reachability checks, and speed limiting;
   - `PlanAngleTarget` handles go-to-angle target angles, tolerance, completion, and speed limiting;
   - `PlanIdleHold` generates zero-speed angle-mode hold commands.
4. Represent reachability failures as a planner status instead of stopping directly inside planning code; the task or safety controller applies the hold.

**Success criteria**
- Cartesian and angle command paths are explicit and separately testable.
- Out-of-bounds handling reuses the same stop/hold path as immediate stop.
- Speed limiting can be validated with host-side tests.

### Phase 5: Extract pump policy

1. Create a `PumpController` that owns purge state and computes pump speed from:
   - emergency-stop state;
   - instruction-active state;
   - active command pump policy, including jog's per-command pump-on/pump-off choice;
   - target error magnitude;
   - current cartesian velocity;
   - configured pump constant.
2. Give purge override a clear priority over normal print-flow mode while still respecting pause/stop.
3. Replace bare purge locals with methods such as `StartPurge(speed, duration)`, `CancelPurge()`, and `Update(period_ms)`.
4. Add tests for normal printing, no-pump modes, position tolerance gating, purge timeout, and pause cancellation.

**Success criteria**
- Pump behavior is no longer interleaved with cartesian planning.
- Pump policy can be reused by future print modes without touching motor planning, while still allowing jogs to choose whether they dispense.
- Purge state transitions are covered by tests.

### Phase 6: Isolate motor hardware access

1. Create a `MotorDriverSet` facade around S0, S1, and pump motors.
2. Move repeated target-speed and update-speed calls into `ApplyMotorSpeeds(const MotorSpeedCommand&, bool forceUpdate)`.
3. Move motor limit updates into facade methods that accept motor IDs and all-motors broadcasts.
4. Add a fake motor-driver implementation for unit tests, or define a minimal interface where host tests can assert commanded speeds without ESP timers.

**Success criteria**
- Motor output application is one method call from the task loop.
- Config commands no longer capture concrete `StepperMotor` objects in local lambdas.
- Host tests can exercise router/planner/controller code without real motors.

### Phase 7: Centralize telemetry publishing and synchronization

1. Create a `TelemetryPublisher` that takes a `MotorControlState` snapshot and motor telemetry snapshot.
2. Keep all writes to `TelemetryData` in one function.
3. Decide whether telemetry needs a FreeRTOS critical section, mutex, or double-buffered snapshot, then apply it in the publisher only.
4. Include pause/active-guidance/pump-purge status fields if the telemetry model supports them later.

**Success criteria**
- The task loop no longer manually copies every telemetry field.
- Thread-safety changes are isolated to one publisher.
- Future telemetry fields can be added without changing planning logic.

### Phase 8: Limit switch and safety integration

1. Move limit switch handling into `SafetyHoldController` or a dedicated `LimitSwitchController`.
2. Make the limit-switch result explicit: directional inhibit, zero request, instruction-complete request, and optional queue drain/fault state.
3. Use the same state transition methods for limit-switch stops as for pause/stop/faults.
4. Add tests that assert the correct motor receives the correct directional inhibit and zero request.

**Success criteria**
- Limit switch policy is not mixed with telemetry publication.
- Every safety-related transition has one clearly named path.
- Safety behavior can be reviewed without reading the entire motor loop.

## Suggested Intermediate File Layout

```text
Pancake_esp/main/
  MotorControl.cpp              // short FreeRTOS orchestration task
  MotorControl.h
  MotorControlState.h           // state and command structs
  MotorCommandRouter.h/.cpp     // queue parsing and command classification
  GuidanceRegistry.h/.cpp       // opcode-to-guidance adapters
  MotionPlanner.h/.cpp          // reusable target/speed planning
  PumpController.h/.cpp         // reusable pump and purge policy
  MotorDriverSet.h/.cpp         // StepperMotor facade
  TelemetryPublisher.h/.cpp     // TelemetryData snapshot writes
```

Start with header-only structs and small `.cpp` files only where necessary. If ESP-IDF include dependencies make host tests hard, keep pure policy headers free of FreeRTOS and GPIO headers.

## Testing Strategy

1. **Host unit tests first**
   - Add tests for `ComputeDecelLimitedSpeedDegps`, angle wrapping, cartesian reachability status, pump purge timing, and command payload validation.
2. **Regression tests around existing guidance**
   - Use existing guidance classes directly to verify registry selection does not alter target generation.
3. **Fake queue and fake motor tests**
   - Test command routing and motor output application without FreeRTOS timers or GPIO.
4. **On-device smoke tests after each major phase**
   - Pause/resume/stop.
   - Jog with pump off/on.
   - Spiral/arc/rectangle print commands.
   - Go-to-angle completion tolerance.
   - Pump purge during idle and during active motion.
   - Limit switch trip and reset behavior.

## Recommended Implementation Order

1. `MotorControlState` and hold-command application helper.
2. `PumpController`, because it is low-risk and easy to test.
3. `MotionPlanner`, because it removes the highest-risk math from the task loop.
4. `GuidanceRegistry`, because it reduces opcode-switch churn after planner seams exist.
5. `MotorCommandRouter`, because it becomes simpler once config and motion targets have homes.
6. `MotorDriverSet` and `TelemetryPublisher`, because these are mostly integration cleanup.
7. `LimitSwitchController`/safety cleanup, after the hold path is proven.

## Risks and Mitigations

- **Risk: behavior changes during extraction.** Mitigate by snapshotting current behavior with tests before changing policy internals.
- **Risk: ESP-IDF dependencies leak into host tests.** Mitigate with pure data structs and facades around queues, GPIO, and `StepperMotor`.
- **Risk: registry descriptors hide important special cases.** Mitigate by making special policy fields explicit (`pumpPolicy`, `commandMode`, `completionPolicy`) and testing each opcode.
- **Risk: too many tiny classes too early.** Mitigate by extracting functions and structs first, then promoting them to classes only when there is clear state or interface ownership.

## Definition of Done

The refactor is complete when:

- `MotorControlTask` is primarily orchestration and fits on one screen or close to it.
- All pause/stop/fault/limit-switch holds share one state transition path.
- Adding a new guidance opcode does not require duplicating payload-validation boilerplate.
- Motion math and pump policy have host-side unit coverage.
- Motor hardware access and telemetry writes are isolated behind narrow interfaces.
- Existing on-device motion behavior is preserved unless a change is intentionally documented.

## Refactor Progress

This checklist tracks what has actually been completed in code, not just what has been planned.

- [x] **Phase 1: Name and isolate loop state** — Complete.
  - [x] Added `MotorControlState` to own current position, velocity, target position, target angles, commanded speeds, active guidance, completion state, pump state, purge state, command mode, pause state, and force-update state.
  - [x] Added shared state helpers, including `ApplyHoldCommand`, `IdleAtCurrentPosition`, `StopPurge`, `CompleteInstruction`, and `StartInstruction`.
  - [x] Replaced repeated stop/out-of-bounds hold assignments with the shared hold application path.
  - [x] Kept existing position/target/pause globals synchronized from the state snapshot for compatibility.
- [x] **Phase 2: Extract command routing and configuration handling** — Complete.
  - [x] Added `MotorCommandRouter` to consume immediate pause/resume/stop commands, drain the CNC queue, consume head-of-queue configuration commands, and return the next motion command only when the state is ready.
  - [x] Moved configuration handlers into named router methods for motor limits, pump constant, acceleration scale, and pump purge.
  - [x] Added `MotorControlConfig` for tunable pump constant, acceleration scale, and position tolerance.
  - [x] Configuration handlers now validate command-specific payload lengths exactly before applying payload contents.
- [x] **Phase 3: Replace central opcode switch with a guidance registry** — Complete.
  - [x] Added `GuidanceRegistry` with opcode descriptors, expected payload sizes, typed apply functions, guidance pointers, explicit pump policy sources, and command-mode metadata.
  - [x] Replaced the repeated guidance payload validate/copy/apply/start switch with a generic registry loader in `MotorControlTask`.
  - [x] Preserved jog pump selection by resolving `CNC_JOG_OPCODE` pump behavior from the `JogConfig::PumpOn` payload field, including both pump-off and pump-on unit coverage.
  - [x] Moved command-mode selection into registry metadata so go-to-angle, sine, and constant-speed commands are started as angle-mode commands by descriptor rather than by switch-case side effect.
  - [x] Added unit coverage for registry selection, pump policy, command-mode metadata, invalid payload length handling, and unknown opcode handling.
- [ ] **Phase 4: Extract motion planning** — Not started. Cartesian-to-angle conversion, reachability handling, and angle-mode speed limiting still live in `MotorControlTask`.
- [ ] **Phase 5: Extract pump policy** — Not started. Normal pump-speed calculation and purge timing still live in `MotorControlTask`, although Phase 3 now makes the active command's pump-policy source explicit.
- [ ] **Phase 6: Isolate motor hardware access** — Not started. Motor speed commands and limit updates still call `StepperMotor` directly from the task/router integration.
- [ ] **Phase 7: Centralize telemetry publishing and synchronization** — Not started. Telemetry writes are still performed directly in the task loop.
- [ ] **Phase 8: Limit switch and safety integration** — Not started. Limit switch handling is still interleaved with telemetry and motor-control logic.
