#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
log_dir="$script_dir/logs"

usage() {
    echo "Usage: $0 attach <bootstrap-pane> <server-pane>" >&2
    echo "       $0 detach <bootstrap-pane> <server-pane>" >&2
    echo "Example: $0 attach wakaama:0.0 wakaama:0.1" >&2
    exit 1
}

[[ $# -eq 3 ]] || usage
command -v tmux >/dev/null 2>&1 || { echo "tmux is required." >&2; exit 1; }

action=$1
bootstrap_pane=$2
server_pane=$3
mkdir -p "$log_dir"

case "$action" in
    attach)
        : > "$log_dir/bootstrap.log"
        : > "$log_dir/server.log"
        tmux pipe-pane -o -t "$bootstrap_pane" "cat >> '$log_dir/bootstrap.log'"
        tmux pipe-pane -o -t "$server_pane" "cat >> '$log_dir/server.log'"
        echo "Attached tmux logs."
        ;;
    detach)
        tmux pipe-pane -t "$bootstrap_pane"
        tmux pipe-pane -t "$server_pane"
        echo "Detached tmux logs."
        ;;
    *) usage ;;
esac