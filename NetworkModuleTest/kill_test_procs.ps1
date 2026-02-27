$procs = Get-Process -Name TestDBServer,TestServer,TestClient -ErrorAction SilentlyContinue
if ($procs) {
    $procs | Select-Object Id,Name,CPU,WorkingSet | Format-Table -AutoSize
    $procs | Stop-Process -Force
    Write-Host "Killed $($procs.Count) process(es)"
} else {
    Write-Host "No remaining processes found"
}
