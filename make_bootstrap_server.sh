#!/usr/bin/env bash
# Build only bootstrap_server from the unified wakaama/ source tree.
#
# Usage:
#   ./make_bootstrap_server.sh          # configure (if needed) + build
#   ./make_bootstrap_server.sh -c        # force a clean reconfigure, then build

set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

reconfigure=0
for arg in "$@"; do
    case "$arg" in
    -c | --reconfigure)
        reconfigure=1
        ;;
    esac
done

if [ "$reconfigure" = "1" ] && [ -d build ]; then
    echo "----- removing existing build/ (reconfigure)"
    rm -rf build
fi

if [ ! -d build ]; then
    echo "----- configuring (cmake -S wakaama/examples -B build)"
    cmake -S wakaama/examples -B build
fi

echo "----- building: bootstrap_server"
cmake --build build --target bootstrap_server -j"$(nproc)"
