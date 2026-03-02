#!/bin/bash
# =============================================================================
# entrypoint_client.sh — Docker Compose client container entrypoint
# =============================================================================
# English: Builds binaries, waits for server, runs TestClient, saves results.
#          Results are written to /workspace/Doc/Performance/Logs/<timestamp>/
#          which is volume-mounted from the Windows host so files persist.
# 한글: 바이너리 빌드, 서버 대기, TestClient 실행, 결과 저장.
#       결과는 /workspace/Doc/Performance/Logs/<timestamp>/에 기록되며
#       Windows 호스트와 볼륨 마운트되어 컨테이너 종료 후에도 유지됨.
#
# Environment variables:
#   SERVER_HOST  — TestServer container hostname (default: server)
#   SERVER_PORT  — TestServer port (default: 9000)
#   BACKEND      — I/O backend label (epoll | iouring)
#   NUM_CLIENTS  — number of parallel clients (default: 10)
#   NUM_PINGS    — ping-pong rounds per client (default: 5)
#   LOG_SESSION  — shared timestamp tag (set by run_docker_test.ps1 for grouping)

set -euo pipefail

SERVER_HOST="${SERVER_HOST:-server}"
SERVER_PORT="${SERVER_PORT:-9000}"
BACKEND="${BACKEND:-epoll}"
NUM_CLIENTS="${NUM_CLIENTS:-10}"
NUM_PINGS="${NUM_PINGS:-5}"

# English: LOG_SESSION groups epoll+iouring results under the same parent timestamp.
#          If not set (manual docker-compose run), generate one now.
# 한글: LOG_SESSION으로 epoll+iouring 결과를 같은 상위 타임스탬프 폴더에 묶음.
#       미설정 시(수동 실행) 현재 시간으로 생성.
LOG_SESSION="${LOG_SESSION:-$(date +%Y%m%d_%H%M%S)_docker}"

BUILD_DIR="/workspace/build/linux"
LOG_BASE="/workspace/Doc/Performance/Logs"
LOG_DIR="${LOG_BASE}/${LOG_SESSION}"
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
echo "  Log dir: ${LOG_DIR}"
echo "========================================"

# English: Build if binaries are not present
# 한글: 바이너리 없으면 빌드
if [ ! -f "${CLIENT_BIN}" ]; then
    echo "[client] Binaries not found — building..."
    /workspace/test_linux/scripts/build.sh
fi

# English: Create log directory (shared mount → writes appear on Windows host)
# 한글: 로그 디렉토리 생성 (공유 마운트 → Windows 호스트에 파일이 직접 기록됨)
mkdir -p "${LOG_DIR}"

# English: Write run metadata
# 한글: 실행 메타데이터 기록
{
    echo "date:        $(date '+%Y-%m-%d %H:%M:%S')"
    echo "backend:     ${BACKEND}"
    echo "server:      ${SERVER_HOST}:${SERVER_PORT}"
    echo "num_clients: ${NUM_CLIENTS}"
    echo "num_pings:   ${NUM_PINGS}"
    uname -r | awk '{print "kernel:      " $0}'
} > "${LOG_DIR}/meta_${BACKEND}.txt"

# English: Wait for server (TCP probe, max 30s)
# 한글: 서버 준비 대기 (TCP 탐침, 최대 30초)
echo "[client] Waiting for ${SERVER_HOST}:${SERVER_PORT}..."
TIMEOUT=30
ELAPSED=0
until bash -c "echo > /dev/tcp/${SERVER_HOST}/${SERVER_PORT}" 2>/dev/null; do
    if [ "${ELAPSED}" -ge "${TIMEOUT}" ]; then
        echo "[client] ERROR: Server not reachable after ${TIMEOUT}s"
        echo "result: FAIL (server_timeout)" >> "${LOG_DIR}/meta_${BACKEND}.txt"
        exit 1
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done

echo "[client] Server ready (waited ${ELAPSED}s)"
echo "wait_sec:    ${ELAPSED}" >> "${LOG_DIR}/meta_${BACKEND}.txt"

# English: Run TestClient — tee output to log file AND stdout
# 한글: TestClient 실행 — 출력을 로그 파일과 stdout 동시 기록
LOG_FILE="${LOG_DIR}/client_${BACKEND}.txt"

START_TS=$(date +%s)

set +e
"${CLIENT_BIN}" \
    --host "${SERVER_HOST}" \
    --port "${SERVER_PORT}" \
    --clients "${NUM_CLIENTS}" \
    --pings "${NUM_PINGS}" \
    2>&1 | tee "${LOG_FILE}"
EXIT=${PIPESTATUS[0]}
set -e

END_TS=$(date +%s)
DURATION=$((END_TS - START_TS))

# English: Append result summary to meta file
# 한글: 결과 요약을 메타 파일에 추가
{
    echo "duration_sec: ${DURATION}"
    if [ "${EXIT}" -eq 0 ]; then
        echo "result:      PASS"
    else
        echo "result:      FAIL (exit_${EXIT})"
    fi
} >> "${LOG_DIR}/meta_${BACKEND}.txt"

echo ""
if [ "${EXIT}" -eq 0 ]; then
    echo "[client] BACKEND=${BACKEND}  RESULT: PASS  (${DURATION}s)"
    echo "[client] Log: ${LOG_DIR}/client_${BACKEND}.txt"
else
    echo "[client] BACKEND=${BACKEND}  RESULT: FAIL (exit ${EXIT})  (${DURATION}s)"
fi

exit "${EXIT}"
