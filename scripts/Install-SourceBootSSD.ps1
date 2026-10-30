param([Parameter(Mandatory=$true)][string]$ConfirmedDeviceId,[switch]$VerifyOnly)
$ErrorActionPreference='Stop'
if($ConfirmedDeviceId -ne '3001237923792379'){throw 'Erase consent does not match the authorized target'}
$candidates=@(Get-Disk | Where-Object {$_.UniqueId -eq $ConfirmedDeviceId -and $_.BusType -eq 'USB' -and -not $_.IsBoot -and -not $_.IsSystem})
if($candidates.Count -ne 1){throw 'Expected exactly one authorized USB target'}
$DiskNumber=$candidates[0].Number
$root=Split-Path -Parent $PSScriptRoot
$manifest=Get-Content -LiteralPath (Join-Path $root 'out/source-boot/image-manifest.json') -Raw | ConvertFrom-Json
$expected=@{
 'Image'='b4cf88a82fa03d227fa495c9bae76ca5f45832627acc564ec68b93385bd9e0b8'
 'sc8180x-surface-pro-x.dtb'='a5a25e439be24ffd506d1dc026d4094c6c2add60e56db54900f8e89e30358516'
 'native-combined-initramfs.cpio.gz'='82979058c798dc77bb5a44796ba590285b1b4d1458117bfe3b6a819b3c3ced0b'
}
foreach($name in $expected.Keys){
 if($manifest.boot_payload_sha256.$name -ne $expected[$name]){throw "Unexpected manifest boot payload: $name"}
 if((Get-FileHash -LiteralPath (Join-Path $root "out/source-boot/$name")).Hash.ToLowerInvariant() -ne $expected[$name]){throw "Boot payload changed: $name"}
}
Write-Output "AUTHORIZED FRESH INSTALL: USB UniqueId=$ConfirmedDeviceId DiskNumber=$DiskNumber; all target data will be replaced"
function Assert-Target {
    $d=Get-Disk -Number $DiskNumber
    if($d.IsBoot -or $d.IsSystem -or $d.BusType -ne 'USB' -or $d.LogicalSectorSize -ne 512 -or $d.Size -ne 128035676160 -or $d.FriendlyName -ne 'Realtek RTL9210B-CG' -or $d.UniqueId -ne '3001237923792379'){throw 'Target identity mismatch; no writes permitted'}
    if($manifest.image_bytes -ne $d.Size -or $manifest.identity -ne 'SQ2-SSD-20261027-3001237923792379'){throw 'Image identity mismatch'}
    return $d
}
$disk=Assert-Target
$image=$manifest.windows_image
if((Get-Item -LiteralPath $image).Length -ne $disk.Size){throw 'Image length mismatch'}
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Security.Cryptography;
public static class Sq2SourceDiskWriter {
  public static string CopyRegion(FileStream source, FileStream dest, long offset, long count, bool write) {
    source.Position=offset; if(write)dest.Position=offset;
    byte[] buffer=new byte[4*1024*1024];
    using(var hash=SHA256.Create()) {
      while(count>0) {
        int n=source.Read(buffer,0,(int)Math.Min(count,buffer.Length));
        if(n<=0)throw new EndOfStreamException();
        hash.TransformBlock(buffer,0,n,null,0);
        if(write)dest.Write(buffer,0,n);
        count-=n;
      }
      hash.TransformFinalBlock(new byte[0],0,0);
      return BitConverter.ToString(hash.Hash).Replace("-","").ToLowerInvariant();
    }
  }
}
'@
$source=$null; $target=$null
try {
    $source=[IO.File]::Open($image,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    # Verify every source region before changing any physical media.
    foreach($e in $manifest.extents) {
        if($e.offset -lt 0 -or $e.bytes -le 0 -or ($e.offset+$e.bytes) -gt $disk.Size -or $e.offset%512 -or $e.bytes%512){throw 'Invalid write range'}
        if(-not $VerifyOnly -and [Sq2SourceDiskWriter]::CopyRegion($source,$null,$e.offset,$e.bytes,$false) -ne $e.sha256){throw 'Source region hash mismatch'}
    }
    Write-Output "Target identity checked; offlining external SSD (VerifyOnly=$VerifyOnly)"
    $null=Assert-Target
    Set-Disk -Number $DiskNumber -IsOffline $true
    $null=Assert-Target
    $access=if($VerifyOnly){[IO.FileAccess]::Read}else{[IO.FileAccess]::ReadWrite}
    $target=[IO.File]::Open("\\.\PhysicalDrive$DiskNumber",[IO.FileMode]::Open,$access,[IO.FileShare]::ReadWrite)
    $done=0L; $watch=[Diagnostics.Stopwatch]::StartNew(); $last=0
    if(-not $VerifyOnly) { foreach($e in $manifest.extents) {
        if([Sq2SourceDiskWriter]::CopyRegion($source,$target,$e.offset,$e.bytes,$true) -ne $e.sha256){throw 'Write source hash mismatch'}
        $done+=$e.bytes
        if($watch.Elapsed.TotalSeconds-$last -ge 10){Write-Output "WRITE $done / $($manifest.written_bytes) bytes"; $last=$watch.Elapsed.TotalSeconds}
    }
    $target.Flush($true)
    }
    Write-Output 'Write complete; reading back every written region'
    foreach($e in $manifest.extents) {
        if([Sq2SourceDiskWriter]::CopyRegion($target,$null,$e.offset,$e.bytes,$false) -ne $e.sha256){throw "Physical readback mismatch at $($e.offset)"}
        if($watch.Elapsed.TotalSeconds-$last -ge 10){Write-Output "VERIFY offset $($e.offset)"; $last=$watch.Elapsed.TotalSeconds}
    }
    if(-not $VerifyOnly){$target.Flush($true)}
    [ordered]@{installed_at=(Get-Date -Format o);disk_number=$DiskNumber;unique_id=$disk.UniqueId;capacity_bytes=$disk.Size;
        written_bytes=$manifest.written_bytes;all_written_regions_sha256_verified=$true;physical_partition_count=$manifest.physical_partitions;
        userdata_bytes=$manifest.userdata_bytes;payload_sha256=$manifest.boot_payload_sha256;
        internal_disks_touched=$false;surface_boot_verified=$false;secure_erasure=$false} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'out/source-boot/deployment.json') -Encoding UTF8
} finally {
    if($target){$target.Dispose()}; if($source){$source.Dispose()}
    # Leave offline, flushed and ready to disconnect. Do not let Windows mount
    # Linux partitions or offer formatting after deployment.
}
Write-Output 'PASS: external SSD written and readback verified; disk remains offline for removal.'
