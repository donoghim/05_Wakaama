#!/usr/bin/env bash
# Build only lwm2mclient (NoSec) from the unified wakaama/ source tree.
#
# For the DTLS-enabled variant (requires autoconf/automake/libtool to build
# the tinydtls dependency), build it directly instead:
#   cmake --build build --target lwm2mclient_tinydtls
#
# Usage:
#   ./make_client.sh          # configure (if needed) + build
#   ./make_client.sh -c        # force a clean reconfigure, then build

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

echo "----- building: lwm2mclient (NoSec)"
cmake --build build --target lwm2mclient -j"$(nproc)"
