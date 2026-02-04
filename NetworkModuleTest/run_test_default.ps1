# 한글: 기본값으로 run_test.ps1 실행

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$script = Join-Path $root "run_test.ps1"

& $script
