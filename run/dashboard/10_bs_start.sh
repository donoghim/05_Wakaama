#!/usr/bin/env bash
set -euo pipefail

echo "----- start Bootstrap Server user service"
systemctl --user start wakaama-bootstrap.service
systemctl --user is-active wakaama-bootstrap.service