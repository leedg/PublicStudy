# publish-wiki.ps1 — Doc/Wiki/ 내용을 PublicStudy.wiki.git에 동기화
# 사용법: .\publish-wiki.ps1 [-WikiUrl <url>]
#   -WikiUrl 미지정 시 GH_TOKEN 우선, 없으면 SSH 폴백
param(
  [string]$WikiUrl = ""
)

$RepoRoot = Resolve-Path "$PSScriptRoot/../../../.."
$WikiSource = Join-Path $RepoRoot "NetworkModuleTest/Doc/Wiki"
$TempDir = Join-Path $env:TEMP "wiki-publish-$(Get-Random)"

# URL 결정: 우선순위 — GH_TOKEN 환경변수 → gh CLI 토큰 → SSH
# SSH 키가 읽기 전용 deploy key일 수 있으므로 토큰 방식을 먼저 시도한다
if ($WikiUrl -eq "") {
  $Token = ""
  if ($env:GH_TOKEN) {
    $Token = $env:GH_TOKEN
  } else {
    $GhToken = & gh auth token 2>&1
    if ($LASTEXITCODE -eq 0 -and $GhToken -match "^ghp_|^github_pat_|^gho_") {
      $Token = $GhToken.Trim()
    }
  }

  if ($Token) {
    $WikiUrl = "https://${Token}@github.com/leedg/PublicStudy.wiki.git"
  } else {
    $SshTest = & git ls-remote git@github.com:leedg/PublicStudy.wiki.git HEAD 2>&1
    if ($LASTEXITCODE -eq 0) {
      $WikiUrl = "git@github.com:leedg/PublicStudy.wiki.git"
    } else {
      Write-Error "인증 필요: GH_TOKEN 환경변수, gh CLI 로그인(gh auth login), 또는 쓰기 권한 있는 SSH 키 중 하나를 설정하세요."
      exit 1
    }
  }
}

$DisplayUrl = if ($WikiUrl -match "GH_TOKEN|@github\.com") { "https://<token>@github.com/leedg/PublicStudy.wiki.git" } else { $WikiUrl }
Write-Host "Wiki URL: $DisplayUrl"

try {
  Write-Host "Cloning wiki repo..."
  git clone $WikiUrl $TempDir
  if ($LASTEXITCODE -ne 0) { throw "git clone failed" }

  # 기존 wiki 파일 삭제 후 전체 복사 (assets/, diagrams/ 서브디렉터리 포함)
  Get-ChildItem $TempDir -Exclude ".git" | Remove-Item -Recurse -Force
  Copy-Item "$WikiSource/*" $TempDir -Recurse -Force

  Push-Location $TempDir
  try {
    git add -A
    $HasChanges = git status --porcelain
    if ($HasChanges) {
      git commit -m "docs: sync wiki from main $(Get-Date -Format 'yyyy-MM-dd')"
      git push
      if ($LASTEXITCODE -ne 0) { throw "git push 실패 — 인증 또는 네트워크 문제를 확인하세요." }
      Write-Host "Wiki published successfully." -ForegroundColor Green
    } else {
      Write-Host "No changes to publish." -ForegroundColor Yellow
    }
  } finally {
    Pop-Location
  }
} finally {
  if (Test-Path $TempDir) { Remove-Item $TempDir -Recurse -Force }
}
