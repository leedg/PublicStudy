& "C:\MyGithub\PublicStudy\NetworkModuleTest\run_perf_test.ps1" -Phase all -RampClients @(10,100,500,1000) -SustainSec 30
exit $LASTEXITCODE
