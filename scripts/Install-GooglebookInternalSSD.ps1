#requires -RunAsAdministrator
param([int]$DiskNumber=1)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$out=Join-Path $root 'out/internal-ssd'
$status=Join-Path $out 'install-status.json'
Start-Transcript -LiteralPath (Join-Path $out 'install.log') -Append | Out-Null
$source=$null; $target=$null
function Save-State($state,$extra) {
    [ordered]@{state=$state;time=(Get-Date -Format o);detail=$extra} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $status -Encoding utf8
}
function Assert-Target {
    $d=Get-Disk -Number $DiskNumber
    if($d.IsBoot -or $d.IsSystem -or $d.BusType -ne 'USB' -or $d.Size -ne 512110190592 -or $d.LogicalSectorSize -ne 512 -or $d.FriendlyName -ne 'Realtek RTL9210B-CG' -or $d.UniqueId -ne '3001237923792379') {
        throw 'Target identity mismatch: expected the authorized external 512 GB Realtek SSD'
    }
    return $d
}
try {
    Save-State 'preflight' 'Checking target, image and NVMe smoke result'
    $disk=Assert-Target
    $manifest=Get-Content -LiteralPath (Join-Path $out 'image-manifest.json') -Raw | ConvertFrom-Json
    $validation=Get-Content -LiteralPath (Join-Path $out 'validation.json') -Raw | ConvertFrom-Json
    $layout=Get-Content -LiteralPath (Join-Path $out 'layout.json') -Raw | ConvertFrom-Json
    if($manifest.image_bytes -ne $disk.Size -or $manifest.identity -ne 'SQ2-INTERNAL-512-20261027') {throw 'Image/target mismatch'}
    if(-not $validation.nvme_partition_selection -or -not $validation.android_init_handoff -or -not $validation.physical_userdata_mounted) {throw 'NVMe boot-path preflight failed'}
    foreach($name in @('Image','sc8180x-surface-pro-x.dtb','native-combined-initramfs.cpio.gz')) {
        $hash=(Get-FileHash -LiteralPath (Join-Path $out $name)).Hash.ToLowerInvariant()
        if($hash -ne $manifest.boot_payload_sha256.$name -or $hash -ne $validation.payload_sha256.$name) {throw "Payload changed after smoke check: $name"}
    }
    if((Get-Item -LiteralPath $manifest.windows_image).Length -ne $disk.Size) {throw 'Source image size mismatch'}
    foreach($e in $manifest.extents) {
        if($e.offset -lt 0 -or $e.bytes -le 0 -or $e.offset+$e.bytes -gt $disk.Size -or $e.offset%512 -or $e.bytes%512) {throw 'Invalid write extent'}
    }
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Security.Cryptography;
public static class Sq2InternalWriter {
 public static string Region(FileStream src, FileStream dst, long offset, long count) {
  src.Position=offset; if(dst!=null) dst.Position=offset;
  byte[] buffer=new byte[4*1024*1024];
  using(var hash=SHA256.Create()) {
   while(count>0) {
    int n=src.Read(buffer,0,(int)Math.Min(count,buffer.Length));
    if(n<=0) throw new EndOfStreamException();
    hash.TransformBlock(buffer,0,n,null,0);
    if(dst!=null) dst.Write(buffer,0,n);
    count-=n;
   }
   hash.TransformFinalBlock(new byte[0],0,0);
   return BitConverter.ToString(hash.Hash).Replace("-", "").ToLowerInvariant();
  }
 }
}
'@
    $source=[IO.File]::Open($manifest.windows_image,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $null=Assert-Target
    Set-Disk -Number $DiskNumber -IsOffline $true
    $null=Assert-Target
    Set-Disk -Number $DiskNumber -IsReadOnly $false
    if((Assert-Target).IsReadOnly) {throw 'Target remains write-protected'}
    $target=[IO.File]::Open("\\.\PhysicalDrive$DiskNumber",[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite)
    $done=0L; $watch=[Diagnostics.Stopwatch]::StartNew(); $last=0
    Save-State 'writing' @{done=0;total=$manifest.written_bytes;disk=$DiskNumber}
    foreach($e in $manifest.extents) {
        if([Sq2InternalWriter]::Region($source,$target,$e.offset,$e.bytes) -ne $e.sha256) {throw "Source changed at $($e.offset)"}
        $done+=$e.bytes
        if($watch.Elapsed.TotalSeconds-$last -ge 10) {
            Write-Output "WRITE $done / $($manifest.written_bytes) bytes"
            Save-State 'writing' @{done=$done;total=$manifest.written_bytes;seconds=[int]$watch.Elapsed.TotalSeconds}
            $last=$watch.Elapsed.TotalSeconds
        }
    }
    $target.Flush($true)
    Save-State 'written' 'All initialized filesystem and system regions flushed; checking critical disk metadata'
    # Bounded readback: avoid the prior bridge's stalled long sequential read.
    $esp=@($layout.partitions | Where-Object name -eq 'SQ2PROBE')[0]
    $userdata=@($layout.partitions | Where-Object name -eq 'userdata')[0]
    $ranges=@(@{offset=0L;bytes=1048576L},@{offset=[long]$esp.offset;bytes=16777216L},@{offset=[long]$userdata.offset;bytes=65536L},@{offset=($disk.Size-16896L);bytes=16896L})
    foreach($r in $ranges) {
        $expected=[Sq2InternalWriter]::Region($source,$null,$r.offset,$r.bytes)
        if([Sq2InternalWriter]::Region($target,$null,$r.offset,$r.bytes) -ne $expected) {throw "Critical metadata readback failed at $($r.offset)"}
    }
    $result=[ordered]@{disk_number=$DiskNumber;bridge_id=$disk.UniqueId;capacity_bytes=$disk.Size;written_bytes=$done;
        partition_count=$manifest.physical_partitions;userdata_bytes=$manifest.userdata_bytes;
        payload_sha256=$manifest.boot_payload_sha256;critical_metadata_readback=$true;full_readback=$false;
        workstation_internal_disk_touched=$false;surface_nvme_hardware_boot_verified=$false;audio_crash_resolved=$false;
        finished_at=(Get-Date -Format o)}
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'deployment.json') -Encoding utf8
    Save-State 'complete' $result
    Write-Output 'COMPLETE: 512 GB replacement system disk flushed; left offline for removal.'
} catch {
    Save-State 'failed' $_.Exception.Message
    Write-Output $_
    exit 1
} finally {
    if($target) {$target.Dispose()}
    if($source) {$source.Dispose()}
    Stop-Transcript | Out-Null
}
