# 빌드 요약: msbuild 경로를 PATH -> vswhere -> 고정 후보 순으로 탐지합니다.
# 목적: 로컬 절대경로 하드코딩 없이 어느 워크스페이스에서도 동일하게 빌드 스크립트가 동작하도록 보장합니다.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-MSBuildPath {
    $msbuildCmd = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($msbuildCmd) {
        return $msbuildCmd.Source
    }

    $vswhereCandidates = @(
        "$env:ProgramFiles(x86)\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    foreach ($vswhere in $vswhereCandidates) {
        if (-not (Test-Path -Path $vswhere -PathType Leaf)) {
            continue
        }

        $installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installationPath)) {
            $candidate = Join-Path $installationPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -Path $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    $fallbackCandidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )

    foreach ($candidate in $fallbackCandidates) {
        if (Test-Path -Path $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw "MSBuild를 찾지 못했습니다. Visual Studio 2022(MSBuild 구성요소) 설치 또는 PATH 등록이 필요합니다."
}

function Invoke-MSBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TargetPath,
        [string]$Configuration = "Release",
        [string]$Platform = "x64",
        [switch]$MultiProc
    )

    if (-not (Test-Path -Path $TargetPath -PathType Leaf)) {
        throw "빌드 대상을 찾지 못했습니다: $TargetPath"
    }

    $msbuild = Resolve-MSBuildPath
    $args = @(
        $TargetPath,
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/nologo",
        "/verbosity:minimal"
    )

    if ($MultiProc) {
        $args += "/m"
    }

    Write-Host "MSBuild 경로: $msbuild"
    Write-Host "빌드 대상: $TargetPath"
    Write-Host "빌드 설정: Configuration=$Configuration / Platform=$Platform"

    & $msbuild @args
    return $LASTEXITCODE
}
