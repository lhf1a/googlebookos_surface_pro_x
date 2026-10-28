param([Parameter(Mandatory=$true)][string]$IPAddress)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$backup=Join-Path $root 'out/esp-retired-20261029'
New-Item -ItemType Directory -Force -Path $backup | Out-Null
$opts=@('-O','-i',(Join-Path $env:USERPROFILE '.ssh/id_ed25519'),'-o','BatchMode=yes','-o','IdentitiesOnly=yes','-o','StrictHostKeyChecking=yes','-o','HostKeyAlias=sq2-probe','-o',('UserKnownHostsFile='+(Join-Path $root 'private/sq2-ssh/known_hosts')))
$rows=Get-Content (Join-Path $root 'out/display-refresh/esp-cpio-manifest.txt')
$deleteLines=[Collections.Generic.List[string]]::new()
$deleteLines.Add('#!/bin/sh')
$deleteLines.Add('set -eu')
$deleteLines.Add('test "$(cat /sys/block/nvme0n1/device/serial | tr -d '' '')" = CYA5N050012105J77')
$deleteLines.Add('test "$(cat /media/esp/SQ2-SSD-ID.txt)" = SQ2-INTERNAL-512-20261027')
$deleteLines.Add('blkid /dev/nvme0n1p6 | grep -q B027-0512')
$deleteLines.Add('test "$(sha256sum /media/esp/EFI/BOOT/grub.cfg | cut -d '' '' -f 1)" = d7af23a37187494f68fc04d2541b2466d14a6af74bc612eec66e733af9237f63')
$report=@()
foreach($row in $rows) {
    if($row -notmatch '^([0-9a-f]{64})  (/media/esp/boot/sq2-android1/([^/]+))$'){throw "Invalid manifest row"}
    $sha=$Matches[1]; $remote=$Matches[2]; $name=$Matches[3]
    if($name -in @('native-combined-initramfs.cpio.gz','native-closed-vulkan-v8.cpio.gz','edp-rates-v3-addon.cpio.gz')){continue}
    if($name -notmatch '^(native-before-[a-z-]+|native-power-v1|native-graphics-only|edp-rates-v2-addon)\.cpio\.gz$'){throw "Unreviewed image: $name"}
    $dest=Join-Path $backup $name
    if(-not (Test-Path -LiteralPath $dest)) {
        & scp.exe @opts "root@${IPAddress}:$remote" $dest
        if($LASTEXITCODE -ne 0){throw "Backup failed: $name"}
    }
    if((Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLower() -ne $sha){throw "Backup hash mismatch: $name"}
    $deleteLines.Add("test -f '$remote' && test ! -L '$remote'")
    $deleteLines.Add('test "$(sha256sum '''+$remote+''' | cut -d '' '' -f 1)" = '+$sha)
    $report+=@{name=$name;sha256=$sha;bytes=(Get-Item -LiteralPath $dest).Length}
}
# Validate every target before deleting any. Exact absolute files only, no glob/recursion.
foreach($item in $report){$deleteLines.Add("rm -- '/media/esp/boot/sq2-android1/$($item.name)'")}
$deleteLines.Add('sync')
$deleteLines.Add('df -k /media/esp')
[IO.File]::WriteAllText((Join-Path $root 'out/display-refresh/remove-retired-initramfs.sh'),($deleteLines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
$report | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $backup 'manifest.json')
$total=($report | Measure-Object -Property bytes -Sum).Sum
Write-Output "Verified $($report.Count) backups, $total bytes; prepared explicit deletion script."
