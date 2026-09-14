#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$script_dir/../.." && pwd)

echo "----- start local Wakaama Dashboard at http://127.0.0.1:8080"
exec env \
    WAKAAMA_BOOTSTRAP_SERVICE=wakaama-bootstrap.service \
    WAKAAMA_SERVER_SERVICE=wakaama-server.service \
    "$project_dir/dashboard/start.sh"