# ==============================================================================
# generate-diagrams.ps1
# 역할: Doc/WikiDraft/ServerStructure/diagrams/ 에 있는 Mermaid 다이어그램 파일(.mmd)을
#       SVG 및 PNG 이미지로 렌더링하여 assets/ 폴더에 저장한다.
#       npx(@mermaid-js/mermaid-cli) 를 사용하므로 Node.js 가 설치되어 있어야 한다.
#
# 사용법:
#   .\generate-diagrams.ps1 [-SourceDir <경로>] [-OutputDir <경로>] [-SkipPng]
#
# 매개변수:
#   -SourceDir : .mmd 파일이 위치한 폴더 (기본값: Doc/WikiDraft/ServerStructure/diagrams)
#   -OutputDir : 렌더링 결과를 저장할 폴더 (기본값: Doc/WikiDraft/ServerStructure/assets)
#   -SkipPng   : 지정 시 SVG 만 생성하고 PNG 생성을 건너뜀
#
# 사전 요구사항:
#   Node.js (npx 포함) 설치 필요
#   (https://nodejs.org/)
# ==============================================================================
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
