#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"

target=${TARGET:-}
target_dir=${TARGET_DIR:-}
image_archive=${IMAGE_ARCHIVE:-wakaama-dashboard-local.tar.gz}
control_path=${SSH_CONTROL_PATH:-/tmp/wakaama-deploy-%r@%h:%p}
ssh_options=(
    -o ControlMaster=auto
    -o ControlPersist=60
    -o ControlPath="$control_path"
)

for argument in "$@"; do
    case "$argument" in
        TARGET=*) target=${argument#TARGET=} ;;
        TARGET_DIR=*) target_dir=${argument#TARGET_DIR=} ;;
        IMAGE_ARCHIVE=*) image_archive=${argument#IMAGE_ARCHIVE=} ;;
        *)
            if [[ -z "$target" ]]; then
                target=$argument
            elif [[ -z "$target_dir" ]]; then
                target_dir=$argument
            else
                echo "Unexpected argument: $argument" >&2
                exit 1
            fi
            ;;
    esac
done

target_dir=${target_dir:-~/wakaama-deploy}

if [[ -z "$target" ]]; then
    echo "Usage: TARGET=user@target-server [TARGET_DIR=~/work] ./docker_deploy.sh" >&2
    echo "Example: TARGET=quectel@localhost TARGET_DIR=~/wakaama-test2 ./docker_deploy.sh " >&2
    echo "   or: ./docker_deploy.sh TARGET=quectel@localhost TARGET_DIR=~/wakaama-test3" >&2
    echo "   or: ./docker_deploy.sh quectel@localhost ~/wakaama-test3" >&2
    exit 1
fi

if [[ ! "$target_dir" =~ ^(~|/)?[A-Za-z0-9._/-]+$ ]]; then
    echo "TARGET_DIR contains unsupported characters: $target_dir" >&2
    exit 1
fi

required_files=("$image_archive" .env compose.yaml start_docker.sh stop_docker.sh show_api_status.sh)
for file in "${required_files[@]}"; do
    if [[ ! -f "$file" ]]; then
        echo "Required file not found: $file" >&2
        exit 1
    fi
done

if [[ ! -d docker-data ]]; then
    echo "Required directory not found: docker-data" >&2
    exit 1
fi

if [[ -d docker-data/run ]]; then
    find docker-data/run -mindepth 1 -exec rm -rf -- {} +
fi

ssh "${ssh_options[@]}" "$target" "mkdir -p $target_dir"

scp "${ssh_options[@]}" "$image_archive" "$target:$target_dir/"
scp "${ssh_options[@]}" .env compose.yaml start_docker.sh stop_docker.sh show_api_status.sh "$target:$target_dir/"
rsync -av -e "ssh -o ControlMaster=auto -o ControlPersist=60 -o ControlPath=$control_path" \
    docker-data/ "$target:$target_dir/docker-data/"

echo "Deployed Docker runtime files to $target:$target_dir"