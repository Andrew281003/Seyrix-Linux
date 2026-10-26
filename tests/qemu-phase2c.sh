#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
export SEYRIX_QEMU_WORKFLOW=phase2c
exec "$script_dir/qemu-graphics.sh" "$@"
