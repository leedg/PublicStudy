# ==============================================================================
# Render-Mermaid.ps1  (Doc/Reports/_scripts/Render-Mermaid.ps1)
# 역할: 지정한 폴더의 Mermaid 다이어그램 파일(.mmd)을 SVG 및 PNG 로 렌더링한다.
#       Doc/Reports 패키지(ExecutiveSummary, TeamShare, WikiPackage)의
#       diagrams/ → assets/ 변환 용도로 사용한다.
#       generate-diagrams.ps1 와 동일한 기능이지만 경로를 직접 지정한다.
#
# 사용법:
#   .\Render-Mermaid.ps1 -SourceDir <다이어그램폴더> -OutputDir <에셋폴더> [-SkipPng]
#
# 매개변수:
#   -SourceDir : .mmd 파일이 위치한 폴더 (필수)
#   -OutputDir : SVG/PNG 출력 폴더 (필수)
#   -SkipPng   : 지정 시 SVG 만 생성하고 PNG 생성을 건너뜀
#
# 사전 요구사항:
#   Node.js (npx 포함) 설치 필요
# ==============================================================================
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
