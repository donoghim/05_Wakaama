#!/usr/bin/env bash
set -euo pipefail

echo "----- restart Bootstrap Server user service"
systemctl --user restart wakaama-bootstrap.service
systemctl --user is-active wakaama-bootstrap.service