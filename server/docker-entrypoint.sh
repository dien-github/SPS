#!/bin/sh
set -eu

ota_dir="${OTA_DIR:-/app/ota}"
staging_dir="${UPDATE_STAGING_DIR:-${ota_dir%/}/staging}"

mkdir -p "$ota_dir" "$staging_dir"
chown nodejs:nodejs "$ota_dir" "$staging_dir"

exec su-exec nodejs:nodejs "$@"
