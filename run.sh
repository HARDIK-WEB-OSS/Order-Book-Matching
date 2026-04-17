#!/usr/bin/env bash
# run.sh — fetch deps, build, and start the server
set -e
cd "$(dirname "$0")"

echo "── Step 1: Fetch third-party headers ───────────────────────"
bash fetch_deps.sh

echo ""
echo "── Step 2: CMake configure + build ─────────────────────────"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build . -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"
cd ..

echo ""
echo "── Step 3: Start server ─────────────────────────────────────"
exec ./build/server "$@"
