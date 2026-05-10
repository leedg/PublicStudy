# ServerStructureSync Test

이 디렉터리는 서버 구조 관련 문서/주석/코드의 정합성을 점검하기 위한 테스트를 설명한다.

## 현재 기준

- 기본 세션 생성 경로는 `SessionManager::CreateSession()` + `SetSessionConfigurator()`
- 기본 세션 객체는 `SessionPool`의 `Core::Session`
- 접속/종료 DB 기록은 `TestServer` 이벤트 핸들러가 `DBTaskQueue`에 enqueue
- `DBTaskQueue` 구현은 워커별 독립 큐 + 세션 친화도 라우팅
- 현재 `TestServer` 런타임 설정은 `Initialize(1, "db_tasks.wal", ...)`

## 검증 항목

- 현재 문서가 `SessionFactory`가 아니라 `SetSessionConfigurator()` 경로를 설명하는지
- `DBTaskQueue`를 "단일 공유 큐"가 아니라 워커별 독립 큐로 설명하는지
- 현재 설정(workerCount=1)과 구현 구조(세션 친화도 멀티워커 가능)를 구분하는지
- `Stop()` 종료 순서가 큐 드레인 -> 로컬 DB disconnect -> 엔진 종료로 맞는지
- DB ping 반복 경로를 `TimerQueue` 기준으로 설명하는지

## 실행

PowerShell:

```powershell
.\ModuleTest\ServerStructureSync\validate_server_structure_sync.ps1
```

성공 시 종료 코드 `0`, 실패 시 `1`을 반환한다.
