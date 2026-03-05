param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $root "build_common.ps1")

$solutionPath = Join-Path $root "NetworkModuleTest.sln"
$exitCode = Invoke-MSBuild -TargetPath $solutionPath -Configuration $Configuration -Platform $Platform -MultiProc
exit $exitCode