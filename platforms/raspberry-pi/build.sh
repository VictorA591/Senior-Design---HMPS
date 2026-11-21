#!/usr/bin/env bash
set -euo pipefail
if [[ "$(uname -s)" != Linux ]] || [[ "$(uname -m)" != aarch64 ]]; then
    echo "Run this build on a 64-bit ARM Linux system, such as Raspberry Pi OS Lite 64-bit." >&2
    exit 1
fi
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/../.." && pwd)"
cmake -S "$project_root" -B "$project_root/build-pi" -G Ninja     -DCMAKE_BUILD_TYPE=Release     -DHMPS_JUCE_DIR="$project_root/JUCE"     -DHMPS_TRACKTION_DIR="$project_root/Dependencies/tracktion_engine"     -DHMPS_BUILD_VST3=ON -DTE_ADD_EXAMPLES=OFF
cmake --build "$project_root/build-pi" --target EducationalDAW --parallel 4
