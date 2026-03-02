#!/bin/bash
# =============================================================================
# build.sh — Docker-internal build script
# =============================================================================
# English: Runs cmake configure + build for the Linux integration test binaries.
#          Called by docker-compose service commands before running the binary.
# 한글: Linux 통합 테스트 바이너리를 cmake로 설정 및 빌드.
#       docker-compose 서비스 커맨드에서 바이너리 실행 전 호출됨.

set -euo pipefail

BUILD_DIR="/workspace/build/linux"
SOURCE_DIR="/workspace/test_linux"
JOBS=$(nproc)

echo "========================================"
echo "  NetworkModuleTest Linux Build"
echo "  Source: ${SOURCE_DIR}"
echo "  Output: ${BUILD_DIR}"
echo "  Jobs  : ${JOBS}"
echo "========================================"

# English: Configure
# 한글: 설정
cmake \
    -B "${BUILD_DIR}" \
    -S "${SOURCE_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_IO_URING=ON \
    -G Ninja

# English: Build
# 한글: 빌드
cmake --build "${BUILD_DIR}" -- -j"${JOBS}"

echo ""
echo "BUILD OK"
echo "Binaries:"
ls -lh "${BUILD_DIR}/TestServer" "${BUILD_DIR}/DBServer" "${BUILD_DIR}/TestClient/TestClient" 2>/dev/null || \
ls -lh "${BUILD_DIR}/TestServer" "${BUILD_DIR}/DBServer" "${BUILD_DIR}/TestClient" 2>/dev/null || true
echo ""
