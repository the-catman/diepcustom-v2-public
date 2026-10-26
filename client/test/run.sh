#!/bin/bash
set -euo pipefail

repoDir="$(cd "$(dirname "$0")/../.." && pwd)"
buildDir="$repoDir/client/test/build"

mkdir -p "$buildDir"
ninja -C "$repoDir/server/build" gamecore
clang++ -std=c++17 -O2 -I"$repoDir/server/src" \
    -I"$repoDir/server/third_party/uWebSockets/uSockets/src" \
    -I"$repoDir/server/third_party" \
    -c "$repoDir/client/test/server_driver.cpp" -o "$buildDir/server_driver.o"
clang++ -std=c++17 -O2 -I"$repoDir/client/src" \
    -c "$repoDir/client/src/sim/Simulation.cpp" -o "$buildDir/sim.o"
clang++ -std=c++17 -O2 -I"$repoDir/client/src" \
    -c "$repoDir/client/test/roundtrip.cpp" -o "$buildDir/roundtrip.o"
clang++ "$buildDir/roundtrip.o" "$buildDir/server_driver.o" "$buildDir/sim.o" \
    "$repoDir/server/build/libgamecore.a" -lz -o "$buildDir/roundtrip"

cd "$repoDir/server"
"$buildDir/roundtrip"
