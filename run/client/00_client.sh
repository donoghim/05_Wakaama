#!/usr/bin/env bash

script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-22102}
EP=${EP:-client01}

echo "----- start example lwm2mclient (NoSec) -> ${HOST}:${PORT}, endpoint ${EP}"
../../build/client/lwm2mclient -4 -h "${HOST}" -p "${PORT}" -n "${EP}"
