# ==============================================================================
# check_logs.ps1
# 역할: run_test_2clients.ps1 또는 run_test_auto.ps1 실행 후 생성된
#       임시 로그 파일(%TEMP%)을 읽어 각 프로세스의 최근 출력을 콘솔에 표시한다.
#
# 표시 내용:
#   - TestServer 표준 출력 마지막 20줄
#   - TestDBServer 표준 출력 마지막 20줄
#   - TestClient #1 표준 출력 마지막 10줄
#   - TestClient #2 표준 출력 마지막 10줄
#
# 사용법:
#   .\check_logs.ps1
#   (인자 없음 — 테스트 실행 후에만 의미 있음)
# ==============================================================================
Write-Host "=== Server last output ===" -ForegroundColor Cyan
Get-Content "$env:TEMP\server_out.txt" -ErrorAction SilentlyContinue | Select-Object -Last 20
Write-Host "=== DBServer last output ===" -ForegroundColor Cyan
Get-Content "$env:TEMP\dbserver_out.txt" -ErrorAction SilentlyContinue | Select-Object -Last 20
Write-Host "=== Client1 last output ===" -ForegroundColor Green
Get-Content "$env:TEMP\client1_out.txt" -ErrorAction SilentlyContinue | Select-Object -Last 10
Write-Host "=== Client2 last output ===" -ForegroundColor Green
Get-Content "$env:TEMP\client2_out.txt" -ErrorAction SilentlyContinue | Select-Object -Last 10
