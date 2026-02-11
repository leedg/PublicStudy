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
	[string]$TargetHost = "127.0.0.1",
	# 한글: 동일 콘솔에서 실행할지 여부
	[switch]$NoNewWindow
)

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$OutputEncoding = $utf8NoBom
[Console]::OutputEncoding = $utf8NoBom
[Console]::InputEncoding = $utf8NoBom
try { chcp 65001 > $null } catch {}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"

if ([string]::IsNullOrWhiteSpace($TargetHost)) {
	$TargetHost = "127.0.0.1"
}

$testDbServer = Join-Path $binDir "TestDBServer.exe"
$testServer = Join-Path $binDir "TestServer.exe"
$testClient = Join-Path $binDir "TestClient.exe"

function Assert-Exe([string]$path) {
	if (-not (Test-Path -Path $path -PathType Leaf)) {
		Write-Error "Executable not found: $path"
		exit 1
	}
}

Assert-Exe $testDbServer
Assert-Exe $testServer
Assert-Exe $testClient

Write-Host "=== Test run start ==="
Write-Host "Bin: $binDir"

try {
	# 한글: DBServer -> TestServer -> TestClient 순으로 기동한다.
	$dbProc = Start-Process -FilePath $testDbServer `
		-ArgumentList @("-p", $DbPort) `
		-WorkingDirectory $binDir `
		-PassThru -NoNewWindow:$NoNewWindow

	Start-Sleep -Milliseconds 500

	$serverProc = Start-Process -FilePath $testServer `
		-ArgumentList @("-p", $ServerPort, "--db-host", $TargetHost, "--db-port", $DbPort) `
		-WorkingDirectory $binDir `
		-PassThru -NoNewWindow:$NoNewWindow

	Start-Sleep -Milliseconds 500

	$clientProc = Start-Process -FilePath $testClient `
		-ArgumentList @("--host", $TargetHost, "--port", $ServerPort) `
		-WorkingDirectory $binDir `
		-PassThru -NoNewWindow:$NoNewWindow

	Write-Host ""
	Write-Host "Running... Press Enter to stop."
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

Write-Host "=== Test run end ==="
