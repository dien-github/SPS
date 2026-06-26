#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
LOG_DIR="${SPS_LOG_DIR:-${ROOT_DIR}/logs}"

if [[ ! -d "${LOG_DIR}" ]]; then
    echo "error: log directory does not exist: ${LOG_DIR}" >&2
    echo "start the demo stack first, or set SPS_LOG_DIR." >&2
    exit 1
fi

shopt -s nullglob
logs=("${LOG_DIR}"/*.log)
if [[ ${#logs[@]} -eq 0 ]]; then
    echo "error: no .log files found in ${LOG_DIR}" >&2
    exit 1
fi

tail -n 80 -f "${logs[@]}"
