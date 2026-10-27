#requires -Version 5.1
#requires -RunAsAdministrator
[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $env:USERPROFILE 'sq2-usb-backup-20261026'))
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$disk = Get-Disk -Number 1
$part = Get-Partition -DiskNumber 1 -PartitionNumber 2
if ($disk.BusType -ne 'USB' -or $disk.Size -ne 31457280000 -or $disk.IsBoot -or $disk.IsSystem -or
    $disk.UniqueId -notlike '*FC2425D40C3E1*' -or $part.Offset -ne 31142707200 -or $part.Size -ne 312475136) {
    throw 'Expected USB disk/partition identity did not match.'
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Backup destination already exists.' }
$null = New-Item -ItemType Directory -Path $OutputDirectory
$disk | Select-Object Number,FriendlyName,BusType,PartitionStyle,Size,UniqueId | ConvertTo-Json |
    Set-Content (Join-Path $OutputDirectory 'disk.json') -Encoding UTF8
Get-Partition -DiskNumber 1 | Select-Object PartitionNumber,MbrType,Size,Offset | ConvertTo-Json |
    Set-Content (Join-Path $OutputDirectory 'partitions.json') -Encoding UTF8
$source = [IO.File]::Open('\\.\PhysicalDrive1',[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
function Copy-Range([long]$Offset, [long]$Length, [string]$Name) {
    $path = Join-Path $OutputDirectory $Name
    $target = [IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $null = $source.Seek($Offset,[IO.SeekOrigin]::Begin)
        $buffer = New-Object byte[] (1024*1024)
        $remaining = $Length
        while ($remaining -gt 0) {
            $count = $source.Read($buffer,0,[int][Math]::Min($buffer.Length,$remaining))
            if ($count -le 0) { throw 'Unexpected end of disk.' }
            $target.Write($buffer,0,$count)
            $null = $sha.TransformBlock($buffer,0,$count,$buffer,0)
            $remaining -= $count
        }
        $null = $sha.TransformFinalBlock([byte[]]@(),0,0)
        $target.Flush($true)
        $digest = [BitConverter]::ToString($sha.Hash).Replace('-','').ToLowerInvariant()
    } finally { $target.Dispose(); $sha.Dispose() }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $digest) { throw 'Backup verification failed.' }
    [ordered]@{name=$Name; offset=$Offset; bytes=$Length; sha256=$digest}
}
try {
    $files = @(Copy-Range 0 1048576 'disk-first-1MiB.bin'; Copy-Range $part.Offset $part.Size 'partition2-original.img')
    $files | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'manifest.json') -Encoding UTF8
    $files | ConvertTo-Json
} finally { $source.Dispose() }
