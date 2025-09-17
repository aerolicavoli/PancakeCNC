"""Interactive run file designer application.

This module provides a Tkinter based application for creating the
newline-delimited run files consumed by the Pancake CNC controller.  The
designer allows operators to place arcs, spirals, jogs and wait locations on a
rectangular griddle workspace, tweak their geometry and timing, and export a
ready-to-run text file.

The tool focuses on providing a friendly visual workflow:

* The canvas displays a metric grid representing the griddle surface.
* Buttons add new arcs, spirals, jogs, or waits to the feature list.
* Selecting a feature exposes draggable handles for reshaping it and editable
  property fields for precise adjustments.
* Between every feature the exported program inserts an automatic "Pump Off"
  jog so icing flow stops while travelling.

The resulting run file contains plain-text controller commands (one per line)
compatible with the `run_file` command described in `Pancake_esp/README.md`.
"""

from __future__ import annotations

import math
import tkinter as tk
from dataclasses import dataclass
from tkinter import filedialog, messagebox, ttk
from typing import Callable, List, Optional, Sequence, Tuple


# ---------------------------------------------------------------------------
# Coordinate utilities


WORKSPACE_WIDTH_M = 0.36
WORKSPACE_HEIGHT_M = 0.36
CANVAS_SIZE_PX = 720
CANVAS_MARGIN_PX = 40


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def distance(p0: Tuple[float, float], p1: Tuple[float, float]) -> float:
    return math.hypot(p1[0] - p0[0], p1[1] - p0[1])


def canvas_scale() -> float:
    return (CANVAS_SIZE_PX - 2 * CANVAS_MARGIN_PX) / max(WORKSPACE_WIDTH_M, WORKSPACE_HEIGHT_M)


def workspace_to_canvas(x_m: float, y_m: float) -> Tuple[float, float]:
    scale = canvas_scale()
    x = CANVAS_MARGIN_PX + (x_m + WORKSPACE_WIDTH_M / 2.0) * scale
    y = CANVAS_MARGIN_PX + (WORKSPACE_HEIGHT_M / 2.0 - y_m) * scale
    return x, y


def canvas_to_workspace(x_px: float, y_px: float) -> Tuple[float, float]:
    scale = canvas_scale()
    x_m = (x_px - CANVAS_MARGIN_PX) / scale - WORKSPACE_WIDTH_M / 2.0
    y_m = WORKSPACE_HEIGHT_M / 2.0 - (y_px - CANVAS_MARGIN_PX) / scale
    return x_m, y_m


# ---------------------------------------------------------------------------
# Generic feature infrastructure


@dataclass
class Handle:
    """Represents a draggable handle belonging to a feature."""

    name: str
    get_position: Callable[[], Tuple[float, float]]
    on_drag: Callable[[float, float], None]


@dataclass
class PropertySpec:
    name: str
    label: str
    value: float | int | bool | str
    kind: str  # "float", "int", "bool", "str"
    help_text: Optional[str] = None
    step: Optional[float] = None


class Feature:
    """Base class for drawable features."""

    display_name: str = "Feature"

    def __init__(self) -> None:
        self._cached_polyline: List[Tuple[float, float]] = []

    # -- geometry -----------------------------------------------------
    def start_point(self) -> Tuple[float, float]:
        raise NotImplementedError

    def end_point(self) -> Tuple[float, float]:
        raise NotImplementedError

    def polyline(self) -> Sequence[Tuple[float, float]]:
        """Sampled polyline representing the feature path."""

        raise NotImplementedError

    def handles(self) -> Sequence[Handle]:
        return []

    def hit_test(self, x_m: float, y_m: float, tol: float = 0.005) -> bool:
        """Return True when the point intersects the feature."""

        pts = self.polyline()
        if len(pts) < 2:
            px, py = pts[0]
            return distance((x_m, y_m), (px, py)) <= tol
        for p0, p1 in zip(pts[:-1], pts[1:]):
            if _distance_to_segment((x_m, y_m), p0, p1) <= tol:
                return True
        return False

    # -- UI -----------------------------------------------------------
    def summary(self) -> str:
        return self.display_name

    def properties(self) -> Sequence[PropertySpec]:
        return []

    def update_property(self, name: str, value: str) -> None:
        raise NotImplementedError

    def draw(self, canvas: tk.Canvas, selected: bool = False) -> List[int]:
        """Draw the feature onto the canvas and return created item IDs."""

        points = [workspace_to_canvas(x, y) for x, y in self.polyline()]
        if not points:
            return []
        color = "#c0392b" if selected else "#2c3e50"
        width = 4 if selected else 2
        ids: List[int] = []
        if len(points) == 1:
            x, y = points[0]
            ids.append(
                canvas.create_oval(x - 4, y - 4, x + 4, y + 4, outline=color, width=width)
            )
        else:
            flat: List[float] = []
            for x, y in points:
                flat.extend([x, y])
            ids.append(canvas.create_line(*flat, fill=color, width=width, smooth=True))
        return ids

    # -- export -------------------------------------------------------
    def generate_commands(
        self,
        current_pos: Tuple[float, float],
        pump_state: int,
    ) -> Tuple[List[str], Tuple[float, float], int]:
        """Return controller commands, resulting position, and pump state."""

        raise NotImplementedError


def _distance_to_segment(
    p: Tuple[float, float], a: Tuple[float, float], b: Tuple[float, float]
) -> float:
    ax, ay = a
    bx, by = b
    px, py = p
    dx = bx - ax
    dy = by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)
    t = clamp(t, 0.0, 1.0)
    proj_x = ax + t * dx
    proj_y = ay + t * dy
    return math.hypot(px - proj_x, py - proj_y)


# ---------------------------------------------------------------------------
# Feature implementations


class JogFeature(Feature):
    display_name = "Jog"

    def __init__(
        self,
        start: Tuple[float, float],
        end: Tuple[float, float],
        speed: float = 0.05,
        pump_on: bool = True,
    ) -> None:
        super().__init__()
        self.start = list(start)
        self.end = list(end)
        self.speed = speed
        self.pump_on = pump_on

    def start_point(self) -> Tuple[float, float]:
        return tuple(self.start)

    def end_point(self) -> Tuple[float, float]:
        return tuple(self.end)

    def polyline(self) -> Sequence[Tuple[float, float]]:
        return [self.start_point(), self.end_point()]

    def handles(self) -> Sequence[Handle]:
        return [
            Handle("start", lambda: self.start_point(), self._drag_start),
            Handle("end", lambda: self.end_point(), self._drag_end),
        ]

    def _drag_start(self, x: float, y: float) -> None:
        self.start[0] = x
        self.start[1] = y

    def _drag_end(self, x: float, y: float) -> None:
        self.end[0] = x
        self.end[1] = y

    def summary(self) -> str:
        dist = distance(self.start_point(), self.end_point())
        return f"Jog {dist:.3f} m @ {self.speed:.2f} m/s"

    def properties(self) -> Sequence[PropertySpec]:
        return [
            PropertySpec("start_x", "Start X (m)", self.start[0], "float"),
            PropertySpec("start_y", "Start Y (m)", self.start[1], "float"),
            PropertySpec("end_x", "End X (m)", self.end[0], "float"),
            PropertySpec("end_y", "End Y (m)", self.end[1], "float"),
            PropertySpec("speed", "Speed (m/s)", self.speed, "float"),
            PropertySpec("pump_on", "Pump On", self.pump_on, "bool"),
        ]

    def update_property(self, name: str, value: str) -> None:
        if name == "start_x":
            self.start[0] = float(value)
        elif name == "start_y":
            self.start[1] = float(value)
        elif name == "end_x":
            self.end[0] = float(value)
        elif name == "end_y":
            self.end[1] = float(value)
        elif name == "speed":
            self.speed = max(0.001, float(value))
        elif name == "pump_on":
            self.pump_on = bool(int(value))
        else:
            raise KeyError(name)

    def generate_commands(
        self, current_pos: Tuple[float, float], pump_state: int
    ) -> Tuple[List[str], Tuple[float, float], int]:
        commands = [
            format_cnc_jog(
                target=self.end_point(),
                speed=self.speed,
                pump_on=int(self.pump_on),
            )
        ]
        return commands, self.end_point(), int(self.pump_on)


class ArcFeature(Feature):
    display_name = "Arc"

    def __init__(
        self,
        center: Tuple[float, float],
        radius: float,
        start_angle: float,
        end_angle: float,
        speed: float = 0.04,
        pump_on: bool = True,
    ) -> None:
        super().__init__()
        self.center = list(center)
        self.radius = max(0.001, radius)
        self.start_angle = start_angle
        self.end_angle = end_angle
        self.speed = speed
        self.pump_on = pump_on

    def start_point(self) -> Tuple[float, float]:
        return self._point_at(self.start_angle)

    def end_point(self) -> Tuple[float, float]:
        return self._point_at(self.end_angle)

    def _point_at(self, theta: float) -> Tuple[float, float]:
        cx, cy = self.center
        return (cx + self.radius * math.cos(theta), cy + self.radius * math.sin(theta))

    def polyline(self) -> Sequence[Tuple[float, float]]:
        samples = max(16, int(abs(self.end_angle - self.start_angle) / (math.pi / 24)))
        pts = [
            self._point_at(self.start_angle + t * (self.end_angle - self.start_angle) / samples)
            for t in range(samples + 1)
        ]
        return pts

    def handles(self) -> Sequence[Handle]:
        return [
            Handle("center", lambda: tuple(self.center), self._drag_center),
            Handle("start", lambda: self.start_point(), self._drag_start),
            Handle("end", lambda: self.end_point(), self._drag_end),
        ]

    def _drag_center(self, x: float, y: float) -> None:
        self.center[0] = x
        self.center[1] = y

    def _drag_start(self, x: float, y: float) -> None:
        self._update_angle_and_radius(x, y, is_start=True)

    def _drag_end(self, x: float, y: float) -> None:
        self._update_angle_and_radius(x, y, is_start=False)

    def _update_angle_and_radius(self, x: float, y: float, *, is_start: bool) -> None:
        cx, cy = self.center
        vec_x = x - cx
        vec_y = y - cy
        new_radius = max(0.001, math.hypot(vec_x, vec_y))
        angle = math.atan2(vec_y, vec_x)
        self.radius = new_radius
        if is_start:
            self.start_angle = angle
        else:
            self.end_angle = angle

    def summary(self) -> str:
        span_deg = math.degrees(self.end_angle - self.start_angle)
        return f"Arc r={self.radius:.3f} m span={span_deg:.1f}°"

    def properties(self) -> Sequence[PropertySpec]:
        return [
            PropertySpec("center_x", "Center X (m)", self.center[0], "float"),
            PropertySpec("center_y", "Center Y (m)", self.center[1], "float"),
            PropertySpec("radius", "Radius (m)", self.radius, "float"),
            PropertySpec(
                "start_angle",
                "Start Angle (deg)",
                math.degrees(self.start_angle),
                "float",
            ),
            PropertySpec(
                "end_angle",
                "End Angle (deg)",
                math.degrees(self.end_angle),
                "float",
            ),
            PropertySpec("speed", "Speed (m/s)", self.speed, "float"),
            PropertySpec("pump_on", "Pump On", self.pump_on, "bool"),
        ]

    def update_property(self, name: str, value: str) -> None:
        if name == "center_x":
            self.center[0] = float(value)
        elif name == "center_y":
            self.center[1] = float(value)
        elif name == "radius":
            self.radius = max(0.001, float(value))
        elif name == "start_angle":
            self.start_angle = math.radians(float(value))
        elif name == "end_angle":
            self.end_angle = math.radians(float(value))
        elif name == "speed":
            self.speed = max(0.001, float(value))
        elif name == "pump_on":
            self.pump_on = bool(int(value))
        else:
            raise KeyError(name)

    def generate_commands(
        self, current_pos: Tuple[float, float], pump_state: int
    ) -> Tuple[List[str], Tuple[float, float], int]:
        commands: List[str] = []
        if self.pump_on and pump_state != 1:
            commands.append(
                format_cnc_jog(target=current_pos, speed=self.speed, pump_on=1)
            )
            pump_state = 1
        elif not self.pump_on and pump_state != 0:
            commands.append(
                format_cnc_jog(target=current_pos, speed=self.speed, pump_on=0)
            )
            pump_state = 0
        commands.append(
            "cnc_arc "
            f"StartTheta_rad={self.start_angle:.5f} "
            f"EndTheta_rad={self.end_angle:.5f} "
            f"Radius_m={self.radius:.5f} "
            f"LinearSpeed_mps={self.speed:.5f} "
            f"CenterX_m={self.center[0]:.5f} "
            f"CenterY_m={self.center[1]:.5f}"
        )
        return commands, self.end_point(), pump_state


class SpiralFeature(Feature):
    display_name = "Spiral"

    def __init__(
        self,
        center: Tuple[float, float],
        spiral_constant: float = 0.02,
        spiral_rate: float = 1.0,
        linear_speed: float = 0.05,
        max_radius: float = 0.12,
        pump_on: bool = True,
    ) -> None:
        super().__init__()
        self.center = list(center)
        self.spiral_constant = max(1e-4, spiral_constant)
        self.spiral_rate = max(1e-4, spiral_rate)
        self.linear_speed = max(1e-4, linear_speed)
        self.max_radius = max(0.001, max_radius)
        self.pump_on = pump_on

    def start_point(self) -> Tuple[float, float]:
        # Spiral starts at the center.
        return tuple(self.center)

    def end_point(self) -> Tuple[float, float]:
        theta_end = self.max_radius / self.spiral_constant
        cx, cy = self.center
        return (
            cx + math.sin(theta_end) * self.max_radius,
            cy + math.cos(theta_end) * self.max_radius,
        )

    def polyline(self) -> Sequence[Tuple[float, float]]:
        theta_end = self.max_radius / self.spiral_constant
        steps = max(60, int(theta_end / (math.pi / 36)))
        pts: List[Tuple[float, float]] = []
        cx, cy = self.center
        for i in range(steps + 1):
            frac = i / max(1, steps)
            theta = frac * theta_end
            radius = self.spiral_constant * theta
            x = cx + math.sin(theta) * radius
            y = cy + math.cos(theta) * radius
            pts.append((x, y))
        return pts

    def handles(self) -> Sequence[Handle]:
        return [
            Handle("center", lambda: tuple(self.center), self._drag_center),
            Handle("radius", self.end_point, self._drag_radius),
        ]

    def _drag_center(self, x: float, y: float) -> None:
        self.center[0] = x
        self.center[1] = y

    def _drag_radius(self, x: float, y: float) -> None:
        cx, cy = self.center
        radius = max(0.001, math.hypot(x - cx, y - cy))
        self.max_radius = radius

    def summary(self) -> str:
        turns = self.max_radius / (self.spiral_constant * 2 * math.pi)
        return f"Spiral r={self.max_radius:.3f} m turns={turns:.2f}"

    def properties(self) -> Sequence[PropertySpec]:
        return [
            PropertySpec("center_x", "Center X (m)", self.center[0], "float"),
            PropertySpec("center_y", "Center Y (m)", self.center[1], "float"),
            PropertySpec("spiral_constant", "Spiral Constant (m/rad)", self.spiral_constant, "float"),
            PropertySpec("spiral_rate", "Spiral Rate (rad/s)", self.spiral_rate, "float"),
            PropertySpec("linear_speed", "Linear Speed (m/s)", self.linear_speed, "float"),
            PropertySpec("max_radius", "Max Radius (m)", self.max_radius, "float"),
            PropertySpec("pump_on", "Pump On", self.pump_on, "bool"),
        ]

    def update_property(self, name: str, value: str) -> None:
        if name == "center_x":
            self.center[0] = float(value)
        elif name == "center_y":
            self.center[1] = float(value)
        elif name == "spiral_constant":
            self.spiral_constant = max(1e-4, float(value))
        elif name == "spiral_rate":
            self.spiral_rate = max(1e-4, float(value))
        elif name == "linear_speed":
            self.linear_speed = max(1e-4, float(value))
        elif name == "max_radius":
            self.max_radius = max(0.001, float(value))
        elif name == "pump_on":
            self.pump_on = bool(int(value))
        else:
            raise KeyError(name)

    def generate_commands(
        self, current_pos: Tuple[float, float], pump_state: int
    ) -> Tuple[List[str], Tuple[float, float], int]:
        commands: List[str] = []
        desired = 1 if self.pump_on else 0
        if pump_state != desired:
            commands.append(
                format_cnc_jog(target=current_pos, speed=self.linear_speed, pump_on=desired)
            )
            pump_state = desired
        commands.append(
            "cnc_spiral "
            f"SpiralConstant_mprad={self.spiral_constant:.5f} "
            f"SpiralRate_radps={self.spiral_rate:.5f} "
            f"LinearSpeed_mps={self.linear_speed:.5f} "
            f"CenterX_m={self.center[0]:.5f} "
            f"CenterY_m={self.center[1]:.5f} "
            f"MaxRadius_m={self.max_radius:.5f}"
        )
        return commands, self.end_point(), pump_state


class WaitFeature(Feature):
    display_name = "Wait"

    def __init__(self, location: Tuple[float, float], timeout_ms: int = 1000) -> None:
        super().__init__()
        self.location = list(location)
        self.timeout_ms = max(0, int(timeout_ms))

    def start_point(self) -> Tuple[float, float]:
        return tuple(self.location)

    def end_point(self) -> Tuple[float, float]:
        return tuple(self.location)

    def polyline(self) -> Sequence[Tuple[float, float]]:
        return [self.start_point()]

    def handles(self) -> Sequence[Handle]:
        return [Handle("wait", lambda: tuple(self.location), self._drag)]

    def _drag(self, x: float, y: float) -> None:
        self.location[0] = x
        self.location[1] = y

    def summary(self) -> str:
        return f"Wait {self.timeout_ms} ms"

    def properties(self) -> Sequence[PropertySpec]:
        return [
            PropertySpec("x", "X (m)", self.location[0], "float"),
            PropertySpec("y", "Y (m)", self.location[1], "float"),
            PropertySpec("timeout", "Timeout (ms)", self.timeout_ms, "int"),
        ]

    def update_property(self, name: str, value: str) -> None:
        if name == "x":
            self.location[0] = float(value)
        elif name == "y":
            self.location[1] = float(value)
        elif name == "timeout":
            self.timeout_ms = max(0, int(float(value)))
        else:
            raise KeyError(name)

    def draw(self, canvas: tk.Canvas, selected: bool = False) -> List[int]:
        x, y = workspace_to_canvas(*self.location)
        color = "#27ae60" if selected else "#16a085"
        size = 6
        ids = [
            canvas.create_line(x - size, y - size, x + size, y + size, fill=color, width=3),
            canvas.create_line(x - size, y + size, x + size, y - size, fill=color, width=3),
        ]
        return ids

    def generate_commands(
        self, current_pos: Tuple[float, float], pump_state: int
    ) -> Tuple[List[str], Tuple[float, float], int]:
        commands: List[str] = []
        if pump_state != 0:
            commands.append(
                format_cnc_jog(target=current_pos, speed=0.05, pump_on=0)
            )
            pump_state = 0
        commands.append(f"wait timeout_ms={self.timeout_ms}")
        return commands, self.end_point(), pump_state


# ---------------------------------------------------------------------------
# Command formatting helpers


def format_cnc_jog(
    target: Tuple[float, float],
    speed: float,
    pump_on: int,
) -> str:
    return (
        "cnc_jog "
        f"TargetX_m={target[0]:.5f} "
        f"TargetY_m={target[1]:.5f} "
        f"LinearSpeed_mps={speed:.5f} "
        f"PumpOn={pump_on}"
    )


# ---------------------------------------------------------------------------
# Tkinter user interface


class RunFileDesignerApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        root.title("Run File Designer")
        root.geometry("1200x820")

        self.features: List[Feature] = []
        self.selected_index: Optional[int] = None
        self.active_handle: Optional[Handle] = None

        self.travel_speed = tk.DoubleVar(value=0.08)
        self.home_x = tk.DoubleVar(value=-0.15)
        self.home_y = tk.DoubleVar(value=-0.15)

        self._build_ui()
        self.redraw_canvas()

    # -- UI construction --------------------------------------------
    def _build_ui(self) -> None:
        outer = ttk.Frame(self.root)
        outer.pack(fill=tk.BOTH, expand=True)

        # Left: canvas
        canvas_frame = ttk.Frame(outer)
        canvas_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.canvas = tk.Canvas(canvas_frame, width=CANVAS_SIZE_PX, height=CANVAS_SIZE_PX, bg="white")
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.canvas.bind("<ButtonPress-1>", self.on_canvas_press)
        self.canvas.bind("<B1-Motion>", self.on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_canvas_release)

        # Right: controls
        control = ttk.Frame(outer, padding=10)
        control.pack(side=tk.RIGHT, fill=tk.Y)

        button_row = ttk.Frame(control)
        button_row.pack(fill=tk.X)
        ttk.Button(button_row, text="Add Arc", command=self.add_arc).pack(fill=tk.X, pady=2)
        ttk.Button(button_row, text="Add Spiral", command=self.add_spiral).pack(fill=tk.X, pady=2)
        ttk.Button(button_row, text="Add Jog", command=self.add_jog).pack(fill=tk.X, pady=2)
        ttk.Button(button_row, text="Add Wait", command=self.add_wait).pack(fill=tk.X, pady=2)

        ttk.Separator(control, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)

        travel_frame = ttk.LabelFrame(control, text="Travel Settings")
        travel_frame.pack(fill=tk.X, pady=4)
        self._add_labeled_entry(travel_frame, "Pump-off speed (m/s)", self.travel_speed)
        self._add_labeled_entry(travel_frame, "Home X (m)", self.home_x)
        self._add_labeled_entry(travel_frame, "Home Y (m)", self.home_y)

        ttk.Separator(control, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)

        ttk.Label(control, text="Features").pack(anchor=tk.W)
        self.feature_list = tk.Listbox(control, height=12)
        self.feature_list.pack(fill=tk.BOTH, expand=True)
        self.feature_list.bind("<<ListboxSelect>>", self.on_feature_list_select)

        move_row = ttk.Frame(control)
        move_row.pack(fill=tk.X, pady=4)
        ttk.Button(move_row, text="▲", command=lambda: self.move_feature(-1)).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(move_row, text="▼", command=lambda: self.move_feature(1)).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(move_row, text="Delete", command=self.delete_selected).pack(side=tk.LEFT, fill=tk.X, expand=True)

        ttk.Separator(control, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)

        self.property_frame = ttk.LabelFrame(control, text="Properties")
        self.property_frame.pack(fill=tk.BOTH, expand=True)

        ttk.Separator(control, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)
        ttk.Button(control, text="Export Run File", command=self.export_run_file).pack(fill=tk.X)

    def _add_labeled_entry(self, parent: ttk.Frame, label: str, variable: tk.DoubleVar) -> None:
        row = ttk.Frame(parent)
        row.pack(fill=tk.X, pady=2)
        ttk.Label(row, text=label).pack(side=tk.LEFT)
        entry = ttk.Entry(row, textvariable=variable, width=10)
        entry.pack(side=tk.RIGHT)
        entry.bind("<FocusOut>", lambda _e: self.redraw_canvas())
        entry.bind("<Return>", lambda _e: self.redraw_canvas())

    # -- Feature management -----------------------------------------
    def add_feature(self, feature: Feature) -> None:
        self.features.append(feature)
        self.selected_index = len(self.features) - 1
        self.refresh_feature_list()
        self.redraw_canvas()
        self.populate_properties()

    def add_arc(self) -> None:
        feature = ArcFeature(center=(0.0, 0.0), radius=0.05, start_angle=0.0, end_angle=math.pi / 2)
        self.add_feature(feature)

    def add_spiral(self) -> None:
        feature = SpiralFeature(center=(0.0, 0.0))
        self.add_feature(feature)

    def add_jog(self) -> None:
        feature = JogFeature(start=(-0.1, -0.1), end=(0.1, -0.1))
        self.add_feature(feature)

    def add_wait(self) -> None:
        feature = WaitFeature(location=(0.0, 0.0), timeout_ms=1000)
        self.add_feature(feature)

    def refresh_feature_list(self) -> None:
        self.feature_list.delete(0, tk.END)
        for feature in self.features:
            self.feature_list.insert(tk.END, feature.summary())
        if self.selected_index is not None and 0 <= self.selected_index < len(self.features):
            self.feature_list.selection_set(self.selected_index)

    def move_feature(self, offset: int) -> None:
        if self.selected_index is None:
            return
        idx = self.selected_index
        new_idx = int(clamp(idx + offset, 0, len(self.features) - 1))
        if new_idx == idx:
            return
        self.features[idx], self.features[new_idx] = self.features[new_idx], self.features[idx]
        self.selected_index = new_idx
        self.refresh_feature_list()
        self.redraw_canvas()

    def delete_selected(self) -> None:
        if self.selected_index is None:
            return
        del self.features[self.selected_index]
        if not self.features:
            self.selected_index = None
        else:
            self.selected_index = min(self.selected_index, len(self.features) - 1)
        self.refresh_feature_list()
        self.redraw_canvas()
        self.populate_properties()

    # -- Canvas rendering -------------------------------------------
    def redraw_canvas(self) -> None:
        self.canvas.delete("all")
        self.draw_grid()
        for idx, feature in enumerate(self.features):
            selected = idx == self.selected_index
            feature.draw(self.canvas, selected=selected)
            if selected:
                for handle in feature.handles():
                    hx, hy = workspace_to_canvas(*handle.get_position())
                    size = 6
                    self.canvas.create_rectangle(
                        hx - size,
                        hy - size,
                        hx + size,
                        hy + size,
                        fill="#f1c40f",
                        outline="#e67e22",
                    )

    def draw_grid(self) -> None:
        scale = canvas_scale()
        min_x = -WORKSPACE_WIDTH_M / 2.0
        max_x = WORKSPACE_WIDTH_M / 2.0
        min_y = -WORKSPACE_HEIGHT_M / 2.0
        max_y = WORKSPACE_HEIGHT_M / 2.0

        # Workspace boundary
        x0, y0 = workspace_to_canvas(min_x, min_y)
        x1, y1 = workspace_to_canvas(max_x, max_y)
        self.canvas.create_rectangle(x0, y1, x1, y0, outline="#95a5a6", width=2)

        # Grid every 0.02 m
        spacing = 0.02
        for i in range(int(min_x / spacing), int(max_x / spacing) + 1):
            x = i * spacing
            cx, _ = workspace_to_canvas(x, 0)
            self.canvas.create_line(cx, y0, cx, y1, fill="#ecf0f1" if i % 5 else "#bdc3c7")
        for j in range(int(min_y / spacing), int(max_y / spacing) + 1):
            y = j * spacing
            _, cy = workspace_to_canvas(0, y)
            self.canvas.create_line(x0, cy, x1, cy, fill="#ecf0f1" if j % 5 else "#bdc3c7")

        # Axes
        cx, _ = workspace_to_canvas(0, 0)
        _, cy = workspace_to_canvas(0, 0)
        self.canvas.create_line(cx, y0, cx, y1, fill="#7f8c8d", dash=(4, 4))
        self.canvas.create_line(x0, cy, x1, cy, fill="#7f8c8d", dash=(4, 4))

    # -- Canvas events ----------------------------------------------
    def on_canvas_press(self, event: tk.Event) -> None:
        x_m, y_m = canvas_to_workspace(event.x, event.y)
        # Check handles on selected feature first
        if self.selected_index is not None:
            feature = self.features[self.selected_index]
            for handle in feature.handles():
                hx, hy = handle.get_position()
                if distance((x_m, y_m), (hx, hy)) <= 0.01:
                    self.active_handle = handle
                    return

        # Hit test features in reverse draw order
        for idx in reversed(range(len(self.features))):
            feature = self.features[idx]
            if feature.hit_test(x_m, y_m, tol=0.01):
                self.selected_index = idx
                self.refresh_feature_list()
                self.populate_properties()
                self.redraw_canvas()
                return

        self.selected_index = None
        self.refresh_feature_list()
        self.populate_properties()
        self.redraw_canvas()

    def on_canvas_drag(self, event: tk.Event) -> None:
        if not self.active_handle:
            return
        x_m, y_m = canvas_to_workspace(event.x, event.y)
        x_m = clamp(x_m, -WORKSPACE_WIDTH_M / 2.0, WORKSPACE_WIDTH_M / 2.0)
        y_m = clamp(y_m, -WORKSPACE_HEIGHT_M / 2.0, WORKSPACE_HEIGHT_M / 2.0)
        self.active_handle.on_drag(x_m, y_m)
        self.refresh_feature_list()
        self.redraw_canvas()
        self.populate_properties()

    def on_canvas_release(self, _event: tk.Event) -> None:
        self.active_handle = None

    # -- Feature list interaction -----------------------------------
    def on_feature_list_select(self, _event: tk.Event) -> None:
        try:
            idx = int(self.feature_list.curselection()[0])
        except IndexError:
            self.selected_index = None
        else:
            self.selected_index = idx
        self.populate_properties()
        self.redraw_canvas()

    # -- Properties panel -------------------------------------------
    def populate_properties(self) -> None:
        for child in self.property_frame.winfo_children():
            child.destroy()

        if self.selected_index is None:
            ttk.Label(self.property_frame, text="Select a feature to edit.").pack(anchor=tk.W, padx=4, pady=4)
            return

        feature = self.features[self.selected_index]
        for spec in feature.properties():
            row = ttk.Frame(self.property_frame)
            row.pack(fill=tk.X, pady=2, padx=4)
            ttk.Label(row, text=spec.label).pack(side=tk.LEFT)
            if spec.kind == "bool":
                var = tk.IntVar(value=1 if spec.value else 0)
                chk = ttk.Checkbutton(
                    row,
                    variable=var,
                    command=lambda s=spec, v=var: self._update_property_bool(feature, s.name, v),
                )
                chk.pack(side=tk.RIGHT)
            else:
                var = tk.StringVar(value=str(spec.value))
                entry = ttk.Entry(row, textvariable=var, width=12)
                entry.pack(side=tk.RIGHT)
                entry.bind(
                    "<FocusOut>",
                    lambda _e, s=spec, v=var: self._update_property_entry(feature, s.name, v.get()),
                )
                entry.bind(
                    "<Return>",
                    lambda _e, s=spec, v=var: self._update_property_entry(feature, s.name, v.get()),
                )

    def _update_property_entry(self, feature: Feature, name: str, raw: str) -> None:
        try:
            feature.update_property(name, raw)
        except Exception as exc:  # noqa: BLE001 - show error to user
            messagebox.showerror("Invalid value", f"Could not update {name}: {exc}")
        self.refresh_feature_list()
        self.redraw_canvas()

    def _update_property_bool(self, feature: Feature, name: str, var: tk.IntVar) -> None:
        feature.update_property(name, str(var.get()))
        self.refresh_feature_list()
        self.redraw_canvas()

    # -- Export ------------------------------------------------------
    def export_run_file(self) -> None:
        if not self.features:
            messagebox.showinfo("Run File", "Add at least one feature before exporting.")
            return

        path = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("Run files", "*.txt"), ("All files", "*.*")],
            title="Save run file",
        )
        if not path:
            return

        try:
            lines = self.generate_run_file_lines()
        except Exception as exc:  # noqa: BLE001 - show user error
            messagebox.showerror("Export failed", str(exc))
            return

        with open(path, "w", encoding="utf-8") as f:
            for line in lines:
                f.write(line + "\n")

        messagebox.showinfo("Run File", f"Saved {len(lines)} commands to {path}.")

    def generate_run_file_lines(self) -> List[str]:
        lines: List[str] = ["# Generated by run_file_designer"]
        current_pos = (self.home_x.get(), self.home_y.get())
        pump_state = 0

        for feature in self.features:
            start = feature.start_point()
            lines.append(
                format_cnc_jog(
                    target=start,
                    speed=max(0.001, float(self.travel_speed.get())),
                    pump_on=0,
                )
            )
            current_pos = start
            pump_state = 0

            feature_lines, current_pos, pump_state = feature.generate_commands(current_pos, pump_state)
            lines.extend(feature_lines)

        return lines


def main() -> None:
    root = tk.Tk()
    app = RunFileDesignerApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
