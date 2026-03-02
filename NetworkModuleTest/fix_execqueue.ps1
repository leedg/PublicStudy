$path = "C:\MyGithub\PublicStudy\NetworkModuleTest\Server\ServerEngine\Concurrency\ExecutionQueue.h"
$c = [IO.File]::ReadAllText($path)
$c2 = $c.Replace("::time_point::max()", "::time_point::max)()")
[IO.File]::WriteAllText($path, $c2)
Write-Host "Done. Occurrences replaced."
exit 0
