param(
    [string]$SourceDir,
    [string]$OutputDir,
    [switch]$SkipPng
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Get-Command npx -ErrorAction SilentlyContinue))
{
    throw "npx is required. Install Node.js first."
}

if (-not (Test-Path $SourceDir))
{
    throw "Source directory not found: $SourceDir"
}

if (-not (Test-Path $OutputDir))
{
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$files = Get-ChildItem -Path $SourceDir -Filter "*.mmd" | Sort-Object Name
if ($files.Count -eq 0)
{
    throw "No .mmd files found in $SourceDir"
}

foreach ($file in $files)
{
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $svgPath = Join-Path $OutputDir ($baseName + ".svg")

    npx --yes @mermaid-js/mermaid-cli `
        -i $file.FullName `
        -o $svgPath `
        --backgroundColor transparent `
        --quiet | Out-Null

    if (-not $SkipPng)
    {
        $pngPath = Join-Path $OutputDir ($baseName + ".png")
        npx --yes @mermaid-js/mermaid-cli `
            -i $file.FullName `
            -o $pngPath `
            --backgroundColor white `
            --quiet | Out-Null
    }
}

Write-Host "Rendered Mermaid diagrams to $OutputDir" -ForegroundColor Green
