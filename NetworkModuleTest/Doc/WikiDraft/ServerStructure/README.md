# ServerStructure Wiki Draft

이 폴더는 GitHub Wiki로 옮기기 전, 서버 구조 문서를 먼저 다듬기 위한 초안입니다.

## 포함 내용
- `Home.md`: 위키 홈 초안
- `_Sidebar.md`: 위키 사이드바 초안
- `Wiki-Import-Guide.md`: GitHub Wiki 반영 절차
- `01-Overall-Architecture.md`: 전체 서버 구조
- `02-Session-Layer.md`: 세션 계층 구조
- `03-Packet-and-AsyncDB-Flow.md`: 패킷/비동기 DB 처리 흐름
- `04-Graceful-Shutdown.md`: 안전 종료(Graceful Shutdown) 순서
- `05-Reconnect-Strategy.md`: DB 서버 재연결 정책

## 다이어그램 소스
- `diagrams/*.mmd`: Mermaid 원본
- `assets/*.svg`: 정적 SVG 출력물(생성 결과)

## SVG 생성
PowerShell:

```powershell
.\Doc\WikiDraft\ServerStructure\scripts\generate-diagrams.ps1
```

참고:
- 첫 실행은 `npx` 패키지 설치 때문에 시간이 더 걸릴 수 있습니다.
- PNG가 필요 없으면 `-SkipPng` 옵션을 사용하세요.

## Wiki 패키지 생성
PowerShell:

```powershell
.\Doc\WikiDraft\ServerStructure\scripts\build-wiki-package.ps1
```

생성 경로:
- `Doc/WikiDraft/ServerStructure/wiki-package`

## 기준 코드/문서
- `Server/TestServer/src/TestServer.cpp`
- `Server/TestServer/include/TestServer.h`
- `Server/TestServer/include/ClientSession.h`
- `Server/TestServer/include/ServerSession.h`
- `Server/TestServer/include/DBServerSession.h`
- `Doc/02_Architecture.md`
- `Doc/03_Protocol.md`
- `Doc/Architecture/AsyncDB.md`

## 동기화 검증 테스트
PowerShell:

```powershell
.\ModuleTest\ServerStructureSync\validate_server_structure_sync.ps1
```

`run_test_auto.ps1`는 기본적으로 위 검증을 먼저 수행합니다.
생략이 필요하면 `-SkipStructureSyncCheck` 옵션을 사용합니다.

검증일: 2026-02-20
