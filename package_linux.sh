#!/bin/sh
# Builds the Linux release and creates .deb and .rpm packages in dist/.
# Requires: cmake, g++, libwxgtk3.2-dev, dpkg-dev (dpkg-shlibdeps), rpm (rpmbuild)
set -e
cd "$(dirname "$0")"
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build-linux -j"$(nproc)"
(cd build-linux && cpack -G "DEB;RPM" -B "$PWD/../dist")
rm -rf dist/_CPack_Packages
ls -l dist/*.deb dist/*.rpm
