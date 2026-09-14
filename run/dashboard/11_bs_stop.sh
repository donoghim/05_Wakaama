#!/usr/bin/env bash
set -euo pipefail

echo "----- stop Bootstrap Server user service"
systemctl --user stop wakaama-bootstrap.service