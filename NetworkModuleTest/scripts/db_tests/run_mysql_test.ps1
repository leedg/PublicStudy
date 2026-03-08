# run_mysql_test.ps1 — Build and run DB functional test against MySQL via Docker
#
# Steps:
#   1. Set up MSVC x64 environment
#   2. Install MySQL Connector/ODBC if missing
#   3. Start Docker Desktop if stopped
#   4. Pull + start MySQL 8 container if not running
#   5. Wait until MySQL is ready
#   6. Create test database
#   7. Compile db_functional_test.cpp -> build\db_test_mysql.exe
#   8. Run test with ODBC connection string
#   9. Stop + remove container (unless -KeepContainer)
#
# Usage:  .\run_mysql_test.ps1 [-Clean] [-KeepBuild] [-KeepContainer] [-RootPassword <pwd>]

param(
    [switch]$Clean,
    [switch]$KeepBuild,
    [switch]$KeepContainer,
    [string]$RootPassword = 'Test1234!'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\_common.ps1"

# ── Constants ──────────────────────────────────────────────────────────────────

# MySQL Connector/ODBC — CDN direct MSI (update version as needed)
$MYSQL_ODBC_MSI_URL = "https://cdn.mysql.com//Downloads/Connector-ODBC/8.4/mysql-connector-odbc-8.4.0-winx64.msi"
$CONTAINER_NAME   = "db_test_mysql"
$MYSQL_IMAGE      = "mysql:8.0"
$MYSQL_PORT       = 3306
$DB_NAME          = "db_func_test"
$EXE_NAME         = "db_test_mysql.exe"

# ── Step 1: MSVC environment ───────────────────────────────────────────────────

Set-VcEnv

# ── Step 2: MySQL Connector/ODBC ──────────────────────────────────────────────

Write-Step "MySQL Connector/ODBC check"

# Find any installed MySQL ODBC Unicode driver (8.x naming varies: "MySQL ODBC 8.x Unicode Driver")
$drivers = Get-OdbcDriver -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$mysqlDriver = $drivers | Where-Object { $_ -match 'MySQL ODBC .+ (Unicode|ANSI) Driver' } |
               Where-Object { $_ -match 'Unicode' } | Select-Object -First 1
if (-not $mysqlDriver) {
    # Accept ANSI as fallback
    $mysqlDriver = $drivers | Where-Object { $_ -match 'MySQL ODBC .+ (Unicode|ANSI) Driver' } | Select-Object -First 1
}

if ($mysqlDriver) {
    Write-Ok "Driver already installed: $mysqlDriver"
} else {
    Write-Warn "MySQL ODBC Unicode Driver not found — installing MySQL Connector/ODBC..."

    $msiPath = Join-Path $env:TEMP "mysql-connector-odbc-winx64.msi"
    Write-Info "Downloading MySQL Connector/ODBC: $MYSQL_ODBC_MSI_URL"
    Invoke-WebRequest -Uri $MYSQL_ODBC_MSI_URL -OutFile $msiPath -UseBasicParsing

    Write-Info "Running installer (silent)..."
    $proc = Start-Process msiexec.exe -ArgumentList @('/i', $msiPath, '/quiet', '/norestart') -Wait -PassThru
    Remove-Item $msiPath -ErrorAction SilentlyContinue

    if ($proc.ExitCode -notin @(0, 3010)) {
        throw "MySQL Connector/ODBC install failed (msiexec exit $($proc.ExitCode))"
    }

    # Re-read driver list to get exact installed name
    $drivers = Get-OdbcDriver -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
    $mysqlDriver = $drivers | Where-Object { $_ -match 'MySQL ODBC .+ Unicode Driver' } | Select-Object -First 1
    if (-not $mysqlDriver) { $mysqlDriver = $drivers | Where-Object { $_ -match 'MySQL ODBC .+ ANSI Driver' } | Select-Object -First 1 }
    if (-not $mysqlDriver) {
        throw "MySQL ODBC driver not found after installation — check installer logs"
    }
    Write-Ok "Installed: $mysqlDriver"
}

$CONN_STR = "Driver={$mysqlDriver};Server=127.0.0.1;Port=$MYSQL_PORT;" +
            "Database=$DB_NAME;UID=root;PWD=$RootPassword;"

# ── Step 3: Docker ────────────────────────────────────────────────────────────

Assert-DockerRunning

# ── Step 4: MySQL container ───────────────────────────────────────────────────

Write-Step "MySQL container"

$existing = docker ps -a --filter "name=^${CONTAINER_NAME}$" --format '{{.Status}}' 2>$null

if ($existing -match '^Up') {
    Write-Ok "Container '$CONTAINER_NAME' already running"
} elseif ($existing -match '^Exited') {
    Write-Info "Container '$CONTAINER_NAME' exists but stopped — starting..."
    docker start $CONTAINER_NAME | Out-Null
} else {
    Write-Info "Pulling $MYSQL_IMAGE ..."
    docker pull $MYSQL_IMAGE

    Write-Info "Creating container '$CONTAINER_NAME'..."
    docker run -d `
        --name $CONTAINER_NAME `
        -e "MYSQL_ROOT_PASSWORD=$RootPassword" `
        -e "MYSQL_DATABASE=$DB_NAME" `
        -p "${MYSQL_PORT}:3306" `
        --health-cmd "mysqladmin ping -h 127.0.0.1 -u root -p$RootPassword --silent" `
        --health-interval 3s `
        --health-timeout 5s `
        --health-retries 15 `
        $MYSQL_IMAGE | Out-Null
}

# ── Step 5: Wait for MySQL ────────────────────────────────────────────────────

Wait-ContainerReady -ContainerName $CONTAINER_NAME -Port $MYSQL_PORT -TimeoutSec 90

# MySQL port open != mysqld ready; wait for health check
Write-Info "Waiting for MySQL to finish initialization (up to 60s)..."
$deadline = (Get-Date).AddSeconds(60)
do {
    Start-Sleep -Seconds 3
    $health = docker inspect --format '{{.State.Health.Status}}' $CONTAINER_NAME 2>$null
    if ($health -eq 'healthy') { Write-Ok "MySQL health: healthy"; break }
    Write-Info "  health: $health"
} while ((Get-Date) -lt $deadline)

if ($health -ne 'healthy') {
    Write-Warn "Health check timed out — proceeding anyway (mysqladmin may not be in PATH)"
}

# ── Step 6: Ensure database exists ────────────────────────────────────────────

Write-Info "Ensuring database '$DB_NAME' exists..."
# Suppress MySQL's password-on-CLI warning (goes to stderr) via ErrorActionPreference
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'SilentlyContinue'
docker exec $CONTAINER_NAME mysql -u root "-p$RootPassword" -e "CREATE DATABASE IF NOT EXISTS ${DB_NAME};" 2>&1 | Out-Null
$ErrorActionPreference = $prevEap
Write-Ok "Database '$DB_NAME' ready"

# ── Step 7: Compile ────────────────────────────────────────────────────────────

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
    '/DUSE_MYSQL'
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

# ── Step 8: Run ────────────────────────────────────────────────────────────────

Write-Step "Running DB functional test (MySQL)"
Write-Info "Connection: $CONN_STR"

& $exePath $CONN_STR
$exitCode = $LASTEXITCODE

# ── Step 9: Cleanup ────────────────────────────────────────────────────────────

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
