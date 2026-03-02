#!/bin/bash
# =============================================================================
# run_integration.sh — Single-container integration test (no Docker Compose)
# =============================================================================
# English: Starts TestServer, runs TestClient (10 clients × 5 pings), checks exit code.
#          Used for quick local validation inside the container:
#            docker run --rm <image> /workspace/test_linux/scripts/run_integration.sh
# 한글: TestServer 기동 후 TestClient(10 클라이언트 × 5 핑) 실행, 종료 코드 확인.
#       컨테이너 내부 빠른 검증용:
#         docker run --rm <image> /workspace/test_linux/scripts/run_integration.sh

set -euo pipefail

BUILD_DIR="/workspace/build/linux"
SERVER_BIN="${BUILD_DIR}/TestServer"
CLIENT_BIN="${BUILD_DIR}/TestClient/TestClient"

# English: Fallback client path (in case subdirectory wasn't nested)
# 한글: 서브디렉토리가 중첩되지 않은 경우 폴백 경로
if [ ! -f "${CLIENT_BIN}" ]; then
    CLIENT_BIN="${BUILD_DIR}/TestClient"
fi

# English: Build first if binaries are missing
# 한글: 바이너리가 없으면 먼저 빌드
if [ ! -f "${SERVER_BIN}" ]; then
    echo "[run_integration] Binaries not found — running build first..."
    /workspace/test_linux/scripts/build.sh
fi

echo "========================================"
echo "  Integration Test — Single Container"
echo "========================================"

# English: Start TestServer in background
# 한글: TestServer를 백그라운드에서 시작
echo "[run_integration] Starting TestServer on port 9000..."
"${SERVER_BIN}" -p 9000 -l INFO &
SERVER_PID=$!

# English: Give the server time to bind and start accepting
# 한글: 서버가 바인딩하고 수신 시작할 시간 부여
sleep 2

# English: Verify server is running
# 한글: 서버 실행 중 확인
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "[run_integration] ERROR: TestServer failed to start"
    exit 1
fi

echo "[run_integration] TestServer running (PID=${SERVER_PID})"
echo "[run_integration] Running TestClient (10 clients × 5 pings)..."

# English: Run client test; capture exit code
# 한글: 클라이언트 테스트 실행; 종료 코드 캡처
CLIENT_EXIT=0
"${CLIENT_BIN}" \
    --host 127.0.0.1 \
    --port 9000 \
    --clients 10 \
    --pings 5 \
    || CLIENT_EXIT=$?

# English: Stop server
# 한글: 서버 종료
echo "[run_integration] Stopping TestServer..."
kill "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true

echo ""
if [ "${CLIENT_EXIT}" -eq 0 ]; then
    echo "RESULT: PASS (exit 0)"
else
    echo "RESULT: FAIL (exit ${CLIENT_EXIT})"
fi

exit "${CLIENT_EXIT}"
