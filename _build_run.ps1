$msbuild = 'C:/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/MSBuild.exe'
$sln = 'E:/MyGitHub/PublicStudy/NetworkModuleTest/NetworkModuleTest.sln'
$output = & $msbuild $sln /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1
$output | Select-Object -Last 40
