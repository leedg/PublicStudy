# _common.ps1 — Shared helpers for DB functional test scripts

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Colored output ─────────────────────────────────────────────────────────────

function Write-Info  { param([string]$msg) Write-Host "[INFO]  $msg" -ForegroundColor Cyan    }
function Write-Ok    { param([string]$msg) Write-Host "[OK]    $msg" -ForegroundColor Green   }
function Write-Warn  { param([string]$msg) Write-Host "[WARN]  $msg" -ForegroundColor Yellow  }
function Write-Err   { param([string]$msg) Write-Host "[ERROR] $msg" -ForegroundColor Red     }
function Write-Step  { param([string]$msg) Write-Host "`n=== $msg ===" -ForegroundColor Magenta }

# ── Path constants ─────────────────────────────────────────────────────────────

$script:RepoRoot    = Resolve-Path "$PSScriptRoot\..\.."          # NetworkModuleTest/
$script:ServerRoot  = Join-Path $script:RepoRoot "Server\ServerEngine"
$script:SrcFile     = Join-Path $PSScriptRoot "src\db_functional_test.cpp"
$script:BuildDir    = Join-Path $PSScriptRoot "build"

# ── Visual Studio / MSVC discovery ────────────────────────────────────────────

function Find-Vswhere {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    return $null
}

function Find-VcvarsAll {
    $vswhere = Find-Vswhere
    if (-not $vswhere) {
        throw "vswhere.exe not found — is Visual Studio installed?"
    }
    $installPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                              -property installationPath 2>$null
    if (-not $installPath) {
        throw "No Visual Studio with MSVC toolchain found"
    }
    $vcvars = Join-Path $installPath "VC\Auxiliary\Build\vcvarsall.bat"
    if (-not (Test-Path $vcvars)) {
        throw "vcvarsall.bat not found at: $vcvars"
    }
    return $vcvars
}

# Sets MSVC environment variables in the current PowerShell session by
# running vcvarsall.bat x64 in a child cmd.exe and importing its env.
function Set-VcEnv {
    if ($env:VCINSTALLDIR) {
        Write-Info "MSVC environment already active ($env:VCToolsVersion)"
        return
    }
    Write-Step "Setting up MSVC x64 environment"
    $vcvars = Find-VcvarsAll
    Write-Info "Using: $vcvars"

    $tempFile = [System.IO.Path]::GetTempFileName()
    cmd /c "`"$vcvars`" x64 > nul 2>&1 && set" | Out-File -FilePath $tempFile -Encoding ascii
    Get-Content $tempFile | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    Remove-Item $tempFile -ErrorAction SilentlyContinue
    Write-Ok "MSVC $env:VCToolsVersion (x64) ready"
}

# Returns the path to cl.exe (or throws).
function Get-ClExe {
    $cmd = Get-Command cl.exe -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "cl.exe not found — call Set-VcEnv first" }
    return $cmd.Source
}

# ── Build directory management ─────────────────────────────────────────────────

function Ensure-BuildDir {
    if (-not (Test-Path $script:BuildDir)) {
        New-Item -ItemType Directory -Path $script:BuildDir | Out-Null
    }
}

function Remove-BuildDir {
    if (-not (Test-Path $script:BuildDir)) { return }

    # Windows AV/Search may briefly hold a handle on newly created executables.
    # Retry up to 5 times (5 s total) before giving up.
    for ($i = 0; $i -lt 5; $i++) {
        try {
            Remove-Item -Recurse -Force -ErrorAction Stop $script:BuildDir
            Write-Info "Build directory removed."
            return
        } catch {
            if ($i -lt 4) { Start-Sleep -Milliseconds 1000 }
        }
    }
    Write-Warn "Build directory could not be removed (still in use): $script:BuildDir"
}

# ── Compiler include paths ─────────────────────────────────────────────────────

function Get-ServerIncludes {
    # Returns /I flags pointing at ServerEngine headers
    $base = $script:ServerRoot
    return @(
        "/I`"$base`""
        "/I`"$base\Interfaces`""
        "/I`"$base\Database`""
    )
}

# ── Docker helpers ─────────────────────────────────────────────────────────────

function Test-DockerRunning {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    docker info 2>&1 | Out-Null
    $ok = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $old
    return $ok
}

function Assert-DockerRunning {
    $dockerCmd = Get-Command docker -ErrorAction SilentlyContinue
    if (-not $dockerCmd) {
        throw "docker CLI not found — install Docker Desktop"
    }

    if (Test-DockerRunning) {
        Write-Ok "Docker daemon is running"
        return
    }

    Write-Warn "Docker daemon not running. Starting Docker Desktop..."
    $desktop = "${env:ProgramFiles}\Docker\Docker\Docker Desktop.exe"
    if (-not (Test-Path $desktop)) {
        throw "Docker Desktop not found at: $desktop"
    }
    Start-Process $desktop
    Write-Info "Waiting for Docker daemon (up to 120s)..."
    $deadline = (Get-Date).AddSeconds(120)
    do {
        Start-Sleep -Seconds 3
        if (Test-DockerRunning) {
            Write-Ok "Docker daemon is running"
            return
        }
    } while ((Get-Date) -lt $deadline)

    throw "Docker daemon did not start within 120 seconds"
}

# Waits until a container's health is 'healthy' or a TCP port responds.
function Wait-ContainerReady {
    param(
        [string]$ContainerName,
        [string]$HostName   = 'localhost',
        [int]   $Port       = 0,
        [int]   $TimeoutSec = 60
    )
    Write-Info "Waiting for $ContainerName to be ready (up to ${TimeoutSec}s)..."
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Seconds 2
        if ($Port -gt 0) {
            try {
                $tcp = New-Object System.Net.Sockets.TcpClient
                $tcp.Connect($HostName, $Port)
                $tcp.Close()
                Write-Ok "$ContainerName is accepting connections on port $Port"
                return
            } catch { <# not ready yet #> }
        } else {
            $health = docker inspect --format '{{.State.Health.Status}}' $ContainerName 2>$null
            if ($health -eq 'healthy') {
                Write-Ok "$ContainerName health: healthy"
                return
            }
        }
    } while ((Get-Date) -lt $deadline)
    throw "$ContainerName did not become ready within ${TimeoutSec}s"
}
