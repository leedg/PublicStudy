[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",

    [switch]$RequireDatabase,
    [switch]$RunSamples,
    [switch]$SkipPrereqCheck,

    [string]$OdbcConnectionString,
    [string]$OledbConnectionString
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

function Resolve-CMakeGenerator {
    $vswhere = Resolve-VsWherePath
    if (-not $vswhere) {
        return "Visual Studio 17 2022"
    }

    $version = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationVersion 2>$null | Select-Object -First 1
    if ($version) {
        if ($version.StartsWith("17.")) {
            return "Visual Studio 17 2022"
        }

        if ($version.StartsWith("16.")) {
            return "Visual Studio 16 2019"
        }
    }

    return "Visual Studio 17 2022"
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Exe,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$Step
    )

    Write-Host ("> {0} {1}" -f $Exe, ($Arguments -join " "))
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Step '$Step' failed with exit code $LASTEXITCODE"
    }
}

function Resolve-TestExecutable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir,
        [Parameter(Mandatory = $true)]
        [string]$Configuration,
        [Parameter(Mandatory = $true)]
        [string]$TargetName
    )

    $candidates = @(
        (Join-Path $BuildDir "$Configuration\$TargetName.exe"),
        (Join-Path $BuildDir "$TargetName.exe"),
        (Join-Path $BuildDir "$TargetName\$Configuration\$TargetName.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $found = Get-ChildItem -Path $BuildDir -Recurse -Filter "$TargetName.exe" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($found) {
        return $found.FullName
    }

    throw "Executable not found for target '$TargetName' under '$BuildDir'"
}

function Write-ProcessOutput {
    param(
        [AllowNull()]
        [string]$Text
    )

    if (-not $Text) {
        return
    }

    $lines = $Text -split "`r?`n"
    foreach ($line in $lines) {
        if ($line -ne "") {
            Write-Host $line
        }
    }
}

function Invoke-TestBinary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    Write-Host ""
    Write-Host ("Running {0}: {1}" -f $Name, $Path)

    $stdoutFile = [System.IO.Path]::GetTempFileName()
    $stderrFile = [System.IO.Path]::GetTempFileName()

    try {
        $proc = Start-Process -FilePath $Path -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
        $stdout = Get-Content -Path $stdoutFile -Raw -ErrorAction SilentlyContinue
        $stderr = Get-Content -Path $stderrFile -Raw -ErrorAction SilentlyContinue

        Write-ProcessOutput -Text $stdout
        Write-ProcessOutput -Text $stderr

        return [PSCustomObject]@{
            Name     = $Name
            Path     = $Path
            ExitCode = $proc.ExitCode
        }
    }
    finally {
        Remove-Item -LiteralPath $stdoutFile -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $stderrFile -ErrorAction SilentlyContinue
    }
}

function Restore-EnvValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [AllowNull()]
        [string]$Value
    )

    if ($null -eq $Value) {
        Remove-Item -Path "Env:$Name" -ErrorAction SilentlyContinue
    }
    else {
        Set-Item -Path "Env:$Name" -Value $Value
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$checkScript = Join-Path $scriptDir "check-db-prereqs.ps1"
$buildDir = Join-Path $projectDir ("build\cmake-{0}-{1}" -f $Platform, $Configuration)

Write-Host ""
Write-Host "=== DBModuleTest runner ==="
Write-Host ("ProjectDir   : {0}" -f $projectDir)
Write-Host ("BuildDir     : {0}" -f $buildDir)
Write-Host ("Config/Plat  : {0}/{1}" -f $Configuration, $Platform)

if (-not $SkipPrereqCheck) {
    if (-not (Test-Path -LiteralPath $checkScript)) {
        throw "Prerequisite script not found: $checkScript"
    }

    Write-Host ""
    Write-Host "[1/4] Prerequisite check"
    & $checkScript -RequireDatabase:$RequireDatabase
    if ($LASTEXITCODE -ne 0) {
        throw "Prerequisite check failed (exit code $LASTEXITCODE)"
    }
}

$cmakePath = Resolve-CMakePath
if (-not $cmakePath) {
    throw "cmake.exe not found. Install CMake or Visual Studio CMake components."
}

$generator = Resolve-CMakeGenerator

Write-Host ""
Write-Host "[2/4] CMake configure"
Invoke-External -Exe $cmakePath -Arguments @(
    "-S", $projectDir,
    "-B", $buildDir,
    "-G", $generator,
    "-A", $Platform
) -Step "cmake configure"

Write-Host ""
Write-Host "[3/4] Build targets"
Invoke-External -Exe $cmakePath -Arguments @(
    "--build", $buildDir,
    "--config", $Configuration,
    "--target", "db_tests", "odbc_sample", "oledb_sample"
) -Step "cmake build"

$oldOdbc = $env:DOCDB_ODBC_CONN
$oldOledb = $env:DOCDB_OLEDB_CONN
$oldRequireDb = $env:DOCDB_REQUIRE_DB

try {
    if ($PSBoundParameters.ContainsKey("OdbcConnectionString")) {
        $env:DOCDB_ODBC_CONN = $OdbcConnectionString
    }

    if ($PSBoundParameters.ContainsKey("OledbConnectionString")) {
        $env:DOCDB_OLEDB_CONN = $OledbConnectionString
    }

    $env:DOCDB_REQUIRE_DB = if ($RequireDatabase) { "1" } else { "0" }

    Write-Host ""
    Write-Host "[4/4] Run tests"

    $results = @()
    $dbTestsExe = Resolve-TestExecutable -BuildDir $buildDir -Configuration $Configuration -TargetName "db_tests"
    $results += Invoke-TestBinary -Path $dbTestsExe -Name "db_tests"

    if ($RunSamples) {
        $odbcSampleExe = Resolve-TestExecutable -BuildDir $buildDir -Configuration $Configuration -TargetName "odbc_sample"
        $oledbSampleExe = Resolve-TestExecutable -BuildDir $buildDir -Configuration $Configuration -TargetName "oledb_sample"

        $results += Invoke-TestBinary -Path $odbcSampleExe -Name "odbc_sample"
        $results += Invoke-TestBinary -Path $oledbSampleExe -Name "oledb_sample"
    }

    Write-Host ""
    Write-Host "=== Test summary ==="
    foreach ($item in $results) {
        $mark = if ($item.ExitCode -eq 0) { "PASS" } else { "FAIL" }
        Write-Host ("[{0}] {1} (exit={2})" -f $mark, $item.Name, $item.ExitCode)
    }

    $failedResults = @($results | Where-Object { $_.ExitCode -ne 0 })
    if ($failedResults.Count -gt 0) {
        exit 1
    }

    exit 0
}
finally {
    Restore-EnvValue -Name "DOCDB_ODBC_CONN" -Value $oldOdbc
    Restore-EnvValue -Name "DOCDB_OLEDB_CONN" -Value $oldOledb
    Restore-EnvValue -Name "DOCDB_REQUIRE_DB" -Value $oldRequireDb
}