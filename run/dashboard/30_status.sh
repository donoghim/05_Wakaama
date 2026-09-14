#!/usr/bin/env bash
set -euo pipefail

echo "----- service status"
systemctl --user --no-pager --full status wakaama-bootstrap.service wakaama-server.service
echo
echo "----- UDP listeners"
ss -lunp | rg ':(22101|22102)\b' || true
echo
echo "----- control socket"
if [[ -S /tmp/lwm2mserver-control.sock ]]; then
    stat -c '%F %a %n' /tmp/lwm2mserver-control.sock
else
    echo "not found: /tmp/lwm2mserver-control.sock"
fi