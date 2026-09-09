#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

rm -f CMakeCache.txt
make clean
cmake ./examples/server
make