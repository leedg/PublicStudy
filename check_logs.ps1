Write-Host "=== Server last output ===" -ForegroundColor Cyan
Get-Content "$env:TEMP\server_out.txt" -ErrorAction SilentlyContinue | Select-Object -Last 20
Write-Host "=== DBServer last output ===" -ForegroundColor Cyan
Get-Content "$env:TEMP\dbserver_out.txt" -ErrorAction SilentlyContinue | Select-Object -Last 20
Write-Host "=== Client1 last output ===" -ForegroundColor Green
Get-Content "$env:TEMP\client1_out.txt" -ErrorAction SilentlyContinue | Select-Object -Last 10
Write-Host "=== Client2 last output ===" -ForegroundColor Green
Get-Content "$env:TEMP\client2_out.txt" -ErrorAction SilentlyContinue | Select-Object -Last 10
