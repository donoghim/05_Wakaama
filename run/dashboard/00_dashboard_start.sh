#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$script_dir/../.." && pwd)
dashboard_host=${WAKAAMA_DASHBOARD_HOST:-0.0.0.0}
dashboard_port=${WAKAAMA_DASHBOARD_PORT:-8080}
internal_url=$(hostname -I 2>/dev/null | tr ' ' '\n' | awk '/^172\./ { print $1; exit }')
display_host=${internal_url:-$dashboard_host}

echo "----- start Wakaama Dashboard at http://$display_host:$dashboard_port"
exec env \
    WAKAAMA_DASHBOARD_HOST="$dashboard_host" \
    WAKAAMA_DASHBOARD_PORT="$dashboard_port" \
    WAKAAMA_BOOTSTRAP_SERVICE=wakaama-bootstrap.service \
    WAKAAMA_SERVER_SERVICE=wakaama-server.service \
    "$project_dir/dashboard/start.sh"