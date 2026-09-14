#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
venv_dir="$script_dir/.venv"

if [[ ! -x "$venv_dir/bin/python" ]]; then
    if ! python3 -c 'import ensurepip' >/dev/null 2>&1; then
        echo "Python virtual-environment support is unavailable." >&2
        echo "Install the OS package, then run this script again:" >&2
        echo "  sudo apt install python3-venv" >&2
        exit 1
    fi
    python3 -m venv "$venv_dir"
fi

exec "$venv_dir/bin/python" "$script_dir/app.py"