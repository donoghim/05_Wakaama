#!/usr/bin/env sh
set -eu

data_dir=/data
config_dir="$data_dir/config"
config_file="$config_dir/01_bs_plain.ini"
: "${PUID:=1001}"
: "${PGID:=1001}"

mkdir -p "$config_dir" "$data_dir/backups" "$data_dir/firmware" "$data_dir/logs" "$data_dir/run"

if [ ! -f "$config_file" ]; then
    cp /app/defaults/01_bs_plain.ini "$config_file"
    echo "Copied initial bootstrap configuration: $config_file"
fi

chown -R "$PUID:$PGID" "$data_dir"
exec gosu "$PUID:$PGID" python3 /app/dashboard/app.py