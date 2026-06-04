#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build/unit-tests"
mkdir -p "$build_dir"

cxx="${CXX:-g++}"
"$cxx" \
    -std=c++17 \
    -Wall \
    -Wextra \
    -Werror \
    -I"$repo_root/Tests/support" \
    -I"$repo_root/Pancake_esp/main" \
    "$repo_root/Tests/PanMathTest.cpp" \
    "$repo_root/Pancake_esp/main/PanMath.cpp" \
    "$repo_root/Pancake_esp/main/Vector2D.cpp" \
    -o "$build_dir/panmath_test"

"$build_dir/panmath_test"
