param(
	# 한글: 빌드 구성(Debug/Release)
	[string]$Configuration = "Debug",
	# 한글: 플랫폼(x64/Win32)
	[string]$Platform = "x64",
	# 한글: TestServer 포트
	[int]$ServerPort = 9000,
	# 한글: TestDBServer 포트
	[int]$DbPort = 8002,
	# 한글: TestClient 접속 대상
	[string]$Host = "127.0.0.1",
	# 한글: 동일 콘솔에서 실행할지 여부
	[switch]$NoNewWindow
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"

$testDbServer = Join-Path $binDir "TestDBServer.exe"
$testServer = Join-Path $binDir "TestServer.exe"
$testClient = Join-Path $binDir "TestClient.exe"

function Assert-Exe([string]$path) {
	if (-not (Test-Path -Path $path -PathType Leaf)) {
		Write-Error "실행 파일을 찾을 수 없습니다: $path"
		exit 1
	}
}

Assert-Exe $testDbServer
Assert-Exe $testServer
Assert-Exe $testClient

Write-Host "=== 테스트 실행 시작 ==="
Write-Host "Bin: $binDir"

try {
	# 한글: DBServer -> TestServer -> TestClient 순으로 기동한다.
	$dbProc = Start-Process -FilePath $testDbServer `
		-ArgumentList @("-p", $DbPort) `
		-WorkingDirectory $binDir `
		-PassThru -NoNewWindow:$NoNewWindow

	Start-Sleep -Milliseconds 500

	$serverProc = Start-Process -FilePath $testServer `
		-ArgumentList @("-p", $ServerPort) `
		-WorkingDirectory $binDir `
		-PassThru -NoNewWindow:$NoNewWindow

	Start-Sleep -Milliseconds 500

	$clientProc = Start-Process -FilePath $testClient `
		-ArgumentList @("--host", $Host, "--port", $ServerPort) `
		-WorkingDirectory $binDir `
		-PassThru -NoNewWindow:$NoNewWindow

	Write-Host ""
	Write-Host "실행 중... 종료하려면 Enter를 누르세요."
	[void][System.Console]::ReadLine()
}
finally {
	# 한글: 종료는 클라이언트 -> 서버 -> DBServer 순으로 시도한다.
	$procs = @($clientProc, $serverProc, $dbProc) | Where-Object { $_ }

	foreach ($p in $procs) {
		if (-not $p.HasExited) {
			try { $null = $p.CloseMainWindow() } catch {}
		}
	}

	Start-Sleep -Milliseconds 800

	foreach ($p in $procs) {
		if (-not $p.HasExited) {
			try { $p.Kill() } catch {}
		}
	}
}

Write-Host "=== 테스트 종료 ==="
