#!/usr/bin/env bash
set -euo pipefail

exec journalctl --user -u wakaama-bootstrap.service -f