# DBModuleTest

DBModuleTest는 독립 DB 모듈 테스트 프로젝트입니다.
DocDBModule 네임스페이스와 소문자 API를 사용합니다.
신규 개발은 `Server/ServerEngine/Database` 모듈 사용을 권장합니다.

## 구성
```text
ModuleTest/DBModuleTest/
  IDatabase.h
  DatabaseFactory.*
  ODBCDatabase.*
  OLEDBDatabase.*
  ConnectionPool.*
  odbc_sample.cpp
  oledb_sample.cpp
  test_database.cpp
  scripts/
    check-db-prereqs.ps1
    run-db-tests.ps1
  DBModuleTest.vcxproj
  Doc/
```

## 빌드
- 권장: `NetworkModuleTest.sln`에서 `DBModuleTest` 빌드
- 단독: `ModuleTest/DBModuleTest/DBModuleTest.vcxproj` 열기
- 실행 파일(`db_tests`, `odbc_sample`, `oledb_sample`)은 CMake 타깃으로 빌드

## DB 테스트 가이드 (MSSQL + MySQL)

### 1) 사전 점검

MSSQL만 점검:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check-db-prereqs.ps1 -DbTarget Mssql
```

MySQL만 점검:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check-db-prereqs.ps1 -DbTarget Mysql
```

둘 다 점검:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check-db-prereqs.ps1 -DbTarget Both
```

DB endpoint까지 필수로 확인하려면 `-RequireDatabase`를 추가합니다.

### 2) 테스트 실행

MSSQL 테스트:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1 -DbTarget Mssql
```

MySQL 테스트:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1 -DbTarget Mysql
```

MSSQL + MySQL 동시 테스트:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1 -DbTarget Both
```

샘플 실행까지 포함:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1 -DbTarget Both -RunSamples
```

DB 접속 실패를 테스트 실패로 강제:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1 -DbTarget Both -RequireDatabase
```

### 3) 연결 문자열 지정

환경변수:
- `DOCDB_ODBC_CONN_MSSQL`
- `DOCDB_ODBC_CONN_MYSQL`
- `DOCDB_OLEDB_CONN_MSSQL`
- `DOCDB_REQUIRE_DB` (`1`이면 연결 실패 시 실패 처리)

실행 인자:
- `-MssqlOdbcConnectionString`
- `-MysqlOdbcConnectionString`
- `-MssqlOledbConnectionString`

예시:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-db-tests.ps1 -DbTarget Both `
  -MssqlOdbcConnectionString 'DRIVER={ODBC Driver 18 for SQL Server};SERVER=(localdb)\MSSQLLocalDB;DATABASE=master;Trusted_Connection=Yes;Encrypt=no' `
  -MysqlOdbcConnectionString 'DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=mysql;USER=root;PASSWORD=1234;OPTION=3' `
  -MssqlOledbConnectionString 'Provider=MSOLEDBSQL;Server=(localdb)\MSSQLLocalDB;Database=master;Integrated Security=SSPI;Encrypt=Optional'
```

## 서버 실행 방법

### MSSQL 실행

#### A. LocalDB 사용 (간단)
```powershell
sqllocaldb i
sqllocaldb start MSSQLLocalDB
```

연결 예시:
- ODBC: `SERVER=(localdb)\MSSQLLocalDB`
- OLEDB: `Server=(localdb)\MSSQLLocalDB`

#### B. SQL Server 서비스 사용
```powershell
Get-Service | Where-Object { $_.Name -like 'MSSQL*' }
Start-Service MSSQLSERVER
```

연결 예시:
- `SERVER=localhost;DATABASE=master;Trusted_Connection=Yes`

### MySQL 실행

#### A. 로컬 서비스 사용
```powershell
Get-Service | Where-Object { $_.Name -like '*mysql*' -or $_.DisplayName -like '*MySQL*' }
Start-Service MySQL80
```

#### B. Docker 사용
```powershell
docker run --name mysql-test -e MYSQL_ROOT_PASSWORD=1234 -e MYSQL_DATABASE=mysql -p 3306:3306 -d mysql:8
```

연결 예시:
- `DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=mysql;USER=root;PASSWORD=1234;OPTION=3`

## 참고
- ServerEngine DB 모듈은 PascalCase API 사용 (`Connect`, `CreateStatement` 등)
- DBModuleTest는 레거시/테스트 용도로 유지