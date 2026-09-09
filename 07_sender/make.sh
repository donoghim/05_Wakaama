#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)

cmake -S "$script_dir" -B "$script_dir/build"
cmake --build "$script_dir/build"