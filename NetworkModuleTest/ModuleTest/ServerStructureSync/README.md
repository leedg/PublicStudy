# ServerStructureSync Test

이 폴더는 서버 구조 문서/주석/코드 핵심 규칙의 정합성을 정적 검증합니다.

## 검증 항목
- `DBTaskQueue` 워커 1개 정책 유지 여부
- `MakeClientSessionFactory()`의 `weak_ptr` 주입 패턴 유지 여부
- `Stop()` 종료 순서(큐 드레인 -> DB 연결 해제 -> 엔진 종료) 유지 여부
- 재연결 정책(`WSAECONNREFUSED` 1초 고정, 그 외 지수 백오프) 유지 여부
- Wiki 초안 문서의 핵심 정책 반영 여부

## 실행
PowerShell:

```powershell
.\ModuleTest\ServerStructureSync\validate_server_structure_sync.ps1
```

성공 시 종료 코드 `0`, 실패 시 `1`을 반환합니다.
