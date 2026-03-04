# Fix line endings: LF -> CRLF for specified files
param([string[]]$Files)

foreach ($path in $Files)
{
    if (-not (Test-Path $path)) { Write-Warning "Not found: $path"; continue }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $text  = [System.Text.Encoding]::UTF8.GetString($bytes)

    # Already CRLF everywhere? skip
    if ($text -notmatch "(?<!\r)\n") { Write-Host "Already CRLF: $path"; continue }

    # Replace bare LF with CRLF (don't double-convert existing CRLF)
    $fixed = $text -replace "(?<!\r)\n", "`r`n"
    [System.IO.File]::WriteAllText($path, $fixed, [System.Text.Encoding]::UTF8)
    Write-Host "Fixed: $path"
}
