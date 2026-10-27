#requires -Version 5.1
#requires -RunAsAdministrator
[CmdletBinding()]
param([switch]$Resume)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$root=Split-Path -Parent $PSScriptRoot
$output=Join-Path $root 'out\test2-usb'
$result=Join-Path $output 'deployment-result.json'
Start-Transcript -Path (Join-Path $output 'deployment.log') -Append | Out-Null
try {
    $manifestPath=Join-Path $output 'payload-manifest.json'
    $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    function Get-TargetDisk {
        $matches=@(Get-Disk | Where-Object { $_.BusType -eq 'USB' -and $_.SerialNumber.Trim() -eq 'FC2425D40C3E1' -and $_.Size -eq 31457280000 })
        if ($matches.Count -ne 1 -or $matches[0].IsBoot -or $matches[0].IsSystem) { throw 'USB identity mismatch; refusing disk operations.' }
        return $matches[0]
    }
    $disk=Get-TargetDisk
    if ($manifest.disk_serial -ne 'FC2425D40C3E1' -or $manifest.disk_bytes -ne 31457280000 -or $manifest.boot_bytes -ne 2147483648) { throw 'Manifest target mismatch.' }
    if (-not (Select-String -LiteralPath (Join-Path $root 'out\smoke-test2-efi-result.log') -SimpleMatch 'PASS: EFI/GRUB')) { throw 'EFI/GRUB smoke test has not passed.' }
    $seen=@{}
    foreach ($f in $manifest.files) {
        if ($f.partition -notin @('boot','data') -or [IO.Path]::IsPathRooted($f.path) -or $f.path -match '(^|[/\\])\.\.([/\\]|$)') { throw 'Unsafe destination in manifest.' }
        $key="$($f.partition)/$($f.path)".ToLowerInvariant()
        if ($seen.ContainsKey($key)) { throw "Duplicate destination $key" }; $seen[$key]=$true
        $src=[IO.Path]::GetFullPath((Join-Path $root $f.source))
        if (-not $src.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or (Get-Item -LiteralPath $src).Length -ne $f.bytes) { throw "Invalid source $src" }
        if ((Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash -ne $f.sha256) { throw "Source hash mismatch $src" }
    }
    $bootBytes=($manifest.files | Where-Object partition -eq boot | Measure-Object bytes -Sum).Sum
    $dataBytes=($manifest.files | Where-Object partition -eq data | Measure-Object bytes -Sum).Sum
    if ($bootBytes -gt (2GB-128MB) -or $dataBytes -gt ($disk.Size-2GB-256MB)) { throw 'Payload exceeds intended partition sizes.' }
    if (-not $Resume) {
        if (Get-Volume -DriveLetter P,Q -ErrorAction SilentlyContinue) { throw 'P: or Q: is occupied; refusing to reuse it.' }
        $disk=Get-TargetDisk
        Write-Output "Formatting authorized USB only: Disk $($disk.Number), serial FC2425D40C3E1, 31457280000 bytes"
        Clear-Disk -Number $disk.Number -RemoveData -RemoveOEM -Confirm:$false
        Update-HostStorageCache
        $disk=Get-TargetDisk
        if ($disk.PartitionStyle -eq 'RAW') {
            Initialize-Disk -Number $disk.Number -PartitionStyle GPT | Out-Null
        } elseif ($disk.PartitionStyle -ne 'GPT') {
            # Some removable devices retain an initialized empty MBR after Clear-Disk.
            if ($disk.NumberOfPartitions -ne 0) { throw 'Refusing conversion of a nonempty disk.' }
            Set-Disk -Number $disk.Number -PartitionStyle GPT
        }
        $boot=New-Partition -DiskNumber $disk.Number -Size 2GB -DriveLetter P -GptType '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
        $boot | Format-Volume -FileSystem FAT32 -NewFileSystemLabel SQ2PROBE -Confirm:$false -Force | Out-Null
        $data=New-Partition -DiskNumber $disk.Number -UseMaximumSize -DriveLetter Q
        $data | Format-Volume -FileSystem exFAT -NewFileSystemLabel SQ2DATA -Confirm:$false -Force | Out-Null
    }
    $disk=Get-TargetDisk
    $boot=Get-Partition -DiskNumber $disk.Number | Where-Object DriveLetter -eq P
    $data=Get-Partition -DiskNumber $disk.Number | Where-Object DriveLetter -eq Q
    if ($disk.PartitionStyle -ne 'GPT' -or $boot.Size -ne 2GB -or $boot.GptType -ne '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}' -or $data.Size -lt 27GB -or
        (Get-Volume -DriveLetter P).FileSystemLabel -ne 'SQ2PROBE' -or (Get-Volume -DriveLetter Q).FileSystemLabel -ne 'SQ2DATA') { throw 'New partition layout mismatch.' }
    $verified=0
    foreach ($f in $manifest.files) {
        $volumeRoot=if($f.partition -eq 'boot'){'P:\'}else{'Q:\'}
        $dst=[IO.Path]::GetFullPath((Join-Path $volumeRoot $f.path))
        if (-not $dst.StartsWith($volumeRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Destination escaped volume root.' }
        $parent=Split-Path -Parent $dst
        if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
        $skip=$false
        if ($Resume -and (Test-Path -LiteralPath $dst)) { $skip=((Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -eq $f.sha256) }
        if (-not $skip) {
            Write-Output "Copy $($f.partition)/$($f.path) [$($f.bytes) bytes]"
            Copy-Item -LiteralPath (Join-Path $root $f.source) -Destination $dst -Force
        }
        if ((Get-Item -LiteralPath $dst).Length -ne $f.bytes -or (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -ne $f.sha256) { throw "USB readback mismatch: $dst" }
        $verified++
    }
    Copy-Item -LiteralPath $manifestPath -Destination 'P:\payload-manifest.json' -Force
    Copy-Item -LiteralPath $manifestPath -Destination 'Q:\payload-manifest.json' -Force
    Write-VolumeCache -DriveLetter P
    Write-VolumeCache -DriveLetter Q
    $layout=@(Get-Partition -DiskNumber $disk.Number | Select-Object PartitionNumber,DriveLetter,Offset,Size,GptType)
    $volumes=@(Get-Volume -DriveLetter P,Q | Select-Object DriveLetter,FileSystemLabel,FileSystem,Size,SizeRemaining)
    [ordered]@{status='PASS';utc=[DateTime]::UtcNow.ToString('o');diskNumber=$disk.Number;serial=$disk.SerialNumber;verifiedFiles=$verified;manifestSHA256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash;partitions=$layout;volumes=$volumes} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $result -Encoding UTF8
    Write-Output 'PASS: USB formatted, payload copied, every payload file verified by SHA256.'
} catch {
    [ordered]@{status='FAILED';utc=[DateTime]::UtcNow.ToString('o');error=$_.Exception.Message;detail=($_ | Out-String)} | ConvertTo-Json | Set-Content -LiteralPath $result -Encoding UTF8
    Write-Output ($_ | Out-String)
    exit 1
} finally { Stop-Transcript | Out-Null }
