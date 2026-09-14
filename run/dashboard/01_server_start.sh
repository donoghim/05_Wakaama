#!/usr/bin/env bash
set -euo pipefail

echo "----- start Bootstrap Server and LwM2M Server user services"
systemctl --user start wakaama-bootstrap.service wakaama-server.service
systemctl --user is-active wakaama-bootstrap.service wakaama-server.service