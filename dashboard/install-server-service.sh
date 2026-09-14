#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
unit_source="$script_dir/systemd/wakaama-server.service"
unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
unit_target="$unit_dir/wakaama-server.service"

mkdir -p "$unit_dir"
install -m 0644 "$unit_source" "$unit_target"
systemctl --user daemon-reload

echo "Installed $unit_target"
echo "Stop the tmux LwM2M Server first so UDP 22102 is free."
echo "Then start the service: systemctl --user start wakaama-server.service"
echo "Start the dashboard with: WAKAAMA_SERVER_SERVICE=wakaama-server.service ./dashboard/start.sh"