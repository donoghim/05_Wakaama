#!/usr/bin/env bash
set -euo pipefail

dashboard_host=${WAKAAMA_DASHBOARD_HOST:-127.0.0.1}
dashboard_port=${WAKAAMA_DASHBOARD_PORT:-8080}

curl -fsS "http://${dashboard_host}:${dashboard_port}/api/status"
echo
