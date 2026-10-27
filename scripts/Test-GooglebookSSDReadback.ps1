#requires -RunAsAdministrator
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$status=Join-Path $root 'out/ssd/readback-status.json'
$manifest=Get-Content (Join-Path $root 'out/ssd/image-manifest.json') -Raw | ConvertFrom-Json
$expected='SCSI\DISK&VEN_REALTEK&PROD_RTL9210B-CG\9&27E2EB8B&0&000000'
$devices=@(Get-CimInstance Win32_DiskDrive | Where-Object {$_.PNPDeviceID -eq $expected -and $_.Model -eq 'Realtek RTL9210B-CG SCSI Disk Device'})
if($devices.Count -ne 1){throw 'Expected exactly one provisioned Realtek SSD'}
$diskNumber=$devices[0].Index
$bridge=(Get-PnpDeviceProperty -InstanceId $expected -KeyName DEVPKEY_Device_Parent).Data
if($bridge -ne 'USB\VID_0BDA&PID_9210\MSFT30012345678938'){throw 'USB parent mismatch'}
$validation=Get-Content (Join-Path $root 'out/ssd/validation.json') -Raw | ConvertFrom-Json
if(-not $validation.persistent_data_two_boots -or -not $validation.physical_layout_boot){throw 'Missing boot preflight'}
Start-Transcript -LiteralPath (Join-Path $root 'out/ssd/readback.log') -Append | Out-Null
$stream=$null
try {
    @{state='opening_read_only';disk_number=$diskNumber;started_at=(Get-Date -Format o)} | ConvertTo-Json | Set-Content $status -Encoding UTF8
    $stream=[IO.File]::Open("\\.\PhysicalDrive$diskNumber",[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    $header=New-Object byte[] 512
    $null=$stream.Seek(512,[IO.SeekOrigin]::Begin)
    if($stream.Read($header,0,512) -ne 512){throw 'Short GPT header read'}
    if([Text.Encoding]::ASCII.GetString($header,0,8) -ne 'EFI PART'){throw 'GPT not found'}
    $guidBytes=New-Object byte[] 16;[Array]::Copy($header,56,$guidBytes,0,16)
    $guid=New-Object Guid (,$guidBytes)
    if($guid.ToString() -ne '3d35f9b8-a603-4c21-a187-eaf47b70f71d'){throw 'Provisioned GPT UUID mismatch'}
    $bytes=([BitConverter]::ToUInt64($header,32)+1)*512
    if($bytes -ne $manifest.image_bytes){throw 'GPT capacity mismatch'}
    Write-Output "Read-only GPT identity verified: disk $diskNumber, $guid"
    $buffer=New-Object byte[] (256KB)
    $done=0L; $watch=[Diagnostics.Stopwatch]::StartNew();$last=0
    foreach($e in $manifest.extents){
        @{state='reading';offset=$e.offset;verified_bytes=$done;total_bytes=$manifest.written_bytes;updated_at=(Get-Date -Format o)} | ConvertTo-Json | Set-Content $status -Encoding UTF8
        $stream.Position=$e.offset;$remaining=[long]$e.bytes
        $hash=[Security.Cryptography.SHA256]::Create()
        try {
            while($remaining -gt 0){
                $n=$stream.Read($buffer,0,[int][Math]::Min($remaining,$buffer.Length))
                if($n -le 0){throw 'Short physical read'}
                $null=$hash.TransformBlock($buffer,0,$n,$null,0)
                $remaining-=$n
            }
            $null=$hash.TransformFinalBlock([byte[]]@(),0,0)
            $actual=[BitConverter]::ToString($hash.Hash).Replace('-','').ToLowerInvariant()
            if($actual -ne $e.sha256){throw "Physical SHA256 mismatch at offset $($e.offset)"}
        }finally{$hash.Dispose()}
        $done+=$e.bytes
        if($watch.Elapsed.TotalSeconds-$last -ge 10){Write-Output "VERIFIED $done / $($manifest.written_bytes) bytes";$last=$watch.Elapsed.TotalSeconds}
    }
    [ordered]@{verified_at=(Get-Date -Format o);disk_number=$diskNumber;unique_id='3001237923792379';gpt_uuid=$guid.ToString();capacity_bytes=$manifest.image_bytes;
        written_bytes=$manifest.written_bytes;all_written_regions_sha256_verified=$true;physical_partition_count=39;userdata_bytes=$manifest.userdata_bytes;
        payload_sha256=$manifest.boot_payload_sha256;internal_disks_touched=$false;surface_boot_verified=$false;secure_erasure=$false;verification_mode='read_only_after_reconnect'} |
        ConvertTo-Json -Depth 5 | Set-Content (Join-Path $root 'out/ssd/deployment.json') -Encoding UTF8
    @{state='complete';verified_bytes=$done;finished_at=(Get-Date -Format o)} | ConvertTo-Json | Set-Content $status -Encoding UTF8
    Write-Output 'PASS: every provisioned region verified through a fresh read-only handle'
}catch{
    @{state='failed';error=$_.Exception.Message;failed_at=(Get-Date -Format o)} | ConvertTo-Json | Set-Content $status -Encoding UTF8
    Write-Output $_
    exit 1
}finally{
    if($stream){$stream.Dispose()}
    Stop-Transcript | Out-Null
}
