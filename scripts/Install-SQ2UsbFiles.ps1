#requires -Version 5.1
#requires -RunAsAdministrator
# Target-specific file deployment after temporarily exposing the existing FAT partition.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$disk=Get-Disk -Number 1
$part=Get-Partition -DiskNumber 1 -PartitionNumber 2
if ($disk.BusType -ne 'USB' -or $disk.Size -ne 31457280000 -or $disk.IsBoot -or $disk.IsSystem -or
    $disk.UniqueId -notlike '*FC2425D40C3E1*' -or $part.Offset -ne 31142707200 -or $part.Size -ne 312475136 -or
    $part.MbrType -ne 6 -or $part.DriveLetter -ne 'E') { throw 'Expected mounted USB partition did not match.' }
$root='E:\'
$stage=Join-Path $env:USERPROFILE 'sq2-probe-stage-20261026'
$backup=Join-Path $env:USERPROFILE 'sq2-usb-backup-20261026'
$original=Get-Content (Join-Path $env:USERPROFILE 'usb-preservation.json') -Raw | ConvertFrom-Json
$payload=Get-Content (Join-Path $env:USERPROFILE 'usb-payload-manifest.json') -Raw | ConvertFrom-Json
$allowed=@('EFI/BOOT/BOOTAA64.EFI','EFI/BOOT/grub.cfg','boot/sq2-probe/Image','boot/sq2-probe/initramfs.cpio.gz','boot/sq2-probe/sc8180x-surface-pro-x.dtb','SQ2-PROBE-MEDIA.txt')
function Assert-OriginalFiles {
    foreach ($file in $original.originalFiles) {
        $path=[IO.Path]::GetFullPath((Join-Path $root $file.path))
        if (-not $path.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or
            (Get-Item -LiteralPath $path).Length -ne $file.bytes -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) {
            throw "Original file mismatch: $($file.path)"
        }
    }
}
try {
    Assert-OriginalFiles
    if (@($payload).Count -ne 6) { throw 'Expected six new files.' }
    foreach ($file in $payload) {
        if ($file.path -cnotin $allowed) { throw 'Unexpected payload path.' }
        $source=Join-Path $stage $file.path
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $file.sha256) { throw 'Staged file hash mismatch.' }
        $existing=Join-Path $root $file.path
        if ((Test-Path -LiteralPath $existing) -and (Get-FileHash -LiteralPath $existing -Algorithm SHA256).Hash -ne $file.sha256) {
            throw "Destination exists with different contents: $($file.path)"
        }
    }
    foreach ($file in $payload) {
        $target=Join-Path $root $file.path
        if (Test-Path -LiteralPath $target) { continue }
        $parent=Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
            $null=New-Item -ItemType Directory -Path $parent -Force
        }
        $inputStream=[IO.File]::OpenRead((Join-Path $stage $file.path))
        try {
            $outputStream=[IO.File]::Open($target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            try { $inputStream.CopyTo($outputStream); $outputStream.Flush($true) } finally { $outputStream.Dispose() }
        } finally { $inputStream.Dispose() }
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $file.sha256) { throw 'New file verification failed.' }
    }
    Assert-OriginalFiles
    if (Get-Command Write-VolumeCache -ErrorAction SilentlyContinue) { Write-VolumeCache -DriveLetter E }
    [ordered]@{diskNumber=1; partitionNumber=2; originalFilesVerified=16; addedFilesVerified=6; originalFileContentsUnchanged=$true} |
        ConvertTo-Json | Tee-Object -FilePath (Join-Path $backup 'file-install-result.json')
} finally {
    # Restore the partition's original hidden FAT16 type and remove its temporary drive letter.
    Remove-PartitionAccessPath -DiskNumber 1 -PartitionNumber 2 -AccessPath $root -ErrorAction Continue
    Set-Partition -DiskNumber 1 -PartitionNumber 2 -MbrType 22
    Update-HostStorageCache
}
