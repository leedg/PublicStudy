#!/bin/bash
# =============================================================================
# entrypoint_client.sh — Docker Compose client container entrypoint
# =============================================================================
# English: Builds binaries (if not already built), waits for the server to be
#          reachable, then runs TestClient against it.
#          Environment variables:
#            SERVER_HOST  — hostname of the TestServer container (default: server)
#            SERVER_PORT  — port of TestServer (default: 9000)
#            BACKEND      — io backend label for logging (epoll | iouring)
#            NUM_CLIENTS  — number of parallel clients (default: 10)
#            NUM_PINGS    — ping-pong rounds per client (default: 5)
# 한글: 바이너리 빌드 후 서버 연결 가능 여부 확인, TestClient 실행.
#       환경 변수:
#         SERVER_HOST  — TestServer 컨테이너 호스트명 (기본: server)
#         SERVER_PORT  — TestServer 포트 (기본: 9000)
#         BACKEND      — I/O 백엔드 레이블 (epoll | iouring)
#         NUM_CLIENTS  — 병렬 클라이언트 수 (기본: 10)
#         NUM_PINGS    — 클라이언트당 핑퐁 횟수 (기본: 5)

set -euo pipefail

SERVER_HOST="${SERVER_HOST:-server}"
SERVER_PORT="${SERVER_PORT:-9000}"
BACKEND="${BACKEND:-epoll}"
NUM_CLIENTS="${NUM_CLIENTS:-10}"
NUM_PINGS="${NUM_PINGS:-5}"

BUILD_DIR="/workspace/build/linux"
CLIENT_BIN="${BUILD_DIR}/TestClient/TestClient"

# English: Fallback client path
# 한글: 폴백 경로
if [ ! -f "${CLIENT_BIN}" ]; then
    CLIENT_BIN="${BUILD_DIR}/TestClient"
fi

echo "========================================"
echo "  TestClient — Backend: ${BACKEND}"
echo "  Target: ${SERVER_HOST}:${SERVER_PORT}"
echo "  Clients: ${NUM_CLIENTS}  Pings: ${NUM_PINGS}"
echo "========================================"

# English: Build if binaries are not present
# 한글: 바이너리 없으면 빌드
if [ ! -f "${CLIENT_BIN}" ]; then
    echo "[client] Binaries not found — building..."
    /workspace/test_linux/scripts/build.sh
fi

# English: Wait for server to be ready (TCP connect probe, max 30s)
# 한글: 서버 준비 대기 (TCP 연결 탐색, 최대 30초)
echo "[client] Waiting for ${SERVER_HOST}:${SERVER_PORT}..."
TIMEOUT=30
ELAPSED=0
until bash -c "echo > /dev/tcp/${SERVER_HOST}/${SERVER_PORT}" 2>/dev/null; do
    if [ "${ELAPSED}" -ge "${TIMEOUT}" ]; then
        echo "[client] ERROR: Server not reachable after ${TIMEOUT}s"
        exit 1
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done

echo "[client] Server is ready (waited ${ELAPSED}s)"

# English: Run TestClient
# 한글: TestClient 실행
echo "[client] Running TestClient..."
"${CLIENT_BIN}" \
    --host "${SERVER_HOST}" \
    --port "${SERVER_PORT}" \
    --clients "${NUM_CLIENTS}" \
    --pings "${NUM_PINGS}"

EXIT=$?

echo ""
if [ "${EXIT}" -eq 0 ]; then
    echo "[client] BACKEND=${BACKEND}  RESULT: PASS"
else
    echo "[client] BACKEND=${BACKEND}  RESULT: FAIL (exit ${EXIT})"
fi

exit "${EXIT}"
