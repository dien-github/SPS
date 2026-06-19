#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LINUXAPP_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
ROOT_DIR="$(cd -- "${LINUXAPP_DIR}/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-${LINUXAPP_DIR}/build}"
RUN_HMI=1
INSIDE_DBUS=0
USE_TMUX=1
TMUX_SESSION_NAME="${SPS_TMUX_SESSION:-sps-wsl-demo}"

usage() {
    cat <<'EOF'
Usage: run_wsl_demo.sh [options]

Start SPS linuxapp services inside a temporary D-Bus session for WSL demos.

Options:
  --build-dir DIR   CMake build directory. Default: linuxapp/build
  --no-hmi          Start services only, without appHmi.
  --no-tmux         Start services in the current terminal without dashboard.
  -h, --help        Show this help.

Environment:
  SPS_CONFIG_DIR            Default: linuxapp/config/demo
  SPS_LOG_DIR               Default: ./logs
  SPS_UART_PORT             Default: /dev/ttyUSB0
  SPS_ENABLE_PC_CONTROL     Default: 0
  SPS_PC_MAC                Required only when PC control is enabled
  SPS_WOL_BROADCAST         Default: 255.255.255.255
  SPS_WOL_PORT              Default: 9
  SPS_TMUX_SESSION          Default: sps-wsl-demo

The default mode opens a tmux dashboard with 6 log panes and 1 command pane.
In the command pane:
  fake-rfid RFID001
  scenario-startup
  router-status
  stop-demo

If you only need a one-line fallback:
  qdbus --system com.sps.auth /com/sps/auth com.sps.auth.UnlockScreen RFID001
EOF
}

args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            if [[ $# -lt 2 ]]; then
                echo "error: --build-dir requires a value" >&2
                exit 2
            fi
            BUILD_DIR="$2"
            shift 2
            ;;
        --no-hmi)
            RUN_HMI=0
            shift
            ;;
        --no-tmux)
            USE_TMUX=0
            shift
            ;;
        --inside-dbus)
            INSIDE_DBUS=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            args+=("$1")
            shift
            ;;
    esac
done

if [[ "${INSIDE_DBUS}" -eq 0 ]]; then
    if ! command -v dbus-run-session >/dev/null 2>&1; then
        echo "error: dbus-run-session is required. Install dbus-x11/dbus-user-session." >&2
        exit 1
    fi
    reexec_args=(--inside-dbus --build-dir "${BUILD_DIR}")
    if [[ "${RUN_HMI}" -eq 0 ]]; then
        reexec_args+=(--no-hmi)
    fi
    if [[ "${USE_TMUX}" -eq 0 ]]; then
        reexec_args+=(--no-tmux)
    fi
    reexec_args+=("${args[@]}")
    exec dbus-run-session -- "$0" "${reexec_args[@]}"
fi

if [[ -z "${DBUS_SYSTEM_BUS_ADDRESS:-}" ]]; then
    export DBUS_SYSTEM_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}"
fi

if [[ -z "${DBUS_SYSTEM_BUS_ADDRESS:-}" ]]; then
    echo "error: DBUS_SYSTEM_BUS_ADDRESS is empty; cannot start SPS services." >&2
    exit 1
fi

export SPS_CONFIG_DIR="${SPS_CONFIG_DIR:-${LINUXAPP_DIR}/config/demo}"
export SPS_LOG_DIR="${SPS_LOG_DIR:-${ROOT_DIR}/logs}"
export SPS_UART_PORT="${SPS_UART_PORT:-/dev/ttyUSB0}"
export SPS_ENABLE_PC_CONTROL="${SPS_ENABLE_PC_CONTROL:-0}"
export SPS_WOL_BROADCAST="${SPS_WOL_BROADCAST:-255.255.255.255}"
export SPS_WOL_PORT="${SPS_WOL_PORT:-9}"
if [[ -z "${TERM:-}" || "${TERM}" == "dumb" ]]; then
    export TERM=xterm-256color
fi

mkdir -p "${SPS_LOG_DIR}"
SYSTEM_LOG="${SPS_LOG_DIR}/demo_system.log"
TMUX_HELPER_DIR="${SPS_LOG_DIR}/tmux"
TMUX_STARTED=0

required_bins=(
    svcProtocolRouter
    svcAuthentication
    svcNetworkManager
    svcOtaManager
    svcAutoEngine
)
if [[ "${RUN_HMI}" -eq 1 ]]; then
    required_bins+=(appHmi)
fi

for bin in "${required_bins[@]}"; do
    if [[ ! -x "${BUILD_DIR}/bin/${bin}" ]]; then
        echo "error: missing executable ${BUILD_DIR}/bin/${bin}" >&2
        echo "build first: cmake -S linuxapp -B ${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build ${BUILD_DIR} --parallel" >&2
        exit 1
    fi
done

pids=()

log_system() {
    local message="$1"
    printf '[%(%F %T)T] %s\n' -1 "${message}" | tee -a "${SYSTEM_LOG}"
}

cleanup() {
    trap - EXIT INT TERM
    if [[ "${TMUX_STARTED}" -eq 1 ]] && command -v tmux >/dev/null 2>&1; then
        tmux kill-session -t "${TMUX_SESSION_NAME}" 2>/dev/null || true
    fi

    local pid
    for pid in "${pids[@]:-}"; do
        if kill -0 "${pid}" 2>/dev/null; then
            kill "${pid}" 2>/dev/null || true
        fi
    done
    wait 2>/dev/null || true
}

trap cleanup EXIT INT TERM

prepare_logs() {
    local file

    : >"${SYSTEM_LOG}"

    for file in \
        protocol_router.log \
        auth_service.log \
        network_manager.log \
        ota_manager.log \
        scenario_engine.log \
        svcProtocolRouter.stdout.log \
        svcAuthentication.stdout.log \
        svcNetworkManager.stdout.log \
        svcOtaManager.stdout.log \
        svcAutoEngine.stdout.log
    do
        : >"${SPS_LOG_DIR}/${file}"
    done

    if [[ "${RUN_HMI}" -eq 1 ]]; then
        : >"${SPS_LOG_DIR}/appHmi.stdout.log"
    fi
}

start_process() {
    local name="$1"
    shift

    log_system "Starting ${name}..."
    "$@" >"${SPS_LOG_DIR}/${name}.stdout.log" 2>&1 &
    pids+=("$!")
    sleep 0.4
}

write_tmux_helpers() {
    mkdir -p "${TMUX_HELPER_DIR}"

    cat >"${TMUX_HELPER_DIR}/tail_file.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

title="$1"
file="$2"

printf '\033]2;%s\033\\' "${title}"
clear
printf '== %s ==\n%s\n\n' "${title}" "${file}"
touch "${file}"
exec tail -n 80 -F "${file}"
EOF

    cat >"${TMUX_HELPER_DIR}/tail_system.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

title="$1"
system_log="$2"
log_dir="$3"

printf '\033]2;%s\033\\' "${title}"
clear
printf '== %s ==\n%s + *.stdout.log\n\n' "${title}" "${system_log}"

touch "${system_log}"
shopt -s nullglob
files=("${system_log}" "${log_dir}"/*.stdout.log)

if [[ ${#files[@]} -eq 0 ]]; then
    echo "No system/stdout logs yet."
    exec bash -i
fi

exec tail -n 80 -F "${files[@]}"
EOF

    {
        printf 'export DBUS_SYSTEM_BUS_ADDRESS=%q\n' "${DBUS_SYSTEM_BUS_ADDRESS}"
        printf 'export SPS_CONFIG_DIR=%q\n' "${SPS_CONFIG_DIR}"
        printf 'export SPS_LOG_DIR=%q\n' "${SPS_LOG_DIR}"
        printf 'export SPS_UART_PORT=%q\n' "${SPS_UART_PORT}"
        printf 'export SPS_ENABLE_PC_CONTROL=%q\n' "${SPS_ENABLE_PC_CONTROL}"
        printf 'export SPS_WOL_BROADCAST=%q\n' "${SPS_WOL_BROADCAST}"
        printf 'export SPS_WOL_PORT=%q\n' "${SPS_WOL_PORT}"
        printf 'cd %q\n' "${ROOT_DIR}"
        printf 'fake-rfid() { python3 %q --uid "${1:-RFID001}"; }\n' "${SCRIPT_DIR}/fake_rfid_auth.py"
        printf 'scenario-startup() { qdbus --system com.sps.engine /com/sps/engine com.sps.engine.ExecuteScenario scenario-startup; }\n'
        printf 'router-status() { qdbus --system com.sps.router /com/sps/router com.sps.router.GetConnectionStatus; }\n'
        printf 'stop-demo() { tmux kill-session -t %q; }\n' "${TMUX_SESSION_NAME}"
        cat <<'EOF'
clear
cat <<'HELP'
SPS WSL demo command pane

Helpers:
  fake-rfid RFID001    Simulate one RFID swipe
  scenario-startup     Execute the startup scenario over D-Bus
  router-status        Read UART router connection status
  stop-demo            Stop tmux dashboard and demo services

Raw fallback:
  qdbus --system com.sps.auth /com/sps/auth com.sps.auth.UnlockScreen RFID001
HELP
PS1='sps-demo$ '
EOF
    } >"${TMUX_HELPER_DIR}/command_rc"

    chmod +x "${TMUX_HELPER_DIR}/tail_file.sh" "${TMUX_HELPER_DIR}/tail_system.sh"
}

tmux_add_log_pane() {
    local title="$1"
    local file="$2"
    local pane_id
    local command

    printf -v command '%q %q %q' "${TMUX_HELPER_DIR}/tail_file.sh" "${title}" "${file}"
    pane_id="$(tmux split-window -d -P -F '#{pane_id}' -t "${TMUX_SESSION_NAME}:0" \
        "${command}")"
    tmux select-pane -t "${pane_id}" -T "${title}"
    tmux select-layout -t "${TMUX_SESSION_NAME}:0" tiled >/dev/null
}

start_tmux_dashboard() {
    if ! command -v tmux >/dev/null 2>&1; then
        echo "error: tmux is required for the WSL demo dashboard. Install tmux or pass --no-tmux." >&2
        return 1
    fi

    if tmux has-session -t "${TMUX_SESSION_NAME}" 2>/dev/null; then
        TMUX_SESSION_NAME="${TMUX_SESSION_NAME}-$$"
        log_system "tmux session already existed; using ${TMUX_SESSION_NAME}"
    fi

    write_tmux_helpers

    local first_pane
    local first_command
    printf -v first_command '%q %q %q' \
        "${TMUX_HELPER_DIR}/tail_file.sh" "Protocol Router" "${SPS_LOG_DIR}/protocol_router.log"
    first_pane="$(tmux new-session -d -x 240 -y 80 -P -F '#{pane_id}' -s "${TMUX_SESSION_NAME}" -n demo \
        "${first_command}")"
    TMUX_STARTED=1
    tmux select-pane -t "${first_pane}" -T "Protocol Router"

    tmux_add_log_pane "Authentication" "${SPS_LOG_DIR}/auth_service.log"
    tmux_add_log_pane "Network Manager" "${SPS_LOG_DIR}/network_manager.log"
    tmux_add_log_pane "OTA Manager" "${SPS_LOG_DIR}/ota_manager.log"
    tmux_add_log_pane "Auto Engine" "${SPS_LOG_DIR}/scenario_engine.log"

    local system_pane
    local system_command
    printf -v system_command '%q %q %q %q' \
        "${TMUX_HELPER_DIR}/tail_system.sh" "System + stdout" "${SYSTEM_LOG}" "${SPS_LOG_DIR}"
    system_pane="$(tmux split-window -d -P -F '#{pane_id}' -t "${TMUX_SESSION_NAME}:0" \
        "${system_command}")"
    tmux select-pane -t "${system_pane}" -T "System + stdout"
    tmux select-layout -t "${TMUX_SESSION_NAME}:0" tiled >/dev/null

    local command_pane
    local command_shell
    printf -v command_shell 'bash --rcfile %q -i' "${TMUX_HELPER_DIR}/command_rc"
    command_pane="$(tmux split-window -d -P -F '#{pane_id}' -t "${TMUX_SESSION_NAME}:0" \
        "${command_shell}")"
    tmux select-pane -t "${command_pane}" -T "Commands"

    tmux set-option -t "${TMUX_SESSION_NAME}" status on >/dev/null
    tmux set-option -t "${TMUX_SESSION_NAME}" status-left " SPS WSL Demo " >/dev/null
    tmux set-window-option -t "${TMUX_SESSION_NAME}:0" pane-border-status top >/dev/null
    tmux set-window-option -t "${TMUX_SESSION_NAME}:0" pane-border-format " #{pane_index}: #{pane_title} " >/dev/null
    tmux select-layout -t "${TMUX_SESSION_NAME}:0" tiled >/dev/null
    tmux select-pane -t "${command_pane}"

    TMUX_STARTED=1
    log_system "tmux dashboard started: ${TMUX_SESSION_NAME}"

    if [[ -n "${TMUX:-}" ]]; then
        echo "tmux dashboard is ready in session: ${TMUX_SESSION_NAME}"
        echo "Attach from another terminal with: tmux attach -t ${TMUX_SESSION_NAME}"
        echo "This script will keep services running until the dashboard session exits."
        while tmux has-session -t "${TMUX_SESSION_NAME}" 2>/dev/null; do
            sleep 1
        done
    else
        tmux attach-session -t "${TMUX_SESSION_NAME}" || true
    fi
}

prepare_logs

log_system "SPS WSL demo session"
log_system "  Build dir: ${BUILD_DIR}"
log_system "  Config dir: ${SPS_CONFIG_DIR}"
log_system "  Log dir: ${SPS_LOG_DIR}"
log_system "  UART port: ${SPS_UART_PORT}"
log_system "  PC control: ${SPS_ENABLE_PC_CONTROL}"
log_system "  D-Bus address: ${DBUS_SYSTEM_BUS_ADDRESS}"

start_process svcProtocolRouter "${BUILD_DIR}/bin/svcProtocolRouter"
start_process svcAuthentication "${BUILD_DIR}/bin/svcAuthentication"
start_process svcNetworkManager "${BUILD_DIR}/bin/svcNetworkManager"
start_process svcOtaManager "${BUILD_DIR}/bin/svcOtaManager"
start_process svcAutoEngine "${BUILD_DIR}/bin/svcAutoEngine"

if [[ "${RUN_HMI}" -eq 1 ]]; then
    start_process appHmi "${BUILD_DIR}/bin/appHmi"
fi

if [[ "${USE_TMUX}" -eq 1 ]]; then
    start_tmux_dashboard
    log_system "tmux dashboard closed; stopping demo stack."
    exit 0
fi

cat <<EOF

Services are running.

Fake RFID:
  DBUS_SYSTEM_BUS_ADDRESS='${DBUS_SYSTEM_BUS_ADDRESS}' python3 ${SCRIPT_DIR}/fake_rfid_auth.py --uid RFID001

Tail logs:
  bash ${SCRIPT_DIR}/tail_sps_logs.sh

Press Ctrl+C to stop the demo stack.
EOF

wait -n "${pids[@]}"
