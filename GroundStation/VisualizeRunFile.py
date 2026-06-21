#!/usr/bin/env python3
"""Preview the command intent of a PancakeCNC run_file program."""

from __future__ import annotations

import argparse
import math
import shlex
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Optional

try:
    from GroundStation.CommandTerminal import DEFAULTS, _canonical_cmd_name, _parse_kv_tokens
except ModuleNotFoundError:  # pragma: no cover - direct script execution fallback
    from CommandTerminal import DEFAULTS, _canonical_cmd_name, _parse_kv_tokens


C_DEG_TO_RAD = math.pi / 180.0
C_RAD_TO_DEG = 180.0 / math.pi
C_MS_TO_S = 0.001
EPSILON = 1.0e-10

C_S0_LENGTH_M = 0.22
C_S1_LENGTH_M = 0.126
C_MAX_REACH_M = C_S0_LENGTH_M + C_S1_LENGTH_M
C_MIN_REACH_M = C_S0_LENGTH_M - C_S1_LENGTH_M

SAMPLE_PERIOD_MS = 10
GO_HOME_S0_DEG = 120.0
GO_HOME_S1_DEG = -115.0

DRAW_LINE_WIDTH = 3.0
TRAVEL_LINE_WIDTH = 0.8

MOTION_COMMANDS = {
    "cnc_jog",
    "cnc_arc",
    "cnc_spiral",
    "cnc_rectangle",
    "cnc_go_to_angle",
    "cnc_go_home",
    "cnc_home",
    "wait",
}
CONFIG_COMMANDS = {
    "set_motor_limits",
    "set_pump_constant",
    "set_accel_scale",
    "pump_purge",
}
LOCAL_COMMANDS = {
    "ask_to_continue",
    "terminal_wait",
    "e",
    "pause",
    "resume",
    "stop",
    "crash_diagnostic",
    "local_origin",
}


class IntentError(RuntimeError):
    """Raised when a run-file line cannot be converted into intent geometry."""


# Compatibility with the previous implementation and tests.
SimulationError = IntentError


@dataclass(frozen=True)
class Vec2:
    x: float
    y: float

    def __add__(self, other: "Vec2") -> "Vec2":
        return Vec2(self.x + other.x, self.y + other.y)

    def __sub__(self, other: "Vec2") -> "Vec2":
        return Vec2(self.x - other.x, self.y - other.y)

    def __mul__(self, scale: float) -> "Vec2":
        return Vec2(self.x * scale, self.y * scale)

    def magnitude(self) -> float:
        return math.hypot(self.x, self.y)

    def as_tuple(self) -> tuple[float, float]:
        return (self.x, self.y)


@dataclass
class ParsedCommand:
    line_no: int
    raw: str
    cmd: str
    args: dict[str, Any] = field(default_factory=dict)


@dataclass
class TraceSegment:
    points: list[Vec2]
    pump_on: bool
    line_no: int
    command: str
    duration_ms: int = 0


@dataclass
class AnimationStep:
    points: list[Vec2]
    pump_on: bool
    line_no: int
    command: str
    duration_ms: int


@dataclass
class IntentResult:
    segments: list[TraceSegment]
    events: list[str]
    final_position_m: Vec2
    final_s0_deg: Optional[float] = None
    final_s1_deg: Optional[float] = None
    timeline: list[AnimationStep] = field(default_factory=list)

    @property
    def requested_segments(self) -> list[TraceSegment]:
        return self.segments

    @property
    def simulated_segments(self) -> list[TraceSegment]:
        return self.segments


# Compatibility with the previous public name.
SimulationResult = IntentResult


def ang_to_cart(s0_deg: float, s1_deg: float) -> Vec2:
    phi_rad = (s0_deg + s1_deg) * C_DEG_TO_RAD
    theta_rad = s0_deg * C_DEG_TO_RAD
    return Vec2(
        math.sin(theta_rad) * C_S0_LENGTH_M + math.sin(phi_rad) * C_S1_LENGTH_M,
        math.cos(theta_rad) * C_S0_LENGTH_M + math.cos(phi_rad) * C_S1_LENGTH_M,
    )


def cart_to_ang(pos_m: Vec2) -> tuple[float, float]:
    """Return the firmware's nominal IK solution for convenience/start overrides."""
    if not (math.isfinite(pos_m.x) and math.isfinite(pos_m.y)):
        raise IntentError("target is not finite")

    target_dist_sq = pos_m.x * pos_m.x + pos_m.y * pos_m.y
    if not math.isfinite(target_dist_sq) or target_dist_sq < EPSILON:
        raise IntentError("target is too close to the origin")

    target_dist = math.sqrt(target_dist_sq)
    target_ang_rad = math.atan2(pos_m.x, pos_m.y)
    if target_dist > C_MAX_REACH_M + EPSILON:
        raise IntentError("target is outside max reach")
    if target_dist < C_MIN_REACH_M - EPSILON:
        raise IntentError("target is inside min reach")

    s0_cos_arg = (
        (C_S0_LENGTH_M * C_S0_LENGTH_M - C_S1_LENGTH_M * C_S1_LENGTH_M + target_dist_sq) /
        (2.0 * C_S0_LENGTH_M * target_dist)
    )
    s1_cos_arg = (
        (C_S0_LENGTH_M * C_S0_LENGTH_M + C_S1_LENGTH_M * C_S1_LENGTH_M - target_dist_sq) /
        (2.0 * C_S0_LENGTH_M * C_S1_LENGTH_M)
    )
    s0_cos_arg = max(-1.0, min(1.0, s0_cos_arg))
    s1_cos_arg = max(-1.0, min(1.0, s1_cos_arg))

    s0_deg = (target_ang_rad + math.acos(s0_cos_arg)) * C_RAD_TO_DEG
    s1_deg = (math.acos(s1_cos_arg) - math.pi) * C_RAD_TO_DEG
    return s0_deg, s1_deg


def home_position_m() -> Vec2:
    return ang_to_cart(GO_HOME_S0_DEG, GO_HOME_S1_DEG)


def get_reachable_rectangle_corners(inset_m: float = 0.0) -> list[Vec2]:
    inset = max(0.0, inset_m)
    inner_radius = C_MIN_REACH_M + inset
    outer_radius = C_MAX_REACH_M - inset
    if outer_radius <= inner_radius or outer_radius <= 0.0:
        raise IntentError("rectangle inset is too large for the arm reach envelope")

    top_y = (inner_radius + math.sqrt(inner_radius * inner_radius + 8.0 * outer_radius * outer_radius)) * 0.25
    if top_y <= inner_radius or top_y >= outer_radius:
        raise IntentError("rectangle inset does not produce valid corners")

    half_width = math.sqrt(outer_radius * outer_radius - top_y * top_y)
    if half_width <= 0.0:
        raise IntentError("rectangle width is not positive")

    return [
        Vec2(-half_width, inner_radius),
        Vec2(half_width, inner_radius),
        Vec2(half_width, top_y),
        Vec2(-half_width, top_y),
    ]


def _apply_local_origin(cmd: str, args: dict[str, Any], local_origin: Vec2) -> dict[str, Any]:
    """Return args with firmware-style local origin applied to cartesian fields."""
    adjusted = dict(args)
    if cmd == "cnc_jog":
        adjusted["TargetX_m"] = float(adjusted["TargetX_m"]) + local_origin.x
        adjusted["TargetY_m"] = float(adjusted["TargetY_m"]) + local_origin.y
    elif cmd in {"cnc_arc", "cnc_spiral"}:
        adjusted["CenterX_m"] = float(adjusted["CenterX_m"]) + local_origin.x
        adjusted["CenterY_m"] = float(adjusted["CenterY_m"]) + local_origin.y
    return adjusted


def _resolve_run_file_path(path_text: str, including_path: Path) -> Path:
    path = Path(path_text)
    if path.is_absolute():
        return path
    candidate = including_path.parent / path
    if candidate.exists():
        return candidate
    return path


def _parse_program(path: Path, include_stack: list[Path], local_origin: Vec2) -> tuple[list[ParsedCommand], Vec2]:
    resolved_path = path.resolve()
    if resolved_path in include_stack:
        chain = " -> ".join(str(item) for item in [*include_stack, resolved_path])
        raise IntentError(f"recursive run_file include disallowed: {chain}")

    commands: list[ParsedCommand] = []
    include_stack.append(resolved_path)
    lines = resolved_path.read_text(encoding="utf-8").splitlines()
    try:
        for line_no, raw_line in enumerate(lines, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            parts = shlex.split(line)
            if not parts:
                continue

            cmd = _canonical_cmd_name(parts[0])
            if len(parts) >= 2 and parts[1].lower() in {"help", "-h", "?"}:
                continue

            if cmd == "run_file":
                if len(parts) < 2:
                    raise IntentError(f"line {line_no}: run_file requires a path")
                child_path = _resolve_run_file_path(parts[1], resolved_path)
                child_commands, local_origin = _parse_program(child_path, include_stack, local_origin)
                commands.extend(child_commands)
                continue

            if cmd in {"ask_to_continue", "e"}:
                args: dict[str, Any] = {}
            else:
                args = _parse_kv_tokens(parts[1:])

            if cmd in DEFAULTS:
                args = {**DEFAULTS[cmd], **args}

            if cmd == "local_origin":
                local_origin = Vec2(float(args.get("OriginX_m", 0.0)), float(args.get("OriginY_m", 0.0)))
            else:
                args = _apply_local_origin(cmd, args, local_origin)

            commands.append(ParsedCommand(line_no=line_no, raw=line, cmd=cmd, args=args))
    finally:
        include_stack.pop()

    return commands, local_origin


def parse_program(path: Path) -> list[ParsedCommand]:
    commands, _ = _parse_program(path, [], Vec2(0.0, 0.0))
    return commands


def _sample_count_for_distance(distance_m: float, speed_mps: float, sample_period_ms: int) -> int:
    if distance_m <= 0.0:
        return 1
    step_m = abs(speed_mps) * sample_period_ms * C_MS_TO_S
    if step_m <= 0.0:
        return 1
    return max(1, int(math.ceil((distance_m / step_m) - EPSILON)))


def _sample_line(start: Vec2, end: Vec2, speed_mps: float, sample_period_ms: int) -> list[Vec2]:
    delta = end - start
    steps = _sample_count_for_distance(delta.magnitude(), speed_mps, sample_period_ms)
    return [start + delta * (i / steps) for i in range(steps + 1)]


def _sample_arc(args: dict[str, Any], sample_period_ms: int) -> list[Vec2]:
    start_theta = float(args["StartTheta_rad"])
    end_theta = float(args["EndTheta_rad"])
    radius_m = float(args["Radius_m"])
    speed_mps = float(args["LinearSpeed_mps"])
    center = Vec2(float(args["CenterX_m"]), float(args["CenterY_m"]))

    if radius_m <= 0.0:
        raise IntentError("cnc_arc Radius_m must be positive")

    delta_theta = end_theta - start_theta
    arc_length = abs(delta_theta) * radius_m
    steps = _sample_count_for_distance(arc_length, speed_mps, sample_period_ms)
    return [
        Vec2(
            center.x + math.sin(start_theta + delta_theta * (i / steps)) * radius_m,
            center.y + math.cos(start_theta + delta_theta * (i / steps)) * radius_m,
        )
        for i in range(steps + 1)
    ]


def _sample_spiral(args: dict[str, Any], sample_period_ms: int) -> list[Vec2]:
    spiral_constant_mprad = float(args["SpiralConstant_mprad"])
    spiral_rate_radps = float(args["SpiralRate_radps"])
    linear_speed_mps = float(args["LinearSpeed_mps"])
    center = Vec2(float(args["CenterX_m"]), float(args["CenterY_m"]))
    max_radius_m = float(args["MaxRadius_m"])

    values = [spiral_constant_mprad, spiral_rate_radps, linear_speed_mps, center.x, center.y, max_radius_m]
    if not all(math.isfinite(value) for value in values):
        raise IntentError("cnc_spiral contains a non-finite value")
    if spiral_constant_mprad <= 0.0 or spiral_rate_radps <= 0.0 or max_radius_m <= 0.0:
        raise IntentError("cnc_spiral requires positive SpiralConstant, SpiralRate, and MaxRadius")

    theta_rad = 0.0
    points: list[Vec2] = []
    max_points = 250_000
    for _ in range(max_points):
        radius_m = theta_rad * spiral_constant_mprad
        points.append(Vec2(
            center.x + math.sin(theta_rad) * radius_m,
            center.y + math.cos(theta_rad) * radius_m,
        ))
        if radius_m > max_radius_m:
            break

        if linear_speed_mps <= 0.0 or spiral_rate_radps * radius_m < linear_speed_mps:
            theta_rate_radps = spiral_rate_radps
        else:
            theta_rate_radps = linear_speed_mps / radius_m
        theta_rad += theta_rate_radps * sample_period_ms * C_MS_TO_S
    else:
        raise IntentError("cnc_spiral generated too many preview points")

    return points


def _append_segment(segments: list[TraceSegment], timeline: list[AnimationStep], points: list[Vec2],
                    pump_on: bool, command: ParsedCommand, sample_period_ms: int) -> Vec2:
    if not points:
        raise IntentError(f"line {command.line_no}: {command.cmd} generated no points")
    if len(points) >= 2:
        duration_ms = max(sample_period_ms, (len(points) - 1) * sample_period_ms)
        segments.append(TraceSegment(points, pump_on, command.line_no, command.cmd, duration_ms))
        timeline.append(AnimationStep(points, pump_on, command.line_no, command.cmd, duration_ms))
    return points[-1]


def build_intent(commands: Iterable[ParsedCommand], sample_period_ms: int = SAMPLE_PERIOD_MS,
                 start_s0_deg: float = GO_HOME_S0_DEG,
                 start_s1_deg: float = GO_HOME_S1_DEG) -> IntentResult:
    if sample_period_ms <= 0:
        raise ValueError("sample_period_ms must be positive")

    current = ang_to_cart(start_s0_deg, start_s1_deg)
    current_s0_deg: Optional[float] = start_s0_deg
    current_s1_deg: Optional[float] = start_s1_deg
    segments: list[TraceSegment] = []
    timeline: list[AnimationStep] = []
    events: list[str] = []

    for command in commands:
        if command.cmd in CONFIG_COMMANDS:
            events.append(f"line {command.line_no}: ignored config command {command.cmd}")
            continue

        if command.cmd in LOCAL_COMMANDS:
            if command.cmd in {"ask_to_continue", "terminal_wait"}:
                events.append(f"line {command.line_no}: ignored local command {command.cmd}")
            continue

        if command.cmd not in MOTION_COMMANDS:
            raise IntentError(f"line {command.line_no}: unsupported command {command.cmd}")

        try:
            if command.cmd == "cnc_jog":
                target = Vec2(float(command.args["TargetX_m"]), float(command.args["TargetY_m"]))
                speed_mps = float(command.args.get("LinearSpeed_mps", command.args.get("MaxLinearSpeed_mps", 0.05)))
                points = _sample_line(current, target, speed_mps, sample_period_ms)
                current = _append_segment(
                    segments,
                    timeline,
                    points,
                    int(command.args.get("PumpOn", 0)) != 0,
                    command,
                    sample_period_ms,
                )
                current_s0_deg = None
                current_s1_deg = None

            elif command.cmd == "cnc_arc":
                points = _sample_arc(command.args, sample_period_ms)
                current = _append_segment(segments, timeline, points, True, command, sample_period_ms)
                current_s0_deg = None
                current_s1_deg = None

            elif command.cmd == "cnc_spiral":
                points = _sample_spiral(command.args, sample_period_ms)
                current = _append_segment(segments, timeline, points, True, command, sample_period_ms)
                current_s0_deg = None
                current_s1_deg = None

            elif command.cmd == "cnc_rectangle":
                speed_mps = float(command.args["LinearSpeed_mps"])
                corners = get_reachable_rectangle_corners(float(command.args["InsetDistance_m"]))
                path = [*corners, corners[0]]
                for target in path:
                    points = _sample_line(current, target, speed_mps, sample_period_ms)
                    current = _append_segment(segments, timeline, points, True, command, sample_period_ms)
                current_s0_deg = None
                current_s1_deg = None

            elif command.cmd == "cnc_go_to_angle":
                target_s0_deg = float(command.args["TargetS0_deg"])
                target_s1_deg = float(command.args["TargetS1_deg"])
                target = ang_to_cart(target_s0_deg, target_s1_deg)
                points = _sample_line(current, target, 0.05, sample_period_ms)
                current = _append_segment(segments, timeline, points, False, command, sample_period_ms)
                current_s0_deg = target_s0_deg
                current_s1_deg = target_s1_deg

            elif command.cmd in {"cnc_go_home", "cnc_home"}:
                if command.cmd == "cnc_home":
                    events.append(f"line {command.line_no}: cnc_home previewed as go-home position")
                target = home_position_m()
                points = _sample_line(current, target, 0.05, sample_period_ms)
                current = _append_segment(segments, timeline, points, False, command, sample_period_ms)
                current_s0_deg = GO_HOME_S0_DEG
                current_s1_deg = GO_HOME_S1_DEG

            elif command.cmd == "wait":
                wait_ms = int(command.args["timeout_ms"])
                if wait_ms < 0:
                    events.append(f"line {command.line_no}: wait indefinitely (not animated)")
                else:
                    events.append(f"line {command.line_no}: wait {wait_ms} ms")
                    if wait_ms > 0:
                        timeline.append(AnimationStep([current], False, command.line_no, command.cmd, wait_ms))

        except KeyError as exc:
            raise IntentError(f"line {command.line_no}: missing required key {exc.args[0]}") from exc
        except ValueError as exc:
            raise IntentError(f"line {command.line_no}: invalid value in {command.cmd}: {exc}") from exc

    return IntentResult(
        segments=segments,
        events=events,
        final_position_m=current,
        final_s0_deg=current_s0_deg,
        final_s1_deg=current_s1_deg,
        timeline=timeline,
    )


def simulate_commands(commands: Iterable[ParsedCommand], dt_ms: int = SAMPLE_PERIOD_MS,
                      start_s0_deg: float = GO_HOME_S0_DEG, start_s1_deg: float = GO_HOME_S1_DEG,
                      max_command_seconds: float = 300.0) -> IntentResult:
    del max_command_seconds
    return build_intent(
        commands,
        sample_period_ms=dt_ms,
        start_s0_deg=start_s0_deg,
        start_s1_deg=start_s1_deg,
    )


def simulate_file(path: Path, dt_ms: int = SAMPLE_PERIOD_MS,
                  start_s0_deg: float = GO_HOME_S0_DEG,
                  start_s1_deg: float = GO_HOME_S1_DEG,
                  max_command_seconds: float = 300.0) -> IntentResult:
    del max_command_seconds
    return build_intent(
        parse_program(path),
        sample_period_ms=dt_ms,
        start_s0_deg=start_s0_deg,
        start_s1_deg=start_s1_deg,
    )


def _plot_points(ax: Any, points: list[Vec2], **kwargs: Any) -> None:
    if len(points) < 2:
        return
    ax.plot([point.x for point in points], [point.y for point in points], **kwargs)


def _plot_trace_segments(ax: Any, segments: list[TraceSegment]) -> None:
    seen_labels: set[str] = set()
    for segment in segments:
        label = "batter path" if segment.pump_on else "travel path"
        display_label = label if label not in seen_labels else None
        seen_labels.add(label)
        _plot_points(
            ax,
            segment.points,
            color="#d95f02" if segment.pump_on else "#2c5aa0",
            linewidth=DRAW_LINE_WIDTH if segment.pump_on else TRAVEL_LINE_WIDTH,
            alpha=0.95,
            label=display_label,
        )


def plot_intent(result: IntentResult, output: Optional[Path] = None,
                show: bool = True, title: str = "PancakeCNC run preview") -> None:
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(8, 8))

    theta = [2.0 * math.pi * i / 720.0 for i in range(721)]
    for radius, label in ((C_MIN_REACH_M, "min/max reach"), (C_MAX_REACH_M, None)):
        ax.plot(
            [radius * math.sin(t) for t in theta],
            [radius * math.cos(t) for t in theta],
            color="#bbbbbb",
            linewidth=0.9,
            linestyle="--",
            label=label,
        )

    _plot_trace_segments(ax, result.segments)

    home = home_position_m()
    ax.scatter([home.x], [home.y], marker="*", s=160, color="#228b22",
               edgecolors="#0f3d0f", linewidths=0.8, label="home position", zorder=6)

    if result.segments:
        start = result.segments[0].points[0]
        ax.scatter([start.x], [start.y], marker="o", s=40, color="#111111", label="start", zorder=5)
    ax.scatter([result.final_position_m.x], [result.final_position_m.y], marker="x", s=60,
               color="#111111", label="final", zorder=5)

    ax.set_aspect("equal", adjustable="box")
    ax.set_xlabel("X (m)")
    ax.set_ylabel("Y (m)")
    ax.set_title(title)
    ax.grid(True, linewidth=0.5, alpha=0.35)
    ax.legend(loc="best", fontsize="small")
    fig.tight_layout()

    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(output, dpi=160)

    if show:
        plt.show()
    else:
        plt.close(fig)


# Compatibility with the previous public function name.
plot_simulation = plot_intent


@dataclass(frozen=True)
class _AnimationFrame:
    step_index: int
    point_index: int
    elapsed_ms: float


def _animation_status(step: AnimationStep, elapsed_ms: float, total_ms: float) -> str:
    elapsed_s = elapsed_ms * C_MS_TO_S
    total_s = total_ms * C_MS_TO_S
    if step.command == "wait":
        return f"t={elapsed_s:.1f}/{total_s:.1f}s | line {step.line_no}: wait {step.duration_ms * C_MS_TO_S:.1f}s"
    pump = "pump on" if step.pump_on else "pump off"
    return f"t={elapsed_s:.1f}/{total_s:.1f}s | line {step.line_no}: {step.command} | {pump}"


def _animation_frame_at(timeline: list[AnimationStep], elapsed_ms: float) -> _AnimationFrame:
    cumulative_ms = 0.0
    last_index = len(timeline) - 1
    for step_index, step in enumerate(timeline):
        duration_ms = max(0, step.duration_ms)
        next_ms = cumulative_ms + duration_ms
        if elapsed_ms <= next_ms or step_index == last_index:
            if duration_ms <= 0:
                progress = 1.0
            else:
                progress = (elapsed_ms - cumulative_ms) / duration_ms
            progress = max(0.0, min(1.0, progress))
            point_index = int(progress * max(0, len(step.points) - 1))
            return _AnimationFrame(step_index, point_index, elapsed_ms)
        cumulative_ms = next_ms
    return _AnimationFrame(last_index, max(0, len(timeline[-1].points) - 1), elapsed_ms)


def _build_animation_frames(timeline: list[AnimationStep], fps: float,
                            time_scale: float) -> list[_AnimationFrame]:
    if fps <= 0.0:
        raise ValueError("fps must be positive")
    if time_scale <= 0.0:
        raise ValueError("time_scale must be positive")
    if not timeline:
        return []

    total_ms = sum(max(0, step.duration_ms) for step in timeline)
    if total_ms <= 0:
        return [_animation_frame_at(timeline, 0.0)]

    frame_interval_ms = 1000.0 / fps
    display_total_ms = total_ms * time_scale
    frame_count = max(1, int(math.ceil(display_total_ms / frame_interval_ms)))
    frames: list[_AnimationFrame] = []
    for frame_no in range(frame_count + 1):
        display_elapsed_ms = min(frame_no * frame_interval_ms, display_total_ms)
        original_elapsed_ms = min(display_elapsed_ms / time_scale, total_ms)
        frames.append(_animation_frame_at(timeline, original_elapsed_ms))
    return frames


def _line_data_for_frame(timeline: list[AnimationStep], frame: _AnimationFrame,
                         pump_on: bool) -> tuple[list[float], list[float]]:
    xs: list[float] = []
    ys: list[float] = []
    for step_index, step in enumerate(timeline[:frame.step_index + 1]):
        if step.command == "wait" or step.pump_on != pump_on:
            continue
        last_point_index = len(step.points) - 1
        if step_index == frame.step_index:
            last_point_index = frame.point_index
        if last_point_index < 1:
            continue
        for point in step.points[:last_point_index + 1]:
            xs.append(point.x)
            ys.append(point.y)
        xs.append(math.nan)
        ys.append(math.nan)
    return xs, ys


def save_animation_gif(result: IntentResult, output: Path, fps: float = 20.0,
                       time_scale: float = 1.0,
                       title: str = "PancakeCNC run animation") -> None:
    """Save a timeline-aware animated GIF of a run preview."""
    if fps <= 0.0:
        raise ValueError("fps must be positive")
    if time_scale <= 0.0:
        raise ValueError("time_scale must be positive")

    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation, PillowWriter

    timeline = result.timeline
    if not timeline:
        timeline = [AnimationStep([result.final_position_m], False, 0, "final", 1)]

    frames = _build_animation_frames(timeline, fps=fps, time_scale=time_scale)
    total_ms = sum(max(0, step.duration_ms) for step in timeline)

    fig, ax = plt.subplots(figsize=(8, 8))

    theta = [2.0 * math.pi * i / 720.0 for i in range(721)]
    for radius, label in ((C_MIN_REACH_M, "min/max reach"), (C_MAX_REACH_M, None)):
        ax.plot(
            [radius * math.sin(t) for t in theta],
            [radius * math.cos(t) for t in theta],
            color="#bbbbbb",
            linewidth=0.9,
            linestyle="--",
            label=label,
        )

    draw_line, = ax.plot(
        [],
        [],
        color="#d95f02",
        linewidth=DRAW_LINE_WIDTH,
        alpha=0.95,
        label="batter path",
    )
    travel_line, = ax.plot(
        [],
        [],
        color="#2c5aa0",
        linewidth=TRAVEL_LINE_WIDTH,
        alpha=0.95,
        label="travel path",
    )

    home = home_position_m()
    ax.scatter([home.x], [home.y], marker="*", s=160, color="#228b22",
               edgecolors="#0f3d0f", linewidths=0.8, label="home position", zorder=6)

    start = timeline[0].points[0]
    ax.scatter([start.x], [start.y], marker="o", s=40, color="#111111", label="start", zorder=5)
    ax.scatter([result.final_position_m.x], [result.final_position_m.y], marker="x", s=60,
               color="#111111", label="final", zorder=5)

    current_marker = ax.scatter(
        [start.x],
        [start.y],
        marker="o",
        s=80,
        color="#2c5aa0",
        edgecolors="#111111",
        linewidths=0.8,
        zorder=8,
    )
    status_text = ax.text(
        0.01,
        0.99,
        "",
        transform=ax.transAxes,
        ha="left",
        va="top",
        fontsize="small",
        bbox={"boxstyle": "round,pad=0.25", "facecolor": "white", "edgecolor": "#dddddd", "alpha": 0.9},
    )

    limit = C_MAX_REACH_M + 0.025
    ax.set_xlim(-limit, limit)
    ax.set_ylim(-limit, limit)
    ax.set_aspect("equal", adjustable="box")
    ax.set_xlabel("X (m)")
    ax.set_ylabel("Y (m)")
    ax.set_title(title)
    ax.grid(True, linewidth=0.5, alpha=0.35)
    ax.legend(loc="best", fontsize="small")
    fig.tight_layout()

    def update(frame: _AnimationFrame) -> tuple[Any, ...]:
        step = timeline[frame.step_index]
        point = step.points[frame.point_index]

        draw_x, draw_y = _line_data_for_frame(timeline, frame, pump_on=True)
        travel_x, travel_y = _line_data_for_frame(timeline, frame, pump_on=False)
        draw_line.set_data(draw_x, draw_y)
        travel_line.set_data(travel_x, travel_y)

        current_marker.set_offsets([[point.x, point.y]])
        if step.command == "wait":
            marker_color = "#555555"
        else:
            marker_color = "#d95f02" if step.pump_on else "#2c5aa0"
        current_marker.set_facecolor(marker_color)
        status_text.set_text(_animation_status(step, frame.elapsed_ms, total_ms))
        return draw_line, travel_line, current_marker, status_text

    output.parent.mkdir(parents=True, exist_ok=True)
    animation = FuncAnimation(
        fig,
        update,
        frames=frames,
        interval=1000.0 / fps,
        blit=False,
        repeat=False,
        cache_frame_data=False,
    )
    animation.save(output, writer=PillowWriter(fps=fps), dpi=120)
    plt.close(fig)


def _resolve_start_angles(args: argparse.Namespace) -> tuple[float, float]:
    has_angle_start = args.start_s0_deg is not None or args.start_s1_deg is not None
    has_cart_start = args.start_x_m is not None or args.start_y_m is not None
    if has_angle_start and has_cart_start:
        raise SystemExit("Use either start angles or start XY, not both")
    if has_cart_start:
        if args.start_x_m is None or args.start_y_m is None:
            raise SystemExit("--start-x-m and --start-y-m must be provided together")
        try:
            return cart_to_ang(Vec2(float(args.start_x_m), float(args.start_y_m)))
        except IntentError as exc:
            raise SystemExit(f"Invalid start XY: {exc}") from exc
    if has_angle_start:
        if args.start_s0_deg is None or args.start_s1_deg is None:
            raise SystemExit("--start-s0-deg and --start-s1-deg must be provided together")
        return float(args.start_s0_deg), float(args.start_s1_deg)
    return GO_HOME_S0_DEG, GO_HOME_S1_DEG


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="run_file text program to preview")
    parser.add_argument("--output", type=Path, help="PNG path to write")
    parser.add_argument("--gif-output", type=Path, help="animated GIF path to write")
    parser.add_argument("--gif-fps", type=float, default=20.0, help="animated GIF frames per second")
    parser.add_argument(
        "--gif-time-scale",
        type=float,
        default=1.0,
        help="multiply animated durations; use values below 1 to speed up long runs",
    )
    parser.add_argument("--no-show", action="store_true", help="save the PNG without opening a window")
    parser.add_argument("--dt-ms", type=int, default=SAMPLE_PERIOD_MS, help="intent path sampling period")
    parser.add_argument("--start-s0-deg", type=float)
    parser.add_argument("--start-s1-deg", type=float)
    parser.add_argument("--start-x-m", type=float)
    parser.add_argument("--start-y-m", type=float)
    return parser


def main() -> None:
    parser = build_arg_parser()
    args = parser.parse_args()

    start_s0_deg, start_s1_deg = _resolve_start_angles(args)
    output = args.output
    if output is None:
        output = args.path.with_name(f"{args.path.stem}_preview.png")

    try:
        result = simulate_file(
            args.path,
            dt_ms=args.dt_ms,
            start_s0_deg=start_s0_deg,
            start_s1_deg=start_s1_deg,
        )
    except IntentError as exc:
        raise SystemExit(f"Preview failed: {exc}") from exc

    plot_intent(result, output=output, show=not args.no_show, title=f"{args.path.name} intent")
    print(f"preview: {output}")
    if args.gif_output is not None:
        save_animation_gif(
            result,
            output=args.gif_output,
            fps=args.gif_fps,
            time_scale=args.gif_time_scale,
            title=f"{args.path.name} intent",
        )
        print(f"animation: {args.gif_output}")
    print(f"final: x={result.final_position_m.x:.4f} m, y={result.final_position_m.y:.4f} m")
    for event in result.events:
        print(event)


if __name__ == "__main__":
    main()
