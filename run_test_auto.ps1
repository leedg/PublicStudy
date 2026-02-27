param(
    [int]$RunSeconds = 5,
    [switch]$SkipStructureSyncCheck
)

$binDir = 'C:\MyGithub\PublicStudy\NetworkModuleTest\x64\Debug'
$dbOut  = "$env:TEMP\dbserver_out.txt"
$srvOut = "$env:TEMP\server_out.txt"
$cliOut = "$env:TEMP\client_out.txt"

if (-not $SkipStructureSyncCheck)
{
    $syncCheckScript = Join-Path $PSScriptRoot "ModuleTest\ServerStructureSync\validate_server_structure_sync.ps1"
    if (Test-Path $syncCheckScript)
    {
        Write-Host "=== Server Structure Sync Check ===" -ForegroundColor Cyan
        & $syncCheckScript
        if (-not $?)
        {
            throw "Server structure sync check failed. Fix docs/comments/tests before runtime test."
        }
    }
    else
    {
        Write-Host "[WARN] Sync check script not found: $syncCheckScript" -ForegroundColor Yellow
    }
}

# English: Helper — signal a Named Event for graceful shutdown, then wait with Kill fallback.
# 한글: Named Event로 정상 종료 신호 → 대기 → 타임아웃 시 Kill 폴백.
function Stop-Gracefully {
    param(
        [System.Diagnostics.Process]$Proc,
        [string]$EventName,
        [int]$TimeoutMs = 5000
    )
    if (-not $Proc -or $Proc.HasExited) { return }

    $sig = [IntPtr]::Zero
    try {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class Win32Event {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenEvent(uint dwDesiredAccess, bool bInheritHandle, string lpName);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool SetEvent(IntPtr hEvent);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr hObject);
}
"@ -ErrorAction SilentlyContinue

        $sig = [Win32Event]::OpenEvent(0x0002, $false, $EventName)
        if ($sig -ne [IntPtr]::Zero) {
            [Win32Event]::SetEvent($sig) | Out-Null
            $Proc.WaitForExit($TimeoutMs) | Out-Null
        }
    } catch {}
    finally {
        if ($sig -ne [IntPtr]::Zero) { [Win32Event]::CloseHandle($sig) | Out-Null }
    }

    if (-not $Proc.HasExited) { try { $Proc.Kill() } catch {} }
}

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

# English: Graceful shutdown via Named Events (Kill fallback on timeout)
# 한글: Named Event로 정상 종료 (타임아웃 시 Kill 폴백)
Stop-Gracefully -Proc $clientProc -EventName "TestClient_GracefulShutdown_$($clientProc.Id)" -TimeoutMs 4000
Stop-Gracefully -Proc $serverProc -EventName "TestServer_GracefulShutdown"   -TimeoutMs 6000
Stop-Gracefully -Proc $dbProc     -EventName "TestDBServer_GracefulShutdown" -TimeoutMs 6000
Start-Sleep -Milliseconds 500

Write-Host "=== DBServer Output ===" -ForegroundColor Cyan
Get-Content $dbOut  -ErrorAction SilentlyContinue
Write-Host "=== Server Output ===" -ForegroundColor Cyan
Get-Content $srvOut -ErrorAction SilentlyContinue
Write-Host "=== Client Output ===" -ForegroundColor Cyan
Get-Content $cliOut -ErrorAction SilentlyContinue
