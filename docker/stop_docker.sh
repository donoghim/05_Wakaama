#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$script_dir/.." && pwd)
cd "$project_dir"

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker command was not found." >&2
    exit 1
fi

if ! docker compose version >/dev/null 2>&1; then
    echo "Docker Compose plugin is required: docker compose" >&2
    exit 1
fi

echo "----- stop Wakaama Docker runtime"
docker compose down

echo
echo "Docker containers and network stopped."
echo "Persistent configuration and data in docker-data/ were preserved."
