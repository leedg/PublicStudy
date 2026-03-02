$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
$sln = "C:\MyGithub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln"
& $msbuild $sln /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal /m
exit $LASTEXITCODE
