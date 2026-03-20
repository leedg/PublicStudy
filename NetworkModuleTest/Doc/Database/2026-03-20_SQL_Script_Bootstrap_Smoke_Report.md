# SQL Script Bootstrap Smoke Report

Date: 2026-03-20 13:38:11 +09:00

Branch: `codex/sp-file-db-processing`

Commit: `74e49e41c7a968b249f468227aeb6389c921ed79`

## Scope

This report validates the file-based SQL flow added for:

- `DB/TABLE`
- `DB/SP`
- `DB/RAW`
- vendor branch resolution for `MySQL`, `SQLServer`, `PostgreSQL`
- coexistence of SP/file execution and direct raw SQL execution
- first-bootstrap create + second-attach manifest verification behavior

The main runtime coverage in this report is `TestServer` DB access. `TestDBServer.exe` was also started as a regression smoke, but this run did not execute DBServer against an external MySQL / SQL Server / PostgreSQL instance.

## What Was Executed

1. Solution build
   Command: `powershell -ExecutionPolicy Bypass -File .\scripts\build_windows.ps1 -Configuration Debug -Platform x64`
2. Smoke harness build
   Output: `scripts\db_tests\build_smoke\testserver_bootstrap_smoke.exe`
3. `TestServer.exe --self-test`
   Purpose: verify Mock-backed `DBTaskQueue` bootstrap path still initializes and self-test passes.
4. `TestDBServer.exe` short startup smoke
   Purpose: verify no startup regression after DB config / dialect changes.
5. MySQL Docker smoke
6. SQL Server Docker smoke
7. PostgreSQL Docker smoke
8. Docker Desktop shutdown after testing

## Backend Smoke Procedure

Each DB smoke used the same harness flow:

1. Start a Docker container for the target DB.
2. Wait until the DB is ready.
3. Create test database `codex_sp_smoke`.
4. Run `testserver_bootstrap_smoke.exe` with an ODBC connection string and dialect hint.
5. Validate the following sequence:
   - first attach prints `Initial SQL bootstrap completed`
   - `SaveUserLoginEvent()` succeeds through `DB/SP`
   - `LoadUserProfileData()` succeeds through `DB/SP`
   - direct raw string update succeeds through `ExecuteCustomSqlQuery()`
   - bound raw file update succeeds through `DB/RAW/RQ_UpdateUserProfileName.sql`
   - `PersistPlayerGameState()` succeeds
   - second attach prints `SQL manifest verified`
6. Remove the container.

The bound raw file used in the smoke was:

- [RQ_UpdateUserProfileName.sql](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Server/TestServer/DB/RAW/RQ_UpdateUserProfileName.sql)

The smoke harness used vendor-specific script folders through dialect resolution:

- `DB/TABLE/MySQL`, `DB/SP/MySQL`
- `DB/TABLE/SQLServer`, `DB/SP/SQLServer`
- `DB/TABLE/PostgreSQL`, `DB/SP/PostgreSQL`

## Results

| Area | Result | Evidence |
|---|---|---|
| Solution build | PASS | `01_build_windows.txt` |
| TestServer self-test | PASS | `DBTaskQueue: initial SQL bootstrap completed`, self-test pass log |
| TestDBServer startup smoke | PASS | startup log reached `TestDBServer is running` |
| MySQL smoke | PASS | first bootstrap, raw string, raw file, manifest verify all passed |
| SQL Server smoke | PASS | first bootstrap, raw string, raw file, manifest verify all passed |
| PostgreSQL smoke | PASS | first bootstrap, raw string, raw file, manifest verify all passed |
| Docker shutdown | PASS | daemon stopped after test run |

## Current Metrics

These are the main numeric indicators that can be shown for this run.

| Metric | Value | Basis |
|---|---|---|
| External DB backend pass rate | `3 / 3` (`100%`) | MySQL, SQL Server, PostgreSQL smoke logs |
| First bootstrap success rate | `3 / 3` (`100%`) | `Initial SQL bootstrap completed` on all external DB backends |
| Second attach manifest verify rate | `3 / 3` (`100%`) | `SQL manifest verified` on all external DB backends |
| Direct raw string execution coverage | `3 / 3` (`100%`) | `ExecuteCustomSqlQuery()` path validated on all external DB backends |
| Bound raw file execution coverage | `3 / 3` (`100%`) | `DB/RAW/RQ_UpdateUserProfileName.sql` path validated on all external DB backends |
| SP-based persistence coverage | `3 / 3` (`100%`) | login/profile/game-state SP paths validated on all external DB backends |
| Local regression runtime checks | `2 / 2` (`100%`) | `TestServer --self-test`, `TestDBServer` startup smoke |
| Core validation run time | `119s` | from `00_environment.txt` start window to `08_docker_shutdown.txt` |
| Solution build time | `13s` | `01_build_windows.txt` file timestamp window |
| Smoke harness build time | `27s` | `02_build_testserver_bootstrap_smoke.txt` file timestamp window |
| TestServer self-test time | `1s` | `03_testserver_self_test.txt` file timestamp window |
| TestDBServer startup smoke time | `5s` | `04_dbserver_startup.txt` file timestamp window |
| MySQL smoke time | `17s` | `05_mysql_smoke.txt` file timestamp window |
| SQL Server smoke time | `33s` | `06_sqlserver_smoke.txt` file timestamp window |
| PostgreSQL smoke time | `6s` | `07_postgresql_smoke.txt` file timestamp window |
| Docker cleanup state | `Stopped` | shutdown verification log |

## Good Dashboard Candidates

If this needs to be surfaced continuously in CI or an internal dashboard, the most useful top-line indicators are:

- backend pass rate by DB vendor
- first-bootstrap success rate
- second-attach manifest verification rate
- raw string execution coverage rate
- raw file execution coverage rate
- end-to-end smoke duration by vendor
- Docker cleanup success rate

## Next Metrics To Add

The current logs are enough for coarse smoke metrics. If you want richer operational indicators, the next useful additions are:

- per-script execution time for each `DB/TABLE`, `DB/SP`, `DB/RAW` file
- manifest hash value recorded in the report for each module
- container readiness time by backend
- row-count assertions emitted explicitly in logs
- bootstrap path split by module
  - `TestServer`
  - `DBServer`
- failure classification counters
  - driver install failure
  - connection failure
  - DDL failure
  - manifest mismatch

## Raw Log Location

All raw logs for this run were saved under:

- [20260320_133811_sql_script_bootstrap](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap)

Important files:

- [00_environment.txt](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap/00_environment.txt)
- [01_build_windows.txt](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap/01_build_windows.txt)
- [02_build_testserver_bootstrap_smoke.txt](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap/02_build_testserver_bootstrap_smoke.txt)
- [03_testserver_self_test.txt](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap/03_testserver_self_test.txt)
- [04_dbserver_startup.txt](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap/04_dbserver_startup.txt)
- [05_mysql_smoke.txt](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap/05_mysql_smoke.txt)
- [06_sqlserver_smoke.txt](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap/06_sqlserver_smoke.txt)
- [07_postgresql_smoke.txt](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap/07_postgresql_smoke.txt)
- [09_docker_shutdown_check.txt](/E:/MyGitHub/PublicStudy4/NetworkModuleTest/Doc/Database/Logs/20260320_133811_sql_script_bootstrap/09_docker_shutdown_check.txt)

## Notes

- The smoke harness prints sample data such as `codex_mysql` on every backend. That string is only test payload text, not a backend identifier.
- PostgreSQL validation in this run used the installed ODBC driver `PostgreSQL Unicode(x64)`.
- SQL Server validation used the installed 64-bit SQL Server ODBC driver selected by the harness.
- The final Docker shutdown check intentionally returns a connection failure because the daemon has already been stopped. That failure is used here as evidence that Docker Desktop was shut down after testing.
