#!/usr/bin/env bash
set -euo pipefail

echo "----- restart Bootstrap Server and LwM2M Server user services"
systemctl --user restart wakaama-bootstrap.service wakaama-server.service
systemctl --user is-active wakaama-bootstrap.service wakaama-server.service