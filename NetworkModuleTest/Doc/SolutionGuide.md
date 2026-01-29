# 통합 Solution 파일 생성 가이드

## 🎯 목표

NetworkModuleTest 프로젝트의 모든 모듈을 하나의 Solution으로 통합하여 개발 및 빌드를 용이하게 합니다.

## 📁 권장 Solution 구조

### Visual Studio Solution
```xml
NetworkModuleTest.sln
├── 📁 MultiPlatformNetwork          # 멀티플랫폼 네트워크 (보관용)
├── 📁 ServerEngine                  # 네트워크/DB/스트림 유틸리티 엔진
├── 📁 TestServer                   # 로직 처리 서버
├── 📁 DBServer                     # 데이터베이스 처리 서버
├── 📁 ClientNetwork                # 클라이언트 통신 모듈
├── 📁 UnitTests                    # 통합 테스트 프로젝트
└── 📁 Examples                     # 사용 예제
```

### CMake 상위 구조
```
NetworkModuleTest/
├── CMakeLists.txt                 # 메인 CMake
├── 📁 MultiPlatformNetwork/
│   └── CMakeLists.txt             # 보관용
├── 📁 Server/
│   ├── ServerEngine/
│   │   └── CMakeLists.txt
│   ├── TestServer/
│   │   └── CMakeLists.txt
│   └── DBServer/
│       └── CMakeLists.txt
├── 📁 Client/
│   └── Network/
│       └── CMakeLists.txt
└── 📁 Tests/
    └── CMakeLists.txt
```

## 🔧 구현 방법

### 1. Visual Studio Solution 생성

#### NetworkModuleTest.sln 구조
```xml
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1

Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "ServerEngine", "Server\ServerEngine\ServerEngine.vcxproj", "{GUID1}"
EndProject

Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "TestServer", "Server\TestServer\TestServer.vcxproj", "{GUID2}"
	ProjectSection(ProjectDependencies) = postProject
		{GUID1} = {GUID1}
	EndProjectSection
EndProject

Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "DBServer", "Server\DBServer\DBServer.vcxproj", "{GUID3}"
	ProjectSection(ProjectDependencies) = postProject
		{GUID1} = {GUID1}
	EndProjectSection
EndProject

Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "ClientNetwork", "Client\Network\ClientNetwork.vcxproj", "{GUID4}"
	ProjectSection(ProjectDependencies) = postProject
		{GUID1} = {GUID1}
	EndProjectSection
EndProject

Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
		Debug|x64 = Debug|x64
		Release|x64 = Release|x64
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
		{GUID1}.Debug|x64.ActiveCfg = Debug|x64
		{GUID1}.Debug|x64.Build.0 = Debug|x64
		{GUID2}.Debug|x64.ActiveCfg = Debug|x64
		{GUID2}.Debug|x64.Build.0 = Debug|x64
		{GUID3}.Debug|x64.ActiveCfg = Debug|x64
		{GUID3}.Debug|x64.Build.0 = Debug|x64
		{GUID4}.Debug|x64.ActiveCfg = Debug|x64
		{GUID4}.Debug|x64.Build.0 = Debug|x64
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
	GlobalSection(ExtensibilityGlobals) = postSolution
		SolutionGuid = {SOLUTION_GUID}
	EndGlobalSection
EndGlobal
```

### 2. 상위 CMakeLists.txt 생성

#### 메인 CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.15)
project(NetworkModuleTest VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Global include directories
include_directories(${CMAKE_SOURCE_DIR})

# 옵션
option(BUILD_SERVER_ENGINE "Build ServerEngine" ON)
option(BUILD_TEST_SERVER "Build TestServer" ON)
option(BUILD_DB_SERVER "Build DBServer" ON)
option(BUILD_CLIENT_NETWORK "Build ClientNetwork" ON)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" OFF)

# 의존성 찾기
find_package(Protobuf CONFIG)
find_package(Threads REQUIRED)

# 서브디렉토리 추가
if(BUILD_SERVER_ENGINE)
    add_subdirectory(Server/ServerEngine)
endif()

if(BUILD_TEST_SERVER)
    add_subdirectory(Server/TestServer)
endif()

if(BUILD_DB_SERVER)
    add_subdirectory(Server/DBServer)
endif()

if(BUILD_CLIENT_NETWORK)
    add_subdirectory(Client/Network)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(Examples)
endif()

# 설치 설정
install(DIRECTORY Doc/ 
        DESTINATION share/NetworkModuleTest/doc
        FILES_MATCHING PATTERN "*.md"
)
```

## 🔗 모듈 간 의존성

### 의존성 그래프
```
ClientNetwork ← ServerEngine (네트워크)
     ↑
TestServer ← ServerEngine (네트워크, 유틸리티)
     ↑
DBServer ← ServerEngine (네트워크, 데이터베이스)
     ↑
Tests ← 모든 모듈
```

### 공유 라이브러리
- **ServerEngine**: 다른 모듈들이 의존하는 핵심 라이브러리
- **NetworkUtils**: 유틸리티 함수 공유
- **Protocols**: 통신 프로토콜 정의

## 🏗️ 빌드 순서

### 올바른 빌드 순서
1. **ServerEngine** (가장 먼저 빌드)
2. **DBServer** (ServerEngine 의존)
3. **TestServer** (ServerEngine 의존)
4. **ClientNetwork** (ServerEngine 의존)
5. **Tests** (모든 모듈 의존)

### 개별 빌드
```bash
# ServerEngine만 빌드
cmake -DBUILD_SERVER_ENGINE=ON -DBUILD_TEST_SERVER=OFF -DBUILD_DB_SERVER=OFF ..

# 특정 모듈만 빌드
cmake --build . --target ServerEngine
cmake --build . --target TestServer
```

## 🎛️ 설정 옵션

### 빌드 옵션
- **BUILD_SERVER_ENGINE**: ServerEngine 빌드 여부 (기본값: ON)
- **BUILD_TEST_SERVER**: TestServer 빌드 여부 (기본값: ON)
- **BUILD_DB_SERVER**: DBServer 빌드 여부 (기본값: ON)
- **BUILD_CLIENT_NETWORK**: ClientNetwork 빌드 여부 (기본값: ON)
- **BUILD_TESTS**: 테스트 빌드 여부 (기본값: ON)
- **BUILD_EXAMPLES**: 예제 빌드 여부 (기본값: OFF)

### 플랫폼 옵션
- **PLATFORM_WINDOWS**: Windows 플랫폼 (자동 감지)
- **PLATFORM_LINUX**: Linux 플랫폼 (자동 감지)
- **PLATFORM_MACOS**: macOS 플랫폼 (자동 감지)

## 📦 배포

### 패키징 구조
```
NetworkModuleTest-1.0.0/
├── bin/
│   ├── TestServer.exe
│   ├── DBServer.exe
│   └── ClientTest.exe
├── lib/
│   ├── libServerEngine.a
│   └── libNetworkUtils.a
├── include/
│   └── NetworkModule/
├── config/
│   └── default.json
└── doc/
    └── *.md
```

### 설치 스크립트
```bash
# 리눅스/macOS
./install.sh --prefix=/usr/local

# Windows
install.bat --directory="C:\NetworkModuleTest"
```

---

*이 가이드는 실제 Solution 파일 생성 시 참고용으로 사용됩니다.*