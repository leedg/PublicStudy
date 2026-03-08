# run_mssql_test.ps1 — Build and run DB functional test against SQL Server via Docker
#
# Steps:
#   1. Set up MSVC x64 environment
#   2. Install "ODBC Driver 17 for SQL Server" if missing
#   3. Start Docker Desktop if stopped
#   4. Pull + start SQL Server 2022 container if not running
#   5. Wait until SQL Server is ready
#   6. Compile db_functional_test.cpp → build\db_test_mssql.exe
#   7. Run test with ODBC connection string
#   8. Stop + remove container (unless -KeepContainer)
#
# Usage:  .\run_mssql_test.ps1 [-Clean] [-KeepBuild] [-KeepContainer] [-SaPassword <pwd>]

param(
    [switch]$Clean,
    [switch]$KeepBuild,
    [switch]$KeepContainer,
    [string]$SaPassword = 'Test1234!'   # must meet SQL Server complexity rules
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\_common.ps1"

# ── Constants ──────────────────────────────────────────────────────────────────

$ODBC_DRIVER_NAME = "ODBC Driver 17 for SQL Server"
$ODBC_INSTALLER   = "https://go.microsoft.com/fwlink/?linkid=2168524"   # msodbcsql17 x64 MSI
$CONTAINER_NAME   = "db_test_mssql"
$MSSQL_IMAGE      = "mcr.microsoft.com/mssql/server:2022-latest"
$MSSQL_PORT       = 1433
$DB_NAME          = "db_func_test"
$EXE_NAME         = "db_test_mssql.exe"

$CONN_STR = "Driver={$ODBC_DRIVER_NAME};Server=localhost,$MSSQL_PORT;" +
            "Database=$DB_NAME;UID=sa;PWD=$SaPassword;TrustServerCertificate=yes;"

# ── Step 1: MSVC environment ───────────────────────────────────────────────────

Set-VcEnv

# ── Step 2: ODBC Driver 17 ────────────────────────────────────────────────────

Write-Step "ODBC Driver check"

$drivers = Get-OdbcDriver -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
if ($drivers -contains $ODBC_DRIVER_NAME) {
    Write-Ok "Driver already installed: $ODBC_DRIVER_NAME"
} else {
    Write-Warn "$ODBC_DRIVER_NAME not found — installing..."
    $msiPath = Join-Path $env:TEMP "msodbcsql17.msi"
    Write-Info "Downloading from Microsoft..."
    Invoke-WebRequest -Uri $ODBC_INSTALLER -OutFile $msiPath -UseBasicParsing

    Write-Info "Running installer (silent)..."
    $msiArgs = @('/i', $msiPath, '/quiet', '/norestart', 'IACCEPTMSODBCSQLLICENSETERMS=YES')
    $proc = Start-Process msiexec.exe -ArgumentList $msiArgs -Wait -PassThru
    Remove-Item $msiPath -ErrorAction SilentlyContinue

    if ($proc.ExitCode -notin @(0, 3010)) {
        throw "ODBC driver install failed (msiexec exit $($proc.ExitCode))"
    }
    Write-Ok "$ODBC_DRIVER_NAME installed"
}

# ── Step 3: Docker ────────────────────────────────────────────────────────────

Assert-DockerRunning

# ── Step 4: SQL Server container ──────────────────────────────────────────────

Write-Step "SQL Server container"

$existing = docker ps -a --filter "name=^${CONTAINER_NAME}$" --format '{{.Status}}' 2>$null

if ($existing -match '^Up') {
    Write-Ok "Container '$CONTAINER_NAME' already running"
} elseif ($existing -match '^Exited') {
    Write-Info "Container '$CONTAINER_NAME' exists but stopped — starting..."
    docker start $CONTAINER_NAME | Out-Null
} else {
    Write-Info "Pulling $MSSQL_IMAGE ..."
    docker pull $MSSQL_IMAGE

    Write-Info "Creating container '$CONTAINER_NAME'..."
    docker run -d `
        --name $CONTAINER_NAME `
        -e "ACCEPT_EULA=Y" `
        -e "MSSQL_SA_PASSWORD=$SaPassword" `
        -p "${MSSQL_PORT}:1433" `
        $MSSQL_IMAGE | Out-Null
}

# ── Step 5: Wait for SQL Server to accept connections ─────────────────────────

Wait-ContainerReady -ContainerName $CONTAINER_NAME -Port $MSSQL_PORT -TimeoutSec 90

# SQL Server needs a few extra seconds after the port opens before it accepts logins
Write-Info "Waiting an extra 10s for SQL Server login readiness..."
Start-Sleep -Seconds 10

# Create the test database (idempotent)
Write-Info "Ensuring database '$DB_NAME' exists..."
$createDbSql = "IF DB_ID('$DB_NAME') IS NULL CREATE DATABASE [$DB_NAME];"
docker exec $CONTAINER_NAME /opt/mssql-tools/bin/sqlcmd `
    -S "localhost,$MSSQL_PORT" -U sa -P $SaPassword -Q $createDbSql 2>$null
if ($LASTEXITCODE -ne 0) {
    # sqlcmd path differs between image versions
    docker exec $CONTAINER_NAME /opt/mssql-tools18/bin/sqlcmd `
        -S "localhost,$MSSQL_PORT" -U sa -P $SaPassword -C -Q $createDbSql
}
Write-Ok "Database '$DB_NAME' ready"

# ── Step 6: Compile ────────────────────────────────────────────────────────────

Write-Step "Compiling"

if ($Clean) { Remove-BuildDir }
Ensure-BuildDir

$cl       = Get-ClExe
$includes = Get-ServerIncludes
$srcRoot  = $script:ServerRoot
$buildDir = $script:BuildDir
$exePath  = Join-Path $buildDir $EXE_NAME

$sources = @(
    $script:SrcFile
    Join-Path $srcRoot "Database\ODBCDatabase.cpp"
)

$clArgs = @(
    '/nologo', '/std:c++17', '/EHsc', '/W3', '/O1', '/utf-8'
    '/DUSE_MSSQL'
    '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS'
) + $includes + $sources + @(
    "/Fe`"$exePath`""
    "/Fo`"$buildDir\\`""
    'odbc32.lib', 'odbccp32.lib'
)

Write-Info "cl.exe $($clArgs -join ' ')"
& $cl @clArgs

if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed (exit $LASTEXITCODE)"
}
Write-Ok "Built: $exePath"

# ── Step 7: Run ────────────────────────────────────────────────────────────────

Write-Step "Running DB functional test (SQL Server)"
Write-Info "Connection: $CONN_STR"

& $exePath $CONN_STR
$exitCode = $LASTEXITCODE

# ── Step 8: Cleanup ────────────────────────────────────────────────────────────

if (-not $KeepContainer) {
    Write-Info "Stopping and removing container '$CONTAINER_NAME'..."
    docker stop $CONTAINER_NAME | Out-Null
    docker rm   $CONTAINER_NAME | Out-Null
    Write-Ok "Container removed."
}

if (-not $KeepBuild) { Remove-BuildDir }

Write-Step "Done"
if ($exitCode -eq 0) {
    Write-Ok "All tests passed."
} else {
    Write-Err "Test FAILED (exit $exitCode)"
}
exit $exitCode
