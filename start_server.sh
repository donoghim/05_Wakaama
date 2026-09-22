#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker command was not found." >&2
    exit 1
fi

if ! docker compose version >/dev/null 2>&1; then
    echo "Docker Compose plugin is required: docker compose" >&2
    exit 1
fi

if [[ ! -f .env ]]; then
    cp .env.example .env
    echo "Created .env from .env.example."
    echo "Set PUBLIC_ENDPOINT to the public IP address or FQDN reachable by devices."
    echo "Review .env, then run ./start_server.sh again."
    exit 0
fi

if grep -q '^PUBLIC_ENDPOINT=lwm2m\.example\.com$' .env; then
    echo "PUBLIC_ENDPOINT is still the example value in .env." >&2
    echo "Set it to the public IP address or FQDN reachable by devices." >&2
    exit 1
fi

echo "----- build and start Wakaama Docker runtime"
docker compose up -d --build

echo
echo "----- Docker runtime status"
docker compose ps
echo
echo "----- dashboard"
echo "Open the dashboard at http://<target-server-ip>:${DASHBOARD_PORT:-8080}"
echo "Use the dashboard Start buttons to start Bootstrap Server and LwM2M Server."