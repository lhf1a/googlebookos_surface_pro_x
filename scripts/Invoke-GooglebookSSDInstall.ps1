#requires -RunAsAdministrator
param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
$status=Join-Path $root 'out/ssd/install-status.json'
Start-Transcript -LiteralPath (Join-Path $root 'out/ssd/install.log') -Append | Out-Null
try {
    @{state='running';started_at=(Get-Date -Format o)} | ConvertTo-Json | Set-Content -LiteralPath $status -Encoding UTF8
    $targets=@(Get-Disk | Where-Object {$_.UniqueId -eq '3001237923792379' -and $_.BusType -eq 'USB' -and $_.Size -eq 128035676160})
    if($targets.Count -ne 1){throw 'Expected exactly one identified Realtek SSD'}
    & (Join-Path $PSScriptRoot 'Install-GooglebookSSD.ps1') -DiskNumber $targets[0].Number -VerifyOnly:$VerifyOnly
    @{state='complete';finished_at=(Get-Date -Format o)} | ConvertTo-Json | Set-Content -LiteralPath $status -Encoding UTF8
} catch {
    @{state='failed';failed_at=(Get-Date -Format o);error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath $status -Encoding UTF8
    Write-Output $_
    exit 1
} finally { Stop-Transcript | Out-Null }
