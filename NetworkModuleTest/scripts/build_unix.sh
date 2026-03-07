#!/usr/bin/env bash

# 한글: NetworkModuleTest (Linux/macOS) 빌드 스크립트
# 한글: 다른 서버에서도 동일한 절차로 빌드할 수 있도록 구성

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CONFIG="Debug"
BUILD_DIR="${ROOT_DIR}/build"
ENABLE_DB="OFF"
ENABLE_IO_URING="OFF"
DO_CLEAN="OFF"

print_usage() {
    cat <<EOF
사용법: $(basename "$0") [옵션]
옵션:
  --config Debug|Release   빌드 구성 (기본값: Debug)
  --build-dir <경로>       빌드 출력 디렉터리 (기본값: ./build)
  --enable-db              데이터베이스 지원 활성화
  --enable-io-uring         io_uring 백엔드 활성화 (Linux, liburing 필요)
  --clean                  빌드 디렉터리 삭제 후 재구성
  -h, --help               도움말 출력

필수 의존성:
  - cmake 3.15+
  - C++ 컴파일러 (gcc/clang)

Linux 설치 예시:
  Ubuntu/Debian:
    sudo apt-get update
    sudo apt-get install -y cmake build-essential pkg-config
  RHEL/CentOS:
    sudo yum install -y cmake gcc-c++ make pkgconfig

macOS 설치 예시:
  brew install cmake pkg-config

io_uring 활성화 시 (Linux):
  sudo apt-get install -y liburing-dev
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
        --enable-db)
            ENABLE_DB="ON"
            shift
            ;;
        --enable-io-uring)
            ENABLE_IO_URING="ON"
            shift
            ;;
        --clean)
            DO_CLEAN="ON"
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "알 수 없는 옵션: $1"
            print_usage
            exit 1
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake가 설치되어 있지 않습니다."
    echo "Ubuntu/Debian: sudo apt-get install -y cmake build-essential pkg-config"
    echo "RHEL/CentOS:   sudo yum install -y cmake gcc-c++ make pkgconfig"
    echo "macOS:         brew install cmake pkg-config"
    exit 1
fi

if [[ "${ENABLE_IO_URING}" == "ON" ]]; then
    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "pkg-config가 필요합니다 (io_uring 활성화)."
        echo "Ubuntu/Debian: sudo apt-get install -y pkg-config"
        echo "RHEL/CentOS:   sudo yum install -y pkgconfig"
        echo "macOS:         brew install pkg-config"
        exit 1
    fi
    if ! pkg-config --exists liburing; then
        echo "liburing이 설치되어 있지 않습니다."
        echo "Ubuntu/Debian: sudo apt-get install -y liburing-dev"
        exit 1
    fi
fi

if [[ "${DO_CLEAN}" == "ON" ]]; then
    echo "빌드 디렉터리 정리: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

echo "빌드 설정:"
echo "  ROOT_DIR=${ROOT_DIR}"
echo "  BUILD_DIR=${BUILD_DIR}"
echo "  CONFIG=${CONFIG}"
echo "  ENABLE_DB=${ENABLE_DB}"
echo "  ENABLE_IO_URING=${ENABLE_IO_URING}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CONFIG}" \
    -DBUILD_SERVER_ENGINE=ON \
    -DBUILD_TEST_SERVER=ON \
    -DBUILD_DB_SERVER=ON \
    -DENABLE_DATABASE_SUPPORT="${ENABLE_DB}" \
    -DENABLE_IO_URING="${ENABLE_IO_URING}"

# 한글: 코어 수에 맞춰 병렬 빌드
if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi

cmake --build "${BUILD_DIR}" -j "${JOBS}"

echo "빌드 완료"
