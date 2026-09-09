#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

make clean
rm -f CMakeCache.txt
cmake ./examples/client
make