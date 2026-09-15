#!/usr/bin/env bash
set -euo pipefail

exec journalctl --user -u wakaama-server.service -f