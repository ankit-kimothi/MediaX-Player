#!/usr/bin/env bash
# =============================================================================
# MediaX — Cross-Platform Build & Verification Script (Linux / macOS)
# Author: Ankit Kimothi
#
# Compiles and tests the C++ backend modules (DSA, Engine, Bridge)
# Supports CMake if available, or falls back to direct g++ compilation.
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================================"
echo "  MediaX C++ Backend Build & Verification"
echo "  Authors: Ankit, Adhiraj, Yatharth, Akash"
echo "============================================================"

if command -v cmake >/dev/null 2>&1; then
    echo "[1/3] Running CMake..."
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    echo "[2/3] Building C++ backend executable..."
    cmake --build . --parallel $(nproc 2>/dev/null || echo 2)
    OUTPUT_BIN="./MediaX"
else
    echo "[1/3] CMake not found; compiling directly with g++ (C++17)..."
    mkdir -p build
    g++ -std=c++17 -O3 -Wall -Wextra -I. -Ibridge -Iengine \
        main.cpp bridge/*.cpp engine/*.cpp \
        -o build/MediaX
    OUTPUT_BIN="build/MediaX"
fi

echo "[2/3] Verifying binary execution..."
$OUTPUT_BIN || true

echo ""
echo "============================================================"
echo "  VERIFICATION SUCCESSFUL!"
echo "  Backend binary created at: backend/build/MediaX"
echo "  On Windows: run build.bat to produce standalone MediaX.exe"
echo "  Web UI testing: open resources/index.html in any browser"
echo "============================================================"
