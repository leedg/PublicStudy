param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$ServerPort = 19010,
    [string]$DbHost = "127.0.0.1",
    [int]$DbPort = 18002,
    [switch]$NoNewWindow,
    [switch]$DisablePortFallback
)

function Test-PortAvailable {
    param([int]$Port)

    try {
        $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
        $listener.Start()
        $listener.Stop()
        return $true
    }
    catch [System.Net.Sockets.SocketException] {
        return $false
    }
}

function Resolve-AvailablePort {
    param(
        [int]$PreferredPort,
        [int[]]$ReservedPorts = @(),
        [int]$MaxScanCount = 200
    )

    for ($offset = 0; $offset -le $MaxScanCount; $offset++) {
        $candidate = $PreferredPort + $offset
        if ($ReservedPorts -contains $candidate) {
            continue
        }

        if (Test-PortAvailable -Port $candidate) {
            return $candidate
        }
    }

    throw "No available port found near $PreferredPort"
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$serverExe = Join-Path $binDir "TestServer.exe"

if (-not (Test-Path -Path $serverExe -PathType Leaf)) {
    Write-Error "Executable not found: $serverExe"
    exit 1
}

$resolvedServerPort = $ServerPort
if (-not $DisablePortFallback) {
    $resolvedServerPort = Resolve-AvailablePort -PreferredPort $ServerPort -ReservedPorts @($DbPort)
}

if ($resolvedServerPort -ne $ServerPort) {
    Write-Warning "Server port $ServerPort is already in use. Falling back to $resolvedServerPort"
}

$serverProc = Start-Process -FilePath $serverExe `
    -ArgumentList @("-p", $resolvedServerPort, "--db-host", $DbHost, "--db-port", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow `
    -PassThru

Write-Host "Started TestServer (PID: $($serverProc.Id)) on port $resolvedServerPort (DB: $DbHost`:$DbPort)"
if ($resolvedServerPort -ne $ServerPort) {
    Write-Host "Use run_client.ps1 -ServerPort $resolvedServerPort to connect."
}