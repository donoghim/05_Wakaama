#!/usr/bin/env bash
set -euo pipefail

echo "----- stop LwM2M Server user service"
systemctl --user stop wakaama-server.service