#!/usr/bin/env bash

# Simple macOS (and Unix) build verification for NetworkModuleTest.
# Builds ServerEngine only with conservative options.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CONFIG="Release"
BUILD_DIR="${ROOT_DIR}/build-mac"
DO_CLEAN="OFF"
ENABLE_ASYNCIO_TESTS="OFF"

print_usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --config Debug|Release   Build config (default: Release)
  --build-dir <path>       Build directory (default: ./build-mac)
  --clean                  Remove build directory before configure
  --enable-asyncio-tests   Build ServerEngine AsyncIO test target
  -h, --help               Show this help

Notes:
  - Requires cmake 3.15+
  - macOS: install deps with Homebrew:
      brew install cmake
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            CONFIG="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --clean)
            DO_CLEAN="ON"
            shift
            ;;
        --enable-asyncio-tests)
            ENABLE_ASYNCIO_TESTS="ON"
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found."
    echo "macOS: brew install cmake"
    exit 1
fi

if [[ "${DO_CLEAN}" == "ON" ]]; then
    echo "Cleaning build dir: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

echo "Config:"
echo "  ROOT_DIR=${ROOT_DIR}"
echo "  BUILD_DIR=${BUILD_DIR}"
echo "  CONFIG=${CONFIG}"
echo "  ENABLE_ASYNCIO_TESTS=${ENABLE_ASYNCIO_TESTS}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CONFIG}" \
    -DBUILD_SERVER_ENGINE=ON \
    -DBUILD_TEST_SERVER=OFF \
    -DBUILD_DB_SERVER=OFF \
    -DENABLE_DATABASE_SUPPORT=OFF \
    -DENABLE_IO_URING=OFF \
    -DENABLE_ASYNCIO_TESTS="${ENABLE_ASYNCIO_TESTS}"

if command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
else
    JOBS=4
fi

cmake --build "${BUILD_DIR}" -j "${JOBS}"

echo "Build verification completed."
