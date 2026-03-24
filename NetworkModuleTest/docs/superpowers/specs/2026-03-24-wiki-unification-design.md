# Wiki 소스 일원화 및 GitHub Wiki 자동 발행 설계

**날짜:** 2026-03-24
**상태:** 승인됨

---

## 목표

- `Doc/WikiDraft/`와 `Doc/Reports/WikiPackage/`로 분기된 Wiki 소스를 `Doc/Wiki/`로 통합
- 현재 코드 기준으로 내용 재작성 (신규 모듈 페이지 추가 포함)
- main 브랜치 push 시 GitHub Wiki 자동 동기화

---

## 삭제 대상

| 경로 | 이유 |
|------|------|
| `Doc/WikiDraft/ServerStructure/` (27파일) | `Doc/Wiki/`로 통합 |
| `Doc/Reports/WikiPackage/` | `Doc/Wiki/`로 통합 |
| `Doc/Reports/_scripts/` (9파일) | docx 생성 도구, Wiki 발행 이후 불필요 |

---

## 신규 폴더 구조

```
NetworkModuleTest/Doc/Wiki/
  Home.md
  _Sidebar.md
  01-Overall-Architecture.md
  02-Network-Engine.md
  03-Session-Layer.md
  04-Concurrency.md               ← 신규 (ExecutionQueue, KeyedDispatcher, AsyncScope)
  05-Database.md                  ← 신규 (IDatabase, ConnectionPool, 멀티DB)
  06-Buffer-Memory.md             ← 신규 (IBufferPool, RIO/IOUring/Standard)
  07-Shutdown-Reconnect.md
  08-Build-and-Run.md
  assets/                         ← PNG/SVG 렌더 결과
  diagrams/                       ← .mmd Mermaid 소스
  scripts/
    generate-diagrams.ps1         ← mmd → svg/png 렌더링
    publish-wiki.ps1              ← 수동 발행용 (hook 우회)
```

---

## 페이지별 내용 구성

각 페이지는 아래 형식을 따른다:

```markdown
# 제목

## 개요
(2~3줄 요약)

## 다이어그램
(Mermaid 인라인 + SVG fallback 이미지)

## 상세 설명

## 관련 코드 포인트
- `파일경로:라인번호`
```

### 페이지 범위

| 페이지 | 주요 내용 | 참고 소스 |
|--------|-----------|-----------|
| 01 Overall Architecture | 전체 2계층 구조, 플랫폼 분기 | WikiDraft + WikiPackage 교차 |
| 02 Network Engine | AsyncIOProvider, 플랫폼 백엔드(Windows/Linux/macOS) | `Network/Platforms/` |
| 03 Session Layer | Session, SessionManager, SessionPool, 연결 수립 흐름 | `Network/Core/Session*` |
| 04 Concurrency | ExecutionQueue, KeyedDispatcher, AsyncScope, Channel, TimerQueue | `Concurrency/` |
| 05 Database | IDatabase 추상화, ConnectionPool, 멀티DB(SQLite/ODBC/PostgreSQL) | `Database/` |
| 06 Buffer Memory | IBufferPool, RIOBufferPool, IOUringBufferPool, StandardBufferPool | `Core/Memory/` |
| 07 Shutdown Reconnect | Graceful Shutdown 순서, DB 재연결 백오프 루프 | WikiDraft 04+05 기반 |
| 08 Build and Run | 빌드 명령, 실행 순서(DBServer→Server→Client), 포트 정보 | README + 기존 run scripts |

---

## 자동 발행 구조

```
main 브랜치 push
  └→ .git/hooks/post-push 실행
       └→ Doc/Wiki/ 변경 여부 확인
            └→ (변경 있을 때만) scripts/publish-wiki.ps1 실행
                 └→ PublicStudy.wiki.git 클론 (임시 디렉터리)
                      └→ Doc/Wiki/ 파일 복사
                           └→ wiki repo commit + push
```

### publish-wiki.ps1 동작

1. `$env:TEMP` 하위에 임시 디렉터리 생성
2. `git clone https://github.com/leedg/PublicStudy.wiki.git` 실행
3. 기존 wiki 파일 삭제 후 `Doc/Wiki/` 내용 복사
4. `git commit -m "docs: sync wiki from main"` + `git push`
5. 임시 디렉터리 정리

### post-push hook 조건

```bash
# Doc/Wiki/ 하위 변경이 있는 push일 때만 실행
changed=$(git diff --name-only HEAD~1 HEAD | grep "^NetworkModuleTest/Doc/Wiki/")
if [ -n "$changed" ]; then
  powershell.exe -File scripts/publish-wiki.ps1
fi
```

---

## 내용 작성 방침

- **언어:** 한국어
- **다이어그램:** Mermaid 인라인 (GitHub Wiki 네이티브 렌더링) + SVG fallback
- **코드 참조:** 현재 `Server/ServerEngine/` 코드 직접 읽어서 라인 포인터 확인
- **검증:** 각 페이지 작성 후 실제 코드와 대조

---

## 구현 순서

1. `Doc/Wiki/` 폴더 및 기존 페이지(01~03, 07~08) 작성 — WikiDraft/WikiPackage 내용 교차 갱신
2. 신규 페이지(04~06) 코드 기반 작성
3. Mermaid 다이어그램 `.mmd` 작성 + SVG 렌더링
4. `publish-wiki.ps1` 스크립트 작성
5. `.git/hooks/post-push` hook 설정
6. 기존 `Doc/WikiDraft/`, `Doc/Reports/WikiPackage/`, `Doc/Reports/_scripts/` 삭제
7. 초기 GitHub Wiki push 실행
