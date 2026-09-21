#!/usr/bin/env sh
set -eu

data_dir=/data
config_dir="$data_dir/config"
config_file="$config_dir/01_bs_plain.ini"

mkdir -p "$config_dir" "$data_dir/backups" "$data_dir/firmware" "$data_dir/logs" "$data_dir/run"

if [ ! -f "$config_file" ]; then
    : "${PUBLIC_ENDPOINT:?PUBLIC_ENDPOINT is required to create the initial bootstrap configuration}"
    : "${BOOTSTRAP_LIFETIME:=7200}"
    : "${PUBLIC_LWM2M_PORT:=22102}"
    export PUBLIC_ENDPOINT BOOTSTRAP_LIFETIME PUBLIC_LWM2M_PORT
    envsubst '${PUBLIC_ENDPOINT} ${BOOTSTRAP_LIFETIME} ${PUBLIC_LWM2M_PORT}' \
        < /app/defaults/01_bs_plain.ini.template > "$config_file"
    echo "Created initial bootstrap configuration: $config_file"
fi

exec python3 /app/dashboard/app.py