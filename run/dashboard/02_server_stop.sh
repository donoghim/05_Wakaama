#!/usr/bin/env bash
set -euo pipefail

echo "----- stop Bootstrap Server and LwM2M Server user services"
systemctl --user stop wakaama-bootstrap.service wakaama-server.service