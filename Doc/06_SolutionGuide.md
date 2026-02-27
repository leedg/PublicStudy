# 솔루션 가이드

## 솔루션 프로젝트
`NetworkModuleTest.sln`에 포함된 주요 프로젝트:
- ServerEngine (`Server/ServerEngine/ServerEngine.vcxproj`)
- TestServer (`Server/TestServer/TestServer.vcxproj`)
- TestDBServer (`Server/DBServer/TestDBServer.vcxproj`)
- TestClient (`Client/TestClient/TestClient.vcxproj`)
- DBModuleTest (`ModuleTest/DBModuleTest/DBModuleTest.vcxproj`)
- MultiPlatformNetwork (`ModuleTest/MultiPlatformNetwork/MultiPlatformNetwork.vcxproj`)

솔루션 폴더
- `1.Thirdparty`, `2.Lib`, `3.Server`, `8.Client`, `9.Test`, `ModuleTest`, `Documentation`

## 빌드 구성
- Debug/Release
- x64/x86

## 권장 빌드 순서
1. ServerEngine
2. TestDBServer
3. TestServer
4. TestClient
5. DBModuleTest, MultiPlatformNetwork (선택)

## CMake 현황
- 루트 CMake는 MultiPlatformNetwork만 빌드
- 다른 CMake는 참고용
