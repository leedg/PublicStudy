param([int]$RunSeconds = 5)

$binDir = 'E:\MyGitHub\PublicStudy\NetworkModuleTest\x64\Debug'
$dbOut   = "$env:TEMP\dbserver_out.txt"
$srvOut  = "$env:TEMP\server_out.txt"
$cliOut  = "$env:TEMP\client_out.txt"

$dbProc = Start-Process -FilePath "$binDir\TestDBServer.exe" `
    -ArgumentList '-p','8002' -WorkingDirectory $binDir -PassThru `
    -RedirectStandardOutput $dbOut -RedirectStandardError "$env:TEMP\dbserver_err.txt"
Start-Sleep -Milliseconds 1000

$serverProc = Start-Process -FilePath "$binDir\TestServer.exe" `
    -ArgumentList '-p','9000','--db-host','127.0.0.1','--db-port','8002' `
    -WorkingDirectory $binDir -PassThru `
    -RedirectStandardOutput $srvOut -RedirectStandardError "$env:TEMP\server_err.txt"
Start-Sleep -Milliseconds 1000

$clientProc = Start-Process -FilePath "$binDir\TestClient.exe" `
    -ArgumentList '--host','127.0.0.1','--port','9000' `
    -WorkingDirectory $binDir -PassThru `
    -RedirectStandardOutput $cliOut -RedirectStandardError "$env:TEMP\client_err.txt"

Start-Sleep -Seconds $RunSeconds

foreach ($p in @($clientProc, $serverProc, $dbProc)) {
    if ($p -and -not $p.HasExited) { try { $p.Kill() } catch {} }
}
Start-Sleep -Milliseconds 500

Write-Host "=== DBServer Output ===" -ForegroundColor Cyan
Get-Content $dbOut  -ErrorAction SilentlyContinue
Write-Host "=== Server Output ===" -ForegroundColor Cyan
Get-Content $srvOut -ErrorAction SilentlyContinue
Write-Host "=== Client Output ===" -ForegroundColor Cyan
Get-Content $cliOut -ErrorAction SilentlyContinue
