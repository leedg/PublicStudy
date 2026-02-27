param(
    [string]$SourceDir = "Doc/WikiDraft/ServerStructure/diagrams",
    [string]$OutputDir = "Doc/WikiDraft/ServerStructure/assets",
    [switch]$SkipPng
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Get-Command npx -ErrorAction SilentlyContinue))
{
    throw "npx is required. Install Node.js first: https://nodejs.org/"
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

Write-Host "Rendering Mermaid diagrams..." -ForegroundColor Cyan

foreach ($file in $files)
{
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $svgPath = Join-Path $OutputDir ($baseName + ".svg")

    Write-Host "  - $($file.Name) -> $([System.IO.Path]::GetFileName($svgPath))"
    npx --yes @mermaid-js/mermaid-cli `
        -i $file.FullName `
        -o $svgPath `
        --backgroundColor transparent | Out-Null
    if ($LASTEXITCODE -ne 0)
    {
        throw "Failed to render SVG for $($file.Name)"
    }

    if (-not $SkipPng)
    {
        $pngPath = Join-Path $OutputDir ($baseName + ".png")
        Write-Host "    -> $([System.IO.Path]::GetFileName($pngPath))"
        npx --yes @mermaid-js/mermaid-cli `
            -i $file.FullName `
            -o $pngPath `
            --backgroundColor white | Out-Null
        if ($LASTEXITCODE -ne 0)
        {
            throw "Failed to render PNG for $($file.Name)"
        }
    }
}

Write-Host "Done. Output: $OutputDir" -ForegroundColor Green
