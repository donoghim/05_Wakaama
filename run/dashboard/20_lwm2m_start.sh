#!/usr/bin/env bash
set -euo pipefail

echo "----- start LwM2M Server user service"
systemctl --user start wakaama-server.service
systemctl --user is-active wakaama-server.service