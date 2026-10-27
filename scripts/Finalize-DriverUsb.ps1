#requires -Version 5.1
#requires -RunAsAdministrator
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$root=Split-Path -Parent $PSScriptRoot
$out=Join-Path $root 'out\test2-usb'
Start-Transcript -LiteralPath (Join-Path $out 'driver-finalization.log') -Append | Out-Null
try {
    if (Get-CimInstance Win32_Process -Filter 'ProcessId=53332' | Where-Object CommandLine -like '*Install-Test2Usb.ps1*') {throw 'Resource installer is still running'}
    $disk=Get-Disk -Number 1
    if ($disk.SerialNumber.Trim() -ne 'FC2425D40C3E1' -or $disk.Size -ne 31457280000 -or $disk.BusType -ne 'USB' -or $disk.IsBoot -or $disk.IsSystem -or $disk.PartitionStyle -ne 'GPT') {throw 'USB identity mismatch'}
    $parts=@(Get-Partition -DiskNumber 1)
    if (@($parts | Where-Object { $_.DriveLetter -eq 'P' -and $_.Size -eq 2GB }).Count -ne 1 -or @($parts | Where-Object DriveLetter -eq Q).Count -ne 1) {throw 'USB partition mismatch'}
    if (-not (Select-String -LiteralPath (Join-Path $root 'out\smoke-test3-unified-result.log') -SimpleMatch 'PASS: EFI/GRUB')) {throw 'Unified image has not passed EFI smoke test'}
    $manifest=Get-Content -LiteralPath (Join-Path $out 'driver-payload-manifest.json') -Raw | ConvertFrom-Json
    $validation=Get-Content -LiteralPath (Join-Path $root 'out\test3-validation.json') -Raw | ConvertFrom-Json
    if($validation.status -ne 'PASS' -or $validation.manifestSHA256 -ne (Get-FileHash -LiteralPath (Join-Path $out 'driver-payload-manifest.json') -Algorithm SHA256).Hash){throw 'Validation does not match this payload'}
    if (@($manifest.files).Count -ne 15) {throw 'Unexpected driver payload file count'}
    $existingLogs=@(Get-ChildItem -LiteralPath 'P:\logs' -Recurse -File | ForEach-Object { [pscustomobject]@{path=$_.FullName;hash=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash} })
    foreach($f in $manifest.files) {
        $volumeRoot=if($f.partition -eq 'boot'){'P:\'}elseif($f.partition -eq 'data'){'Q:\'}else{throw 'Unknown partition'}
        $src=[IO.Path]::GetFullPath((Join-Path $root $f.source))
        $dst=[IO.Path]::GetFullPath((Join-Path $volumeRoot $f.path))
        if (-not $src.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $dst.StartsWith($volumeRoot,[StringComparison]::OrdinalIgnoreCase)) {throw 'Path escaped expected root'}
        if ((Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash -ne $f.sha256) {throw "Source mismatch $($f.path)"}
        $same=(Test-Path -LiteralPath $dst) -and ((Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -eq $f.sha256)
        if (-not $same) {
            $parent=Split-Path -Parent $dst
            if(-not (Test-Path -LiteralPath $parent)){New-Item -ItemType Directory -Path $parent | Out-Null}
            Copy-Item -LiteralPath $src -Destination $dst -Force
        }
        if ((Get-Item -LiteralPath $dst).Length -ne $f.bytes -or (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -ne $f.sha256) {throw "USB readback mismatch $($f.path)"}
        Write-Output "Verified $($f.partition)/$($f.path)"
    }
    Copy-Item -LiteralPath (Join-Path $out 'driver-payload-manifest.json') -Destination 'P:\payload-manifest.json' -Force
    Copy-Item -LiteralPath (Join-Path $out 'driver-payload-manifest.json') -Destination 'Q:\payload-manifest.json' -Force
    Write-VolumeCache -DriveLetter P
    Write-VolumeCache -DriveLetter Q
    foreach($log in $existingLogs){if((Get-FileHash -LiteralPath $log.path -Algorithm SHA256).Hash -ne $log.hash){throw 'Existing USB log changed'}}
    $existingLogs | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'preserved-log-hashes.json') -Encoding UTF8
    [ordered]@{status='PASS';scope='Unified test3 driver diagnostics; Googlebook recovery deferred; existing USB logs preserved';utc=[DateTime]::UtcNow.ToString('o');serial=$disk.SerialNumber;verifiedFiles=15;unifiedTest=$true;manifestSHA256=(Get-FileHash -LiteralPath (Join-Path $out 'driver-payload-manifest.json') -Algorithm SHA256).Hash;partitions=@($parts | Select-Object PartitionNumber,DriveLetter,Size,GptType);volumes=@(Get-Volume -DriveLetter P,Q | Select-Object DriveLetter,FileSystemLabel,FileSystem,Size,SizeRemaining)} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'deployment-result.json') -Encoding UTF8
    Write-Output 'PASS: driver USB complete, all 15 files verified.'
} catch {
    [ordered]@{status='FAILED';utc=[DateTime]::UtcNow.ToString('o');error=($_ | Out-String)} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'deployment-result.json') -Encoding UTF8
    Write-Output ($_ | Out-String)
    exit 1
} finally {Stop-Transcript | Out-Null}
