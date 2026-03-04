# ==============================================================================
# kill_test_procs.ps1
# 역할: 실행 중인 TestDBServer, TestServer, TestClient 프로세스를 강제 종료한다.
#       테스트 도중 프로세스가 비정상적으로 남아있을 때 수동으로 정리하는 용도.
#
# 동작:
#   - 세 프로세스 중 실행 중인 것이 있으면 목록 출력 후 강제 종료(Stop-Process -Force)
#   - 실행 중인 프로세스가 없으면 "No remaining processes found" 출력
#
# 사용법:
#   .\kill_test_procs.ps1
#   (인자 없음)
# ==============================================================================
$procs = Get-Process -Name TestDBServer,TestServer,TestClient -ErrorAction SilentlyContinue
if ($procs) {
    $procs | Select-Object Id,Name,CPU,WorkingSet | Format-Table -AutoSize
    $procs | Stop-Process -Force
    Write-Host "Killed $($procs.Count) process(es)"
} else {
    Write-Host "No remaining processes found"
}
