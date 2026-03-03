<#
한글: NetworkModuleTest (Windows) 빌드 스크립트
한글: 다른 서버에서도 동일한 절차로 빌드할 수 있도록 구성
#>

param(
    [ValidateSet("Debug","Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("x64","Win32")]
    [string]$Platform = "x64",

    [switch]$Clean,
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Resolve-Path (Join-Path $ScriptDir "..")
$SolutionPath = Join-Path $RootDir "NetworkModuleTest.sln"

if (-not (Test-Path $SolutionPath)) {
    Write-Error "솔루션 파일이 없습니다: $SolutionPath"
    exit 1
}

if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) {
    Write-Error "msbuild를 찾을 수 없습니다."
    Write-Host "설치 가이드:"
    Write-Host "  1) Visual Studio Build Tools 또는 Visual Studio 설치"
    Write-Host "  2) C++ 빌드 도구(MSVC v143 이상)와 Windows SDK 10.0 설치"
    Write-Host "  3) 'Developer PowerShell for VS'에서 실행 권장"
    exit 1
}

Write-Host "빌드 설정:"
Write-Host "  RootDir = $RootDir"
Write-Host "  Configuration = $Configuration"
Write-Host "  Platform = $Platform"

if ($Clean) {
    Write-Host "Clean 실행..."
    msbuild $SolutionPath /t:Clean /p:Configuration=$Configuration /p:Platform=$Platform
}

if ($Rebuild) {
    Write-Host "Rebuild 실행..."
    msbuild $SolutionPath /t:Rebuild /p:Configuration=$Configuration /p:Platform=$Platform
} else {
    Write-Host "Build 실행..."
    msbuild $SolutionPath /t:Build /p:Configuration=$Configuration /p:Platform=$Platform
}

Write-Host "빌드 완료"
