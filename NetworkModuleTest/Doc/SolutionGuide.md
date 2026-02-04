# Solution Guide

## Solution projects
`NetworkModuleTest.sln` includes:
- ServerEngine (`Server/ServerEngine/ServerEngine.vcxproj`)
- TestServer (`Server/TestServer/TestServer.vcxproj`)
- TestDBServer (`Server/DBServer/TestServer.vcxproj`)
- TestClient (`Client/TestClient/TestClient.vcxproj`)
- DBModuleTest (`ModuleTest/DBModuleTest/DBModuleTest.vcxproj`)
- MultiPlatformNetwork (`ModuleTest/MultiPlatformNetwork/MultiPlatformNetwork.vcxproj`)

Solution folders
- `1.Thirdparty`, `2.Lib`, `3.Server`, `8.Client`, `9.Test`, `ModuleTest`, `Documentation`

## Build configurations
- Debug/Release
- x64/x86

## Recommended build order
1. ServerEngine
2. TestDBServer
3. TestServer
4. TestClient
5. DBModuleTest, MultiPlatformNetwork (optional)

## CMake status
- Root CMake builds MultiPlatformNetwork only
- Other CMake files are reference
