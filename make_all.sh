#!/usr/bin/env bash
# Build the core targets (bootstrap_server, lwm2mclient, lwm2mserver, lightclient)
# from the unified wakaama/ source tree, all at once.
#
# lwm2mclient_tinydtls is intentionally NOT included here since it requires
# autoconf/automake/libtool to fetch/build the tinydtls dependency. Build it
# separately if needed:
#   cmake --build build --target lwm2mclient_tinydtls
#
# Usage:
#   ./make_all.sh              # configure (if needed) + build
#   ./make_all.sh -c            # force a clean reconfigure, then build

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

echo "----- building: bootstrap_server lwm2mclient lwm2mserver lightclient"
cmake --build build --target bootstrap_server lwm2mclient lwm2mserver lightclient -j"$(nproc)"
