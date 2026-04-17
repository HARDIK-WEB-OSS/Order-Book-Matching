#!/usr/bin/env bash
# fetch_deps.sh — downloads single-header third-party libraries
# into third_party/. Run once before cmake.

set -e
cd "$(dirname "$0")"

echo "Fetching cpp-httplib..."
mkdir -p third_party
curl -sSL \
  "https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h" \
  -o third_party/httplib.h
echo "  → third_party/httplib.h"

echo "Fetching Catch2 (single header v2)..."
mkdir -p third_party/catch2
curl -sSL \
  "https://github.com/catchorg/Catch2/releases/download/v2.13.10/catch.hpp" \
  -o third_party/catch2/catch.hpp
echo "  → third_party/catch2/catch.hpp"

echo ""
echo "Done. You can now run:"
echo "  mkdir build && cd build"
echo "  cmake .. -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build . -j\$(nproc)"
