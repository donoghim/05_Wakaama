#!/usr/bin/env bash
set -euo pipefail

echo "----- restart LwM2M Server user service"
systemctl --user restart wakaama-server.service
systemctl --user is-active wakaama-server.service