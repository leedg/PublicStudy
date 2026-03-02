$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
$proj = "C:\MyGithub\PublicStudy\NetworkModuleTest\Server\TestServer\TestServer.vcxproj"
& $msbuild $proj /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal
exit $LASTEXITCODE
