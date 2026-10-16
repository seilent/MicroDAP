#!/usr/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

microset="${MICROSET_SRC:?MICROSET_SRC is the MicroSET source tree}"
jobs=$(nproc)
export CCACHE_DIR="${PWD}/build/cache/ccache"

cmake -S "${microset}" -B build/microset -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build/microset
cmake --install build/microset

build/fonts.sh build/cache/fonts

cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DMICRODAP_TEST_FONT_DIR="${PWD}/build/cache/fonts"
cmake --build build/cmake
ctest --test-dir build/cmake --timeout 300 --output-on-failure -j "${jobs}" -E '^scale'
ctest --test-dir build/cmake --timeout 300 --output-on-failure -R '^scale'
cmake --install build/cmake

for test in tests/*-test.sh; do
    bash "${test}"
done
