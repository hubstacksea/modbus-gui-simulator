#!/bin/sh
# Native Linux build (requires: cmake, g++, libwxgtk3.2-dev)
set -e
cd "$(dirname "$0")"
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j"$(nproc)"
echo "Built: build-linux/modbus_gui"
