param(
    [string]$SourceRoot = "Doc/WikiDraft/ServerStructure",
    [string]$OutputRoot = "Doc/WikiDraft/ServerStructure/wiki-package",
    [switch]$IncludePng
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path $SourceRoot))
{
    throw "Source root not found: $SourceRoot"
}

$pages = @(
    "Home.md",
    "_Sidebar.md",
    "01-Overall-Architecture.md",
    "02-Session-Layer.md",
    "03-Packet-and-AsyncDB-Flow.md",
    "04-Graceful-Shutdown.md",
    "05-Reconnect-Strategy.md",
    "Wiki-Import-Guide.md"
)

$assetSource = Join-Path $SourceRoot "assets"
if (-not (Test-Path $assetSource))
{
    throw "Assets folder not found: $assetSource"
}

if (Test-Path $OutputRoot)
{
    Remove-Item -Path $OutputRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$assetOutput = Join-Path $OutputRoot "assets"
New-Item -ItemType Directory -Path $assetOutput | Out-Null

foreach ($page in $pages)
{
    $src = Join-Path $SourceRoot $page
    if (-not (Test-Path $src))
    {
        throw "Missing page: $src"
    }

    $dst = Join-Path $OutputRoot $page
    Copy-Item -Path $src -Destination $dst -Force
}

Get-ChildItem -Path $assetSource -Filter "*.svg" | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination (Join-Path $assetOutput $_.Name) -Force
}

if ($IncludePng)
{
    Get-ChildItem -Path $assetSource -Filter "*.png" | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination (Join-Path $assetOutput $_.Name) -Force
    }
}

$readmePath = Join-Path $OutputRoot "README.md"
@"
# Wiki Package

이 폴더는 GitHub Wiki 루트에 바로 복사할 수 있는 배포용 패키지입니다.

## 포함
- Wiki 페이지 markdown
- assets 폴더(svg 기본, png 선택)

## 복사 방법
1. <repo>.wiki.git 클론
2. 이 폴더 내용을 Wiki 루트에 복사
3. 커밋/푸시
"@ | Set-Content -Path $readmePath -Encoding utf8

Write-Host "Wiki package generated: $OutputRoot" -ForegroundColor Green
