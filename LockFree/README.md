# LockFree Samples (VS2022)

- Intro: 단일 생산자/소비자(SPSC) 락프리 큐 데모
  - 솔루션: `LockFree.sln` (프로젝트: `Intro/LockFreeIntro`)
  - 코드: `Intro/LockFreeIntro/include/LockFreeSPSCQueue.h`, `Intro/LockFreeIntro/src/main.cpp`
- Queue: 다중 생산자/소비자(MPMC) 락프리 큐 데모
  - 솔루션: `LockFree.sln` (프로젝트: `Queue/LockFreeQueue`)
  - 코드: `Queue/LockFreeQueue/include/LockFreeMPMCQueue.h`, `Queue/LockFreeQueue/src/main.cpp`

## 실행 방법
1. Visual Studio 2022에서 `LockFree.sln`을 엽니다.
2. 구성/플랫폼(예: `Debug|x64`)을 선택합니다.
3. 각 프로젝트를 시작 프로젝트로 설정해 실행(F5)해 보세요.

## 메모
- 두 큐 모두 용량은 2의 거듭제곱이어야 합니다.
- Intro는 SPSC(1P-1C), Queue는 MPMC(NP-NC) 예제입니다.
