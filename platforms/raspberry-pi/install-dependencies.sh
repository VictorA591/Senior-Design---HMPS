#!/usr/bin/env bash
set -euo pipefail
if [[ "$(uname -s)" != Linux ]] || ! command -v dpkg >/dev/null; then
    echo "Run this script on Raspberry Pi OS Lite 64-bit, not macOS." >&2
    exit 1
fi
if [[ "$(dpkg --print-architecture)" != arm64 ]]; then
    echo "This setup targets ARM64. Install Raspberry Pi OS Lite 64-bit." >&2
    exit 1
fi
if [[ $# -gt 1 ]] || [[ $# -eq 1 && "$1" != --with-display ]]; then
    echo "Usage: bash install-dependencies.sh [--with-display]" >&2
    exit 1
fi
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
packages=()
read_packages() {
    local line
    while IFS= read -r line || [[ -n "$line" ]]; do
        [[ -z "$line" || "$line" == \#* ]] && continue
        packages+=("$line")
    done < "$1"
}
read_packages "$script_dir/packages-build.txt"
if [[ "${1:-}" == --with-display ]]; then
    read_packages "$script_dir/packages-display.txt"
fi
sudo apt-get update
# Do not use -y: apt displays package changes before installation.
sudo apt-get install "${packages[@]}"
echo "Packages installed. Follow platforms/raspberry-pi/README.md to build and run."
