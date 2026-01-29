# Visual Studio Version Selector Guide

이 프로젝트는 여러 버전의 Visual Studio를 지원합니다. 설치된 Visual Studio 버전에 따라 적절한 솔루션 파일을 선택하세요.

## 📁 사용 가능한 솔루션 파일

| 솔루션 파일 | Visual Studio 버전 | 비고 |
|-------------|-------------------|------|
| `DocDBModule.sln` | **Visual Studio 2022** (v17) | 권장 - 최신 기능 지원 |
| `DocDBModule_vs2015.sln` | **Visual Studio 2015** (v14) | 안정적 버전 |
| `DocDBModule_vs2010.sln` | **Visual Studio 2010** (v11) | 기본 호환성 |

## 🚀 빠른 시작

### 방법 1: 자동 선택 (권장)
```
DocDBModule.sln
```
- Visual Studio 2022, 2019, 2017에서 자동으로 열림
- Visual Studio Version Selector가 최적의 버전을 선택

### 방법 2: 수동 선택
설치된 Visual Studio 버전에 맞는 파일을 선택:

**Visual Studio 2022/2019/2017 사용자:**
```bash
start DocDBModule.sln
```

**Visual Studio 2015 사용자:**
```bash
start DocDBModule_vs2015.sln
```

**Visual Studio 2010-2013 사용자:**
```bash
start DocDBModule_vs2010.sln
```

## ⚙️ 버전별 특징

### Visual Studio 2022 (v17)
- ✅ C++20/17 전체 기능 지원
- ✅ 최신 IntelliSense
- ✅ 통합 터미널
- ✅ Git 통합 강화
- ✅ 성능 분석기 개선

### Visual Studio 2015 (v14)
- ✅ C++11/14/17 기본 지원
- ✅ 안정적인 빌드 시스템
- ✅ 대부분의 Windows 환경에서 호환

### Visual Studio 2010 (v11)
- ✅ 기본 C++11 지원
- ✅ 구형 시스템 호환성
- ⚠️ 일부 최신 기능 제한

## 🔧 빌드 요구사항

### 공통 요구사항
- Windows 7 이상
- Visual Studio C++ 빌드 도구
- Windows SDK 10.0

### Visual Studio 2022
- Visual Studio 2022 Community/Professional/Enterprise
- Windows 10 SDK (포함)

### Visual Studio 2015
- Visual Studio 2015 Community/Professional/Enterprise
- Windows 8.1 SDK 이상
- Update 3 이상 권장

### Visual Studio 2010
- Visual Studio 2010 Professional/Premium/Ultimate
- Windows 7 SDK 이상
- SP1 설치 권장

## 🛠️ 플랫폼 지원

| Visual Studio 버전 | Win32 | x64 | ARM64 |
|-------------------|-------|-----|-------|
| VS 2022 | ✅ | ✅ | ✅ |
| VS 2015 | ✅ | ✅ | ❌ |
| VS 2010 | ✅ | ✅ | ❌ |

## 🔍 문제 해결

### "이 프로젝트를 열 수 없습니다" 오류
1. **Visual Studio 버전 확인**: 설치된 버전과 솔루션 파일 버전 확인
2. **구성 요소 설치**: C++ 데스크톱 개발 워크로드 설치
3. **SDK 설치**: 해당 Windows SDK 버전 설치

### "C++ 표준 오류" 발생 시
- VS 2022/2015: `/std:c++17` 옵션으로 자동 설정
- VS 2010: 프로젝트 속성에서 C++11 수동 활성화

### "intellisense 오류" 발생 시
1. 솔루션 닫기 후 다시 열기
2. IntelliSense 캐시 삭제: `Delete *.sdf`
3. 프로젝트 다시 빌드

## 📞 지원

문제 발생 시:
1. **솔루션 파일 버전 확인**: 설치된 VS와 맞는지
2. **업데이트 설치**: Visual Studio 최신 업데이트
3. **커뮤니티 지원**: Microsoft Docs 또는 Stack Overflow

---

**팁**: 가장 좋은 경험을 위해 Visual Studio 2022 Community 버전(무료) 사용을 권장합니다.