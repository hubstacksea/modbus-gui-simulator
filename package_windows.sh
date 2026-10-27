#!/bin/sh
# Cross builds the Windows release with MinGW-w64 and creates a .7z archive in dist/.
#   WX_MINGW_DIR : install prefix of a static wxWidgets build for MinGW
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
    -DWX_CONFIG_OPTIONS="--static=yes" "$@"
cmake --build build-mingw -j"$(nproc)"
(cd build-mingw && cpack -G 7Z -B "$PWD/../dist")
rm -rf dist/_CPack_Packages
ls -l dist/*.7z
