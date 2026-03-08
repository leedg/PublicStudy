# DBModuleTest (요약)

`Network::Database` 모듈(ODBC / OLE DB / SQLite)을 Docker 없이 로컬에서 수동 검증하는 독립 실행형 테스트 도구.

- 지원 백엔드: SQLite (내장) · MSSQL ODBC · PostgreSQL ODBC · MySQL ODBC · OLE DB (SQL Server)
- 테스트 결과: SQLite 23/23 PASS · ODBC/OLE DB 22/22 PASS
- 빌드·실행: `.\scripts\run-db-tests.ps1 -Backend sqlite -Build`
- 솔루션: `NetworkModuleTest.sln` (권장) 또는 `DocDBModule.sln` (독립)
- 상세: `Doc/README.md`
