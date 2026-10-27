#!/bin/sh
# Cross build for Windows (x86_64) using MinGW-w64 on Linux.
#   WX_MINGW_DIR : install prefix of wxWidgets built for MinGW (contains bin/wx-config)
set -e
cd "$(dirname "$0")"
WX_MINGW_DIR=${WX_MINGW_DIR:-$HOME/wx-mingw}
if [ ! -x "$WX_MINGW_DIR/bin/wx-config" ]; then
    echo "wx-config not found in $WX_MINGW_DIR/bin. Set WX_MINGW_DIR." >&2
    exit 1
fi
cmake -S . -B build-mingw \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DwxWidgets_CONFIG_EXECUTABLE="$WX_MINGW_DIR/bin/wx-config" \
    -DWX_CONFIG_OPTIONS="--static=yes"
cmake --build build-mingw -j"$(nproc)"
echo "Built: build-mingw/modbus_gui.exe"
