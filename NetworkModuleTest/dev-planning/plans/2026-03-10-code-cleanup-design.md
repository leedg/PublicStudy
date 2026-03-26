# Code Cleanup Design — 2026-03-10

## 목적

로컬 3개 critical 커밋 이후 남은 correctness/형식 문제 4건을 독립 커밋으로 정리.

## 범위

| # | 커밋 | 파일 | 분류 |
|---|------|------|------|
| 1 | fix(critical) | ExecutionQueue.h | UB 제거 |
| 2 | fix | LinuxNetworkEngine.cpp | 누락 방어 |
| 3 | chore | ExecutionQueue.h | 포맷 |
| 4 | chore | .gitignore | 런타임 아티팩트 |

## 상세 설계

### 1. ExecutionQueue — mNotEmptyCV UB 수정

**문제:** `mNotEmptyCV` 하나를 두 개의 다른 mutex(`mMutexQueueMutex`, `mWaitMutex`)와 함께 `wait()`에 전달.
C++ 표준상 하나의 `condition_variable`은 항상 동일 mutex와 써야 함 → Undefined Behavior.

**수정:** CV를 백엔드별로 분리
- `mNotEmptyMutexCV` — Mutex 백엔드 전용 (`mMutexQueueMutex`와 쌍)
- `mNotEmptyLFCV_pop` — LockFree 백엔드 Pop 전용 (`mWaitMutex`와 쌍)

영향 함수: `TryPushMutex`, `TryPushLockFree`, `PopMutexWait`, `PopLockFreeWait`, `Shutdown`

**안전성:** 기존 `mNotFullMutexCV` / `mNotFullLFCV`와 동일한 패턴. 동작 의미 변경 없음.

### 2. LinuxNetworkEngine — fcntl 반환값 검사

**문제:** `fcntl(F_GETFL)` 실패(-1) 시 `-1 | O_NONBLOCK` 오염값을 `F_SETFL`에 전달.
macOS에는 이미 방어 코드 존재, Linux에 누락.

**수정:**
```cpp
int flags = fcntl(clientSocket, F_GETFL, 0);
if (flags != -1)
{
    fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
}
```

### 3. ExecutionQueue.h 포맷

모든 statement/블록 뒤의 불필요한 빈 줄 제거. 로직 변경 없음.

### 4. .gitignore

런타임 생성 파일(`*.wal`, `0306.txt` 등) 커밋 방지.

## 트레이드오프

- CV 분리 시 `Shutdown()`이 CV 3개(`mNotEmptyMutexCV`, `mNotEmptyLFCV_pop`, `mNotFullMutexCV`, `mNotFullLFCV`) 를 모두 notify_all 해야 함 — 단순 추가이므로 안전.
- LockFree 백엔드 기존 `mNotFullLFCV`와 이름 충돌 없도록 `_pop` 접미사로 명확히 구분.
