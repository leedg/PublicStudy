# run-db-tests.ps1
#
# Usage:
#   .\scripts\run-db-tests.ps1 [-Backend sqlite|mssql|pgsql|mysql|oledb|all]
#                              [-Config Debug|Release]
#                              [-ConnStr <connection string>]
#                              [-Build] [-Rebuild]
#
# Optional environment variables:
#   DB_MSSQL_ODBC
#   DB_PGSQL_ODBC
#   DB_MYSQL_ODBC
#   DB_OLEDB

param(
    [ValidateSet('sqlite', 'mssql', 'pgsql', 'mysql', 'oledb', 'all', 'interactive')]
    [string]$Backend = 'interactive',

    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [string]$ConnStr = '',

    [switch]$Build,
    [switch]$Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$vcxproj = Join-Path $projectDir 'DBModuleTest.vcxproj'
$projectBinDir = Join-Path $projectDir "x64\$Config"
$repoBinDir = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path "x64\$Config"

function Resolve-DBModuleTestExePath {
    $candidates = @(
        (Join-Path $projectBinDir 'DBModuleTest.exe')
        (Join-Path $repoBinDir 'DBModuleTest.exe')
    )

    $existing = $candidates | Where-Object { Test-Path $_ }
    if ($existing) {
        return $existing |
            Sort-Object { (Get-Item $_).LastWriteTimeUtc } -Descending |
            Select-Object -First 1
    }

    return $candidates[0]
}

function Find-MSBuild {
    $cmd = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $vswhere = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1

    if ($vswhere) {
        $vs = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($vs) {
            $amd64 = Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe'
            if (Test-Path $amd64) {
                return $amd64
            }

            $default = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $default) {
                return $default
            }
        }
    }

    return $null
}

function Invoke-Build {
    param([switch]$Clean)

    $msbuild = Find-MSBuild
    if (-not $msbuild) {
        throw 'MSBuild.exe was not found. Install Visual Studio or Build Tools.'
    }

    $target = if ($Clean) { 'Rebuild' } else { 'Build' }

    Write-Host "[BUILD] MSBuild: $msbuild"
    & $msbuild $vcxproj /p:Configuration=$Config /p:Platform=x64 /t:$target /m /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed (exit $LASTEXITCODE)"
    }

    Write-Host "[BUILD] Output dir: $projectBinDir"
}

function Run-Backend {
    param(
        [string]$ExePath,
        [string]$BackendName,
        [string]$Label,
        [string]$ConnOverride
    )

    Write-Host "`n[$Label] Starting..."

    $args = @('--backend', $BackendName)
    if ($ConnOverride) {
        $args += @('--connstr', $ConnOverride)
    }

    $stdoutPath = Join-Path $env:TEMP "dbmoduletest_stdout_$PID.txt"
    $stderrPath = Join-Path $env:TEMP "dbmoduletest_stderr_$PID.txt"

    try {
        Remove-Item $stdoutPath, $stderrPath -ErrorAction SilentlyContinue

        $proc = Start-Process -FilePath $ExePath `
            -ArgumentList $args `
            -WorkingDirectory (Split-Path -Parent $ExePath) `
            -PassThru -Wait `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath

        if (Test-Path $stdoutPath) {
            Get-Content $stdoutPath | Write-Host
        }

        if ((Test-Path $stderrPath) -and ((Get-Item $stderrPath).Length -gt 0)) {
            Get-Content $stderrPath | Write-Host
        }

        if ($proc.ExitCode -ne 0) {
            Write-Host "[WARN] Exit code: $($proc.ExitCode)" -ForegroundColor Yellow
        }

        return ($proc.ExitCode -eq 0)
    }
    finally {
        Remove-Item $stdoutPath, $stderrPath -ErrorAction SilentlyContinue
    }
}

$exePath = Resolve-DBModuleTestExePath

Write-Host ''
Write-Host '=== DBModuleTest / Network::Database manual test runner ===' -ForegroundColor Magenta
Write-Host "Project: $projectDir"
Write-Host "Config:  $Config / x64"
Write-Host ''

if ($Build -or $Rebuild) {
    Invoke-Build -Clean:$Rebuild
    $exePath = Resolve-DBModuleTestExePath
}

if (-not (Test-Path $exePath)) {
    Write-Host "[WARN] Executable not found: $exePath" -ForegroundColor Yellow
    Write-Host '       Use -Build or build the project from Visual Studio first.' -ForegroundColor Yellow
    $answer = Read-Host 'Build now? (y/N)'
    if ($answer -match '^[yY]') {
        Invoke-Build
        $exePath = Resolve-DBModuleTestExePath
    }
    else {
        exit 1
    }
}

if ($Backend -eq 'interactive') {
    Write-Host 'Launching interactive mode.'
    Write-Host ''
    & $exePath
    exit $LASTEXITCODE
}

$map = @{
    'sqlite' = @{ Label = 'SQLite'; Env = '' }
    'mssql'  = @{ Label = 'MSSQL ODBC'; Env = 'DB_MSSQL_ODBC' }
    'pgsql'  = @{ Label = 'PostgreSQL ODBC'; Env = 'DB_PGSQL_ODBC' }
    'mysql'  = @{ Label = 'MySQL ODBC'; Env = 'DB_MYSQL_ODBC' }
    'oledb'  = @{ Label = 'OLE DB'; Env = 'DB_OLEDB' }
}

$targets = if ($Backend -eq 'all') {
    @('sqlite', 'mssql', 'pgsql', 'mysql', 'oledb')
}
else {
    @($Backend)
}

$results = @()
foreach ($target in $targets) {
    $info = $map[$target]
    $conn = ''

    if ($ConnStr) {
        $conn = $ConnStr
    }
    elseif ($info.Env) {
        $envValue = [System.Environment]::GetEnvironmentVariable($info.Env)
        if ($envValue) {
            $conn = $envValue
        }
    }

    $ok = Run-Backend -ExePath $exePath -BackendName $target -Label $info.Label -ConnOverride $conn
    $results += [PSCustomObject]@{
        Backend = $info.Label
        OK = $ok
    }
}

Write-Host ''
Write-Host '=== Final Summary ===' -ForegroundColor Magenta

$allOk = $true
foreach ($result in $results) {
    $mark = if ($result.OK) { 'PASS' } else { 'FAIL' }
    $color = if ($result.OK) { 'Green' } else { 'Red' }
    if (-not $result.OK) {
        $allOk = $false
    }

    Write-Host ("  [{0}] {1}" -f $mark, $result.Backend) -ForegroundColor $color
}

exit $(if ($allOk) { 0 } else { 1 })
