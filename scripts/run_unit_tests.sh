#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build/unit-tests"
mkdir -p "$build_dir"

cxx="${CXX:-g++}"
common_flags=(
    -std=c++17
    -Wall
    -Wextra
    -Werror
    -I"$repo_root/Tests"
    -I"$repo_root/Tests/support"
    -I"$repo_root/Pancake_esp/main"
)

build_and_run() {
    local name="$1"
    shift
    "$cxx" "${common_flags[@]}" "$@" -o "$build_dir/$name"
    "$build_dir/$name"
}

build_and_run panmath_test \
    "$repo_root/Tests/PanMathTest.cpp" \
    "$repo_root/Pancake_esp/main/PanMath.cpp" \
    "$repo_root/Pancake_esp/main/Vector2D.cpp"

build_and_run vector2d_test \
    "$repo_root/Tests/Vector2DTest.cpp" \
    "$repo_root/Pancake_esp/main/Vector2D.cpp"

build_and_run motion_safety_test \
    "$repo_root/Tests/MotionSafetyTest.cpp" \
    "$repo_root/Pancake_esp/main/MotionSafety.cpp" \
    "$repo_root/Pancake_esp/main/Vector2D.cpp"

build_and_run archimedean_spiral_test \
    "$repo_root/Tests/ArchimedeanSpiralTest.cpp" \
    "$repo_root/Pancake_esp/main/ArchimedeanSpiral.cpp" \
    "$repo_root/Pancake_esp/main/Vector2D.cpp"

build_and_run angle_motion_test \
    "$repo_root/Tests/AngleMotionTest.cpp" \
    "$repo_root/Pancake_esp/main/AngleMotion.cpp"

build_and_run homing_controller_test \
    "$repo_root/Tests/HomingControllerTest.cpp" \
    "$repo_root/Pancake_esp/main/HomingController.cpp"

build_and_run motor_control_state_test \
    "$repo_root/Tests/MotorControlStateTest.cpp" \
    "$repo_root/Pancake_esp/main/MotionSafety.cpp" \
    "$repo_root/Pancake_esp/main/Vector2D.cpp"

build_and_run guidance_registry_test \
    "$repo_root/Tests/GuidanceRegistryTest.cpp" \
    "$repo_root/Pancake_esp/main/Vector2D.cpp"

build_and_run go_to_angle_guidance_test \
    "$repo_root/Tests/GoToAngleGuidanceTest.cpp" \
    "$repo_root/Pancake_esp/main/PanMath.cpp" \
    "$repo_root/Pancake_esp/main/Vector2D.cpp"

build_and_run influxdb_parser_test \
    "$repo_root/Tests/InfluxDBParserTest.cpp" \
    "$repo_root/Pancake_esp/main/InfluxDBParser.cpp"
