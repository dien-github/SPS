#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LINUXAPP_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${LINUXAPP_DIR}/build}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
RUN_VERIFY=1
RUN_BUILD=0
APT_ASSUME_YES=0

usage() {
    cat <<'EOF'
Usage: setup_dev_env.sh [options]

Install the native development environment for linuxapp.
Buildroot/image-generation dependencies are intentionally excluded.

Options:
  -y, --yes        Pass -y to apt-get install.
  --no-verify     Install packages only; skip CMake configure check.
  --build         After configure, run a build as an environment smoke test.
  --build-dir DIR Use a custom CMake build directory.
  --release       Configure CMake with CMAKE_BUILD_TYPE=Release.
  -h, --help      Show this help.

Environment:
  BUILD_DIR       Build directory, default: linuxapp/build
  BUILD_TYPE      CMake build type, default: Debug
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -y|--yes)
            APT_ASSUME_YES=1
            shift
            ;;
        --no-verify)
            RUN_VERIFY=0
            shift
            ;;
        --build)
            RUN_BUILD=1
            shift
            ;;
        --build-dir)
            if [[ $# -lt 2 ]]; then
                echo "error: --build-dir requires a value" >&2
                exit 2
            fi
            BUILD_DIR="$2"
            shift 2
            ;;
        --release)
            BUILD_TYPE=Release
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ ! -r /etc/os-release ]]; then
    echo "error: cannot detect OS because /etc/os-release is missing" >&2
    exit 1
fi

# shellcheck disable=SC1091
. /etc/os-release

if [[ "${ID:-}" != "ubuntu" && "${ID_LIKE:-}" != *"debian"* ]]; then
    echo "error: this script supports Ubuntu/Debian apt-based systems only." >&2
    echo "detected: ID=${ID:-unknown} ID_LIKE=${ID_LIKE:-unknown}" >&2
    exit 1
fi

if ! command -v sudo >/dev/null 2>&1 && [[ "${EUID}" -ne 0 ]]; then
    echo "error: sudo is required when not running as root" >&2
    exit 1
fi

SUDO=()
if [[ "${EUID}" -ne 0 ]]; then
    SUDO=(sudo)
fi

APT_INSTALL_ARGS=()
if [[ "${APT_ASSUME_YES}" -eq 1 ]]; then
    APT_INSTALL_ARGS=(-y)
fi

packages=(
    build-essential
    ca-certificates
    ccache
    cmake
    dbus-x11
    gdb
    git
    libgl1-mesa-dev
    libvulkan-dev
    libxkbcommon-dev
    ninja-build
    pkg-config
    qml6-module-qtquick
    qml6-module-qtquick-controls
    qml6-module-qtquick-layouts
    qml6-module-qtquick-window
    qt6-base-dev
    qt6-base-dev-tools
    qt6-declarative-dev
    qt6-declarative-dev-tools
    qt6-serialport-dev
)

echo "Installing linuxapp development packages..."
echo "Project: ${LINUXAPP_DIR}"
echo "OS: ${PRETTY_NAME:-${ID}}"

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install "${APT_INSTALL_ARGS[@]}" --no-install-recommends "${packages[@]}"

echo
echo "Tool versions:"
cmake --version | head -n 1
c++ --version | head -n 1
qmake6 --version | head -n 2 || true

if [[ "${RUN_VERIFY}" -eq 0 ]]; then
    echo
    echo "Package install complete. CMake verification skipped."
    exit 0
fi

echo
echo "Configuring linuxapp with CMake..."
cmake -S "${LINUXAPP_DIR}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

if [[ "${RUN_BUILD}" -eq 1 ]]; then
    echo
    echo "Building linuxapp..."
    cmake --build "${BUILD_DIR}" --parallel
fi

echo
echo "Development environment setup complete."
echo "Build directory: ${BUILD_DIR}"
if [[ "${RUN_BUILD}" -eq 0 ]]; then
    echo "To compile: cmake --build \"${BUILD_DIR}\" --parallel"
fi
