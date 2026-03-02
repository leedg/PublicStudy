# Check and fix LF -> CRLF in all Concurrency headers
$dir = "C:\MyGithub\PublicStudy\NetworkModuleTest\Server\ServerEngine\Concurrency"
$files = Get-ChildItem $dir -Recurse -Include "*.h","*.cpp"

foreach ($f in $files)
{
    $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
    $text  = [System.Text.Encoding]::UTF8.GetString($bytes)

    if ($text -match "(?<!\r)\n")
    {
        $fixed = $text -replace "(?<!\r)\n", "`r`n"
        [System.IO.File]::WriteAllText($f.FullName, $fixed, [System.Text.Encoding]::UTF8)
        Write-Host "Fixed CRLF: $($f.Name)"
    }
    else
    {
        Write-Host "OK (already CRLF): $($f.Name)"
    }
}
Write-Host "Done."
