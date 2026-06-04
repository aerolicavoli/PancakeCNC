#!/usr/bin/env python3
"""Compute and plot the largest reachable inscribed rectangle."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import matplotlib.pyplot as plt


# Copied from Pancake_esp/main/PanMath.cpp
C_S0Length_m = 0.22
C_S1Length_m = 0.119
C_MAX_REACH_m = C_S0Length_m + C_S1Length_m
C_MIN_REACH_m = C_S0Length_m - C_S1Length_m


def compute_corners(inset_m: float) -> list[tuple[float, float]]:
    inner_radius_m = C_MIN_REACH_m + inset_m
    outer_radius_m = C_MAX_REACH_m - inset_m
    if outer_radius_m <= inner_radius_m:
        raise ValueError("Inset is too large for the arm reach envelope")

    top_y_m = (inner_radius_m + math.sqrt(inner_radius_m**2 + 8.0 * outer_radius_m**2)) / 4.0
    half_width_m = math.sqrt(outer_radius_m**2 - top_y_m**2)

    return [
        (-half_width_m, inner_radius_m),
        (half_width_m, inner_radius_m),
        (half_width_m, top_y_m),
        (-half_width_m, top_y_m),
        (-half_width_m, inner_radius_m),
    ]


def write_jog_program(path: Path, corners: list[tuple[float, float]], speed_mps: float) -> None:
    lines = [
        "# Largest inscribed rectangle using linear jog guidance",
        f"# S0 length: {C_S0Length_m:.6f} m",
        f"# S1 length: {C_S1Length_m:.6f} m",
        "",
        "# Move to start with pump off",
        (
            f"cnc_jog TargetX_m={corners[0][0]:.6f} TargetY_m={corners[0][1]:.6f} "
            f"LinearSpeed_mps={speed_mps:.6f} PumpOn=0"
        ),
        "",
        "# Trace rectangle",
    ]
    for x_m, y_m in corners[1:]:
        lines.append(
            f"cnc_jog TargetX_m={x_m:.6f} TargetY_m={y_m:.6f} "
            f"LinearSpeed_mps={speed_mps:.6f} PumpOn=1"
        )
    lines.append("wait timeout_ms=500")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def plot_rectangle(path: Path, corners: list[tuple[float, float]], inset_m: float) -> None:
    inner_radius_m = C_MIN_REACH_m + inset_m
    outer_radius_m = C_MAX_REACH_m - inset_m

    fig, ax = plt.subplots(figsize=(6, 6))
    theta = [math.pi * i / 240.0 for i in range(241)]
    ax.plot([outer_radius_m * math.sin(t) for t in theta],
            [outer_radius_m * math.cos(t) for t in theta], label="max reach")
    ax.plot([inner_radius_m * math.sin(t) for t in theta],
            [inner_radius_m * math.cos(t) for t in theta], label="min reach")

    xs, ys = zip(*corners)
    ax.plot(xs, ys, marker="o", label="rectangle")
    ax.set_aspect("equal", adjustable="box")
    ax.set_xlabel("X (m)")
    ax.set_ylabel("Y (m)")
    ax.grid(True)
    ax.legend()
    ax.set_title(f"Inscribed rectangle, inset={inset_m:.3f} m")
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--inset-m", type=float, default=0.01)
    parser.add_argument("--speed-mps", type=float, default=0.03)
    parser.add_argument("--plot", type=Path, default=Path("InscribedRectangle.png"))
    parser.add_argument("--gcode", type=Path, default=Path("InscribedRectangle.txt"))
    args = parser.parse_args()

    corners = compute_corners(args.inset_m)
    plot_rectangle(args.plot, corners, args.inset_m)
    write_jog_program(args.gcode, corners, args.speed_mps)

    print(f"inner radius: {C_MIN_REACH_m + args.inset_m:.6f} m")
    print(f"outer radius: {C_MAX_REACH_m - args.inset_m:.6f} m")
    for idx, (x_m, y_m) in enumerate(corners[:-1], start=1):
        print(f"corner {idx}: x={x_m:.6f} m, y={y_m:.6f} m")
    print(f"plot: {args.plot}")
    print(f"gcode: {args.gcode}")


if __name__ == "__main__":
    main()
