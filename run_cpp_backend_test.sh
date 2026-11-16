#!/usr/bin/env bash
# =============================================================================
# Run MediaX C++ Backend Live Verification
# =============================================================================

set -e
cd "$(dirname "$0")/backend"

echo "[MediaX] Compiling and running native C++ backend verification driver..."
g++ -std=c++17 -O2 -Wall -Wextra -I. -Ibridge -Iengine \
    test_runner.cpp bridge/*.cpp engine/*.cpp \
    -o verify_backend

./verify_backend

