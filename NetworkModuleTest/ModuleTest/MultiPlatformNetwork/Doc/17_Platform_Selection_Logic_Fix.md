# 플랫폼 선택 로직 수정

**작성일**: 2026-01-27  
**버전**: 1.0  
**목표**: CreateAsyncIOProvider() 로직 버그 분석 및 수정  
**상태**: 🔴 **문제 발견 - 즉시 수정 필요**

---

## 🐛 문제 분석

### 발견된 버그

**파일**: `AsyncIOProvider.cpp`  
**함수**: `CreateAsyncIOProvider()`  
**문제 위치**: 라인 25-41 (Windows 케이스)

```cpp
// ❌ WRONG - 현재 코드
case PlatformType::IOCP:
{
    // Try RIO first if high performance is preferred
    if (preferHighPerformance && Platform::IsWindowsRIOSupported())
    {
        auto provider = CreateRIOProvider();  // ✓ 올바름
        if (provider && provider->Initialize())
            return provider;
    }
    
    // Fall back to IOCP
    auto provider = CreateIocpProvider();     // ✓ 올바름
    if (provider && provider->Initialize())
        return provider;
    
    return nullptr;
}
```

### 왜 잘못되었나?

1. **Switch 문의 케이스 이름이 부정확함**
   - `case PlatformType::IOCP:` - Windows 플랫폼을 나타내는 것이 아니라 "기본 IOCP 선택"을 나타냄
   - `case PlatformType::Epoll:` - Linux 플랫폼을 나타내는 것이 아니라 "기본 epoll 선택"을 나타냄

2. **GetCurrentPlatform()이 반환하는 값의 의미**
   - Windows에서: `PlatformType::IOCP` 반환 (= "기본 선택은 IOCP")
   - Linux에서: `PlatformType::Epoll` 반환 (= "기본 선택은 epoll")
   - macOS에서: `PlatformType::Kqueue` 반환 (= "기본 선택은 kqueue")

3. **논리적 혼동**
   - PlatformType이 "OS 플랫폼"이 아니라 "백엔드 구현"을 나타냄
   - CreateAsyncIOProvider()가 고성능 옵션을 처리하려고 하는데, 케이스 이름이 혼동을 초래

---

## ✅ 해결 방안

### 방안 1: 케이스 이름 명확화 (추천)

```cpp
// ✅ CORRECT - 수정된 코드
case PlatformType::IOCP:  // Windows에서 기본 선택 = IOCP
{
    // Try RIO first if high performance is preferred and supported
    if (preferHighPerformance && Platform::IsWindowsRIOSupported())
    {
        auto provider = CreateRIOProvider();
        if (provider && provider->Initialize())
            return provider;
        // RIO 초기화 실패 → IOCP로 폴백
    }
    
    // Fall back to IOCP (또는 기본값)
    auto provider = CreateIocpProvider();
    if (provider && provider->Initialize())
        return provider;
    
    // IOCP도 실패 → nullptr
    return nullptr;
}

case PlatformType::Epoll:  // Linux에서 기본 선택 = epoll
{
    // Try io_uring first if high performance is preferred and supported
    if (preferHighPerformance && Platform::IsLinuxIOUringSupported())
    {
        auto provider = CreateIOUringProvider();
        if (provider && provider->Initialize())
            return provider;
        // io_uring 초기화 실패 → epoll로 폴백
    }
    
    // Fall back to epoll (또는 기본값)
    auto provider = CreateEpollProvider();
    if (provider && provider->Initialize())
        return provider;
    
    // epoll도 실패 → nullptr
    return nullptr;
}
```

### 방안 2: PlatformType 재설계 (근본적)

```cpp
// enum class를 두 개로 분리
enum class OSPlatform : uint8_t
{
    Windows,  // OS 레벨
    Linux,    // OS 레벨
    macOS,    // OS 레벨
};

enum class AsyncIOBackend : uint8_t
{
    IOCP,     // 구현 레벨
    RIO,      // 구현 레벨
    Epoll,    // 구현 레벨
    IOUring,  // 구현 레벨
    Kqueue,   // 구현 레벨
};

// 그러면 로직이 명확해짐:
std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(bool preferHighPerformance)
{
    OSPlatform os = GetCurrentOS();
    
    switch(os)
    {
    case OSPlatform::Windows:
    {
        // Windows에서 고성능 선호 → RIO 시도 → IOCP 폴백
        if (preferHighPerformance && IsWindowsRIOSupported())
        {
            auto provider = CreateRIOProvider();
            if (provider && provider->Initialize())
                return provider;
        }
        
        auto provider = CreateIocpProvider();
        if (provider && provider->Initialize())
            return provider;
        
        return nullptr;
    }
    // ...
    }
}
```

---

## 📋 현재 설계 문서와의 관계

### 16_Fallback_Mechanisms.md의 의도

```
Windows 플랫폼 감지
    ↓
PlatformType::IOCP 반환 (= "기본 IOCP 선택")
    ↓
CreateAsyncIOProvider(true) 호출 (고성능 원함)
    ↓
- preferHighPerformance = true && IsWindowsRIOSupported() = true
    → RIO 시도 → (성공하면 반환, 실패하면 다음)
- RIO 실패 또는 unsupported
    → IOCP 시도 → (성공하면 반환, 실패하면 nullptr)
```

### 06_Cross_Platform_Architecture.md의 의도

플랫폼별 폴백 우선순위:

```
Windows:
  선호 (preferHighPerformance=true) →  RIO → IOCP → nullptr
  기본 (preferHighPerformance=false) → IOCP → nullptr

Linux:
  선호 (preferHighPerformance=true) →  io_uring → epoll → nullptr
  기본 (preferHighPerformance=false) → epoll → nullptr

macOS:
  선호 또는 기본 → kqueue → nullptr
```

---

## 🔧 수정 계획

### Step 1: 코드 수정
- ✅ CreateAsyncIOProvider() 로직 검증 (실제로는 올바름 - 코드만 명확히)
- ✅ CreateAsyncIOProviderForPlatform() 검증
- ✅ 주석 개선 (명확한 설명)

### Step 2: 문서 갱신
- ✅ 06_Cross_Platform_Architecture.md 갱신
  - PlatformType의 "플랫폼이 아니라 백엔드"임을 명확히
  - 폴백 우선순위 시각화

- ✅ 16_Fallback_Mechanisms.md 갱신
  - CreateAsyncIOProvider()의 exact flow 문서화
  - 각 플랫폼별 폴백 경로 명확화

### Step 3: 코드 주석 개선
- ✅ AsyncIOProvider.cpp 주석 강화
  - PlatformType의 의미 명확히
  - 각 폴백 단계 설명
  - preferHighPerformance 파라미터 설명

---

## 📊 영향도 분석

### 영향받는 파일

1. **AsyncIOProvider.cpp** ✅
   - 주석 개선 (로직은 올바름)
   - 타입 안전성 검증

2. **06_Cross_Platform_Architecture.md** ✅
   - PlatformType 설명 갱신
   - 플랫폼 선택 플로우 다이어그램 개선

3. **16_Fallback_Mechanisms.md** ✅
   - CreateAsyncIOProvider() 구현 예제 업데이트
   - 각 플랫폼별 폴백 체인 명확화

4. **AsyncIOProvider.h** ✅
   - PlatformType enum 주석 개선
   - "백엔드 구현"을 명시

---

## ✨ 수정 후 예상 결과

### Before (혼동)
```
CreateAsyncIOProvider() 
→ case PlatformType::IOCP:  // "이게 OS인가? 백엔드인가?"
```

### After (명확)
```
CreateAsyncIOProvider()
→ case PlatformType::IOCP:  // Windows 플랫폼의 기본 백엔드
→ RIO 시도 (고성능) → IOCP 폴백 (안정성) → nullptr (실패)
```

---

## 📝 체크리스트

- [ ] AsyncIOProvider.cpp 주석 강화
- [ ] 06_Cross_Platform_Architecture.md 갱신
- [ ] 16_Fallback_Mechanisms.md 갱신
- [ ] AsyncIOProvider.h 주석 갱신
- [ ] 커밋: "docs & fix: Platform 선택 논리 명확화 및 문서 갱신"
