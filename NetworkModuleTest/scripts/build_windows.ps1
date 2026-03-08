<#
한글: NetworkModuleTest (Windows) 빌드 스크립트
      build_common.ps1의 MSBuild 자동 탐지를 사용합니다.
      PATH 미등록 환경에서도 vswhere → VS2022 Professional/Community/Enterprise/BuildTools
      순으로 자동 탐지하므로 별도 설정 없이 동작합니다.

사용법:
  .\scripts\build_windows.ps1                        # Debug x64
  .\scripts\build_windows.ps1 -Configuration Release # Release 빌드
  .\scripts\build_windows.ps1 -Rebuild               # 클린 후 재빌드
  .\scripts\build_windows.ps1 -Clean                 # 클린만

선결 조건:
  - Visual Studio 2022 (MSVC v143 이상 + C++ 빌드 도구 + Windows SDK 10.0)
    또는 Visual Studio 2022 Build Tools
  - 다운로드: https://visualstudio.microsoft.com/downloads/
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

$ScriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir      = Resolve-Path (Join-Path $ScriptDir "..")
$SolutionPath = Join-Path $RootDir "NetworkModuleTest.sln"

if (-not (Test-Path $SolutionPath)) {
    Write-Error "솔루션 파일이 없습니다: $SolutionPath"
    exit 1
}

# 한글: build_common.ps1 로드 (MSBuild 자동 탐지 함수 포함)
. (Join-Path $RootDir "build_common.ps1")

Write-Host "빌드 설정:"
Write-Host "  RootDir       = $RootDir"
Write-Host "  Configuration = $Configuration"
Write-Host "  Platform      = $Platform"

if ($Clean) {
    Write-Host "Clean 실행..."
    $exitCode = Invoke-MSBuild -TargetPath $SolutionPath -Configuration $Configuration -Platform $Platform -Target Clean
    if ($exitCode -ne 0) { exit $exitCode }
}

$target = if ($Rebuild) { "Rebuild" } else { "Build" }
$exitCode = Invoke-MSBuild -TargetPath $SolutionPath -Configuration $Configuration -Platform $Platform -Target $target -MultiProc

Write-Host "빌드 완료"
exit $exitCode
