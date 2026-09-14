#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
unit_source="$script_dir/systemd/wakaama-bootstrap.service"
unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
unit_target="$unit_dir/wakaama-bootstrap.service"

mkdir -p "$unit_dir"
install -m 0644 "$unit_source" "$unit_target"
systemctl --user daemon-reload

echo "Installed $unit_target"
echo "Stop the tmux Bootstrap Server first so UDP 22101 is free."
echo "Then start the service: systemctl --user start wakaama-bootstrap.service"
echo "Start the dashboard with: WAKAAMA_BOOTSTRAP_SERVICE=wakaama-bootstrap.service ./dashboard/start.sh"