[CmdletBinding()]
param(
    [switch]$RequireDatabase,
    [ValidateSet("Mssql", "Mysql", "Both")]
    [string]$DbTarget = "Both"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-VsWherePath {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

function Resolve-MsBuildPath {
    $msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($msbuild) {
        return $msbuild.Source
    }

    $vswhere = Resolve-VsWherePath
    if ($vswhere) {
        $resolved = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" 2>$null |
            Select-Object -First 1
        if ($resolved) {
            return $resolved
        }
    }

    return $null
}

function Resolve-CMakePath {
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmake) {
        return $cmake.Source
    }

    $vswhere = Resolve-VsWherePath
    if ($vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null | Select-Object -First 1
        if ($installPath) {
            $candidate = Join-Path $installPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    return $null
}

function Test-TcpPort {
    param(
        [Parameter(Mandatory = $true)]
        [string]$HostName,
        [Parameter(Mandatory = $true)]
        [int]$Port,
        [int]$TimeoutMs = 2000
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne($TimeoutMs)) {
            return $false
        }
        $null = $client.EndConnect($iar)
        return $true
    }
    catch {
        return $false
    }
    finally {
        $client.Close()
    }
}

function Write-CheckResult {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [bool]$Ok,
        [Parameter(Mandatory = $true)]
        [string]$Detail
    )

    $tag = if ($Ok) { "[OK] " } else { "[FAIL]" }
    Write-Host ("{0} {1,-26} {2}" -f $tag, $Name, $Detail)
}

$isWindows = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
$requireMssql = $DbTarget -eq "Mssql" -or $DbTarget -eq "Both"
$requireMysql = $DbTarget -eq "Mysql" -or $DbTarget -eq "Both"

$vswherePath = Resolve-VsWherePath
$msbuildPath = Resolve-MsBuildPath
$cmakePath = Resolve-CMakePath

$odbcDrivers = @()
try {
    $odbcDrivers = @(Get-OdbcDriver -ErrorAction Stop)
}
catch {
    $odbcDrivers = @()
}

$mssqlOdbcDrivers = @(
    $odbcDrivers | Where-Object {
        $_.Name -in @("ODBC Driver 17 for SQL Server", "ODBC Driver 18 for SQL Server") -and
        $_.Platform -eq "64-bit"
    }
)

$mysqlOdbcDrivers = @(
    $odbcDrivers | Where-Object {
        $_.Name -like "MySQL ODBC*" -and $_.Platform -eq "64-bit"
    }
)

$oledbProviderKeys = @(
    "HKLM:\SOFTWARE\Microsoft\MSOLEDBSQL",
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\MSOLEDBSQL"
)
$existingOledbKeys = @($oledbProviderKeys | Where-Object { Test-Path -LiteralPath $_ })

$mssqlServices = @(Get-Service -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -like "MSSQL*" -or $_.Name -eq "SQLBrowser" -or $_.Name -eq "SQLSERVERAGENT"
    })
$mssqlServicesRunning = @($mssqlServices | Where-Object { $_.Status -eq "Running" })
$mssqlTcp1433 = Test-TcpPort -HostName "127.0.0.1" -Port 1433 -TimeoutMs 2000

$localDbInstances = @()
$sqlLocalDbCommand = Get-Command sqllocaldb.exe -ErrorAction SilentlyContinue
if ($sqlLocalDbCommand) {
    $localDbInstances = @(& $sqlLocalDbCommand.Source i 2>$null | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

$mysqlServices = @(Get-Service -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -like "*mysql*" -or $_.DisplayName -like "*MySQL*"
    })
$mysqlServicesRunning = @($mysqlServices | Where-Object { $_.Status -eq "Running" })
$mysqlProcesses = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -like "*mysql*" -or $_.ProcessName -like "*mysqld*"
    })
$mysqlTcp3306 = Test-TcpPort -HostName "127.0.0.1" -Port 3306 -TimeoutMs 2000

$hasBuildToolchain = $isWindows -and [bool]$msbuildPath -and [bool]$cmakePath
$hasMssqlDriver = $mssqlOdbcDrivers.Count -gt 0
$hasMysqlDriver = $mysqlOdbcDrivers.Count -gt 0
$hasOledbProvider = $existingOledbKeys.Count -gt 0

$hasMssqlEndpoint = ($mssqlServicesRunning.Count -gt 0) -or $mssqlTcp1433 -or ($localDbInstances.Count -gt 0)
$hasMysqlEndpoint = ($mysqlServicesRunning.Count -gt 0) -or ($mysqlProcesses.Count -gt 0) -or $mysqlTcp3306

$hasRequiredDrivers = $true
if ($requireMssql -and -not $hasMssqlDriver) { $hasRequiredDrivers = $false }
if ($requireMysql -and -not $hasMysqlDriver) { $hasRequiredDrivers = $false }

Write-Host ""
Write-Host ("=== DBModuleTest prerequisite check ({0}) ===" -f $DbTarget)
Write-Host ""
Write-CheckResult -Name "OS" -Ok $isWindows -Detail ($(if ($isWindows) { "Windows detected" } else { "Windows is required" }))
Write-CheckResult -Name "vswhere" -Ok ([bool]$vswherePath) -Detail ($(if ($vswherePath) { $vswherePath } else { "Not found" }))
Write-CheckResult -Name "MSBuild" -Ok ([bool]$msbuildPath) -Detail ($(if ($msbuildPath) { $msbuildPath } else { "Not found" }))
Write-CheckResult -Name "CMake" -Ok ([bool]$cmakePath) -Detail ($(if ($cmakePath) { $cmakePath } else { "Not found" }))
Write-CheckResult -Name "ODBC MSSQL driver" -Ok $hasMssqlDriver -Detail ($(if ($hasMssqlDriver) { ($mssqlOdbcDrivers | Select-Object -ExpandProperty Name -Unique) -join ", " } else { "ODBC Driver 17/18 for SQL Server (64-bit) missing" }))
Write-CheckResult -Name "ODBC MySQL driver" -Ok $hasMysqlDriver -Detail ($(if ($hasMysqlDriver) { ($mysqlOdbcDrivers | Select-Object -ExpandProperty Name -Unique) -join ", " } else { "MySQL ODBC 8.x driver (64-bit) missing" }))
Write-CheckResult -Name "OLEDB provider" -Ok $hasOledbProvider -Detail ($(if ($hasOledbProvider) { "MSOLEDBSQL registry key detected" } else { "MSOLEDBSQL provider missing (OLEDB tests may skip/fail)" }))
Write-CheckResult -Name "MSSQL endpoint" -Ok $hasMssqlEndpoint -Detail ("services={0}, tcp1433={1}, localdb={2}" -f $mssqlServicesRunning.Count, $mssqlTcp1433, $localDbInstances.Count)
Write-CheckResult -Name "MySQL endpoint" -Ok $hasMysqlEndpoint -Detail ("services={0}, processes={1}, tcp3306={2}" -f $mysqlServicesRunning.Count, $mysqlProcesses.Count, $mysqlTcp3306)

Write-Host ""
Write-Host "Environment variables"
Write-Host ("  DOCDB_ODBC_CONN_MSSQL = {0}" -f $(if ($env:DOCDB_ODBC_CONN_MSSQL) { "set" } else { "not set" }))
Write-Host ("  DOCDB_ODBC_CONN_MYSQL = {0}" -f $(if ($env:DOCDB_ODBC_CONN_MYSQL) { "set" } else { "not set" }))
Write-Host ("  DOCDB_OLEDB_CONN_MSSQL = {0}" -f $(if ($env:DOCDB_OLEDB_CONN_MSSQL) { "set" } else { "not set" }))
Write-Host ("  DOCDB_REQUIRE_DB = {0}" -f $(if ($env:DOCDB_REQUIRE_DB) { $env:DOCDB_REQUIRE_DB } else { "not set" }))
Write-Host ("  DOCDB_RUN_MSSQL = {0}" -f $(if ($env:DOCDB_RUN_MSSQL) { $env:DOCDB_RUN_MSSQL } else { "not set" }))
Write-Host ("  DOCDB_RUN_MYSQL = {0}" -f $(if ($env:DOCDB_RUN_MYSQL) { $env:DOCDB_RUN_MYSQL } else { "not set" }))
Write-Host ("  DOCDB_RUN_OLEDB = {0}" -f $(if ($env:DOCDB_RUN_OLEDB) { $env:DOCDB_RUN_OLEDB } else { "not set" }))

if ($localDbInstances.Count -gt 0) {
    Write-Host ""
    Write-Host "LocalDB instances"
    $localDbInstances | ForEach-Object { Write-Host ("  - {0}" -f $_) }
}

Write-Host ""
if (-not $hasBuildToolchain -or -not $hasRequiredDrivers) {
    Write-Host "Result: NOT READY (missing required build/driver components)"
    exit 1
}

if ($RequireDatabase) {
    if ($requireMssql -and -not $hasMssqlEndpoint) {
        Write-Host "Result: PARTIAL (MSSQL endpoint is not reachable)"
        exit 2
    }

    if ($requireMysql -and -not $hasMysqlEndpoint) {
        Write-Host "Result: PARTIAL (MySQL endpoint is not reachable)"
        exit 2
    }
}

Write-Host "Result: READY"
exit 0