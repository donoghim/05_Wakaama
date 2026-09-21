#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$script_dir/../.." && pwd)
dashboard_host=${WAKAAMA_DASHBOARD_HOST:-0.0.0.0}
dashboard_port=${WAKAAMA_DASHBOARD_PORT:-8080}
bootstrap_port=${WAKAAMA_BOOTSTRAP_PORT:-22101}
lwm2m_port=${WAKAAMA_LWM2M_PORT:-22102}
control_socket=${WAKAAMA_CONTROL_SOCKET:-/tmp/lwm2mserver-control.sock}
dfota_dir=${WAKAAMA_DFOTA_FIRMWARE_DIR:-$project_dir/run/server/dfota_fw}

echo "----- start dashboard-managed Wakaama runtime at http://${dashboard_host}:${dashboard_port}"
echo "----- use the dashboard to start Bootstrap Server (${bootstrap_port}) and LwM2M Server (${lwm2m_port})"
echo "----- stop existing systemd/tmux servers first if they use the same UDP ports"

exec env \
    WAKAAMA_RUNTIME_MODE=managed \
    WAKAAMA_DASHBOARD_HOST="$dashboard_host" \
    WAKAAMA_DASHBOARD_PORT="$dashboard_port" \
    WAKAAMA_BOOTSTRAP_PORT="$bootstrap_port" \
    WAKAAMA_LWM2M_PORT="$lwm2m_port" \
    WAKAAMA_BOOTSTRAP_BINARY="${WAKAAMA_BOOTSTRAP_BINARY:-$project_dir/build/bootstrap_server/bootstrap_server}" \
    WAKAAMA_LWM2M_SERVER_BINARY="${WAKAAMA_LWM2M_SERVER_BINARY:-$project_dir/build/server/lwm2mserver}" \
    WAKAAMA_BOOTSTRAP_INI="${WAKAAMA_BOOTSTRAP_INI:-$project_dir/run/bootstrap_server/01_bs_plain.ini}" \
    WAKAAMA_BACKUP_DIR="${WAKAAMA_BACKUP_DIR:-$project_dir/dashboard/backups}" \
    WAKAAMA_BOOTSTRAP_LOG="${WAKAAMA_BOOTSTRAP_LOG:-$project_dir/dashboard/logs/bootstrap.log}" \
    WAKAAMA_SERVER_LOG="${WAKAAMA_SERVER_LOG:-$project_dir/dashboard/logs/server.log}" \
    WAKAAMA_CONTROL_SOCKET="$control_socket" \
    WAKAAMA_DFOTA_FIRMWARE_DIR="$dfota_dir" \
    "$project_dir/dashboard/start.sh"