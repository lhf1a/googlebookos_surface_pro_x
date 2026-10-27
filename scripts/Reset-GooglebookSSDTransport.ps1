#requires -RunAsAdministrator
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Start-Transcript -LiteralPath (Join-Path $root 'out/ssd/transport-reset.log') -Append | Out-Null
try {
    # These are this task's verified installer/recovery process IDs. Avoid a
    # storage-provider query: it is blocked behind the USB request being reset.
    foreach($taskProcessId in @(64700,6136)) {
        $p=Get-Process -Id $taskProcessId -ErrorAction SilentlyContinue
        if($p -and $p.ProcessName -eq 'powershell' -and $p.StartTime -gt (Get-Date '2026-10-27 11:36:00') -and $p.StartTime -lt (Get-Date '2026-10-27 11:44:00')) {
            $p.Kill()
            Write-Output "Cancelled task process $taskProcessId"
        }
    }
    @{state='connection_recovery';note='Writes flushed; cancelling stalled read and resetting the verified Realtek USB bridge.'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'out/ssd/install-status.json') -Encoding UTF8
    # Verified parent of the external SSD, not the root hub or host controller.
    & pnputil.exe /restart-device 'USB\VID_0BDA&PID_9210\MSFT30012345678938'
    Write-Output "Restart exit=$LASTEXITCODE"
} finally {Stop-Transcript | Out-Null}
