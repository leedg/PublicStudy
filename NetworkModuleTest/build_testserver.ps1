param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $root "build_common.ps1")

$projectPath = Join-Path $root "Server\TestServer\TestServer.vcxproj"
$exitCode = Invoke-MSBuild -TargetPath $projectPath -Configuration $Configuration -Platform $Platform
exit $exitCode