#requires -RunAsAdministrator
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Start-Transcript -LiteralPath (Join-Path $root 'out/ssd/connection-recovery.log') -Append | Out-Null
try {
    $disk=Get-Disk -Number 1
    if($disk.UniqueId -ne '3001237923792379' -or $disk.Size -ne 128035676160 -or $disk.BusType -ne 'USB' -or $disk.IsBoot -or $disk.IsSystem){throw 'SSD identity mismatch'}
    $child='SCSI\DISK&VEN_REALTEK&PROD_RTL9210B-CG\9&27E2EB8B&0&000000'
    $bridge='USB\VID_0BDA&PID_9210\MSFT30012345678938'
    if((Get-PnpDeviceProperty -InstanceId $child -KeyName DEVPKEY_Device_Parent).Data -ne $bridge){throw 'USB bridge identity mismatch'}
    $process=Get-CimInstance Win32_Process -Filter 'ProcessId=64700'
    if($process -and $process.CommandLine -like '*Invoke-GooglebookSSDInstall.ps1*'){Stop-Process -Id 64700 -Force}
    @{state='connection_recovery';note='Writes flushed; readback blocked by UASP reset. Read-only verification must resume.'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'out/ssd/install-status.json') -Encoding UTF8
    & pnputil.exe /restart-device $bridge
    if($LASTEXITCODE -ne 0){throw "USB restart exit $LASTEXITCODE"}
} finally {Stop-Transcript | Out-Null}
