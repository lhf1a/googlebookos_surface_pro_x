param([Parameter(Mandatory=$true)][string]$IPAddress)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$opts=@('-i',$(if ($env:SQ2_SSH_KEY) { $env:SQ2_SSH_KEY } else { Join-Path $env:USERPROFILE '.ssh/id_ed25519' }),'-o','IdentitiesOnly=yes','-o','StrictHostKeyChecking=yes','-o','HostKeyAlias=sq2-probe','-o',('UserKnownHostsFile='+(Join-Path $root 'private/sq2-ssh/known_hosts')),'-o','ConnectTimeout=8')
function Remote([string]$Command) {
    $value=& ssh.exe @opts "root@$IPAddress" $Command
    if($LASTEXITCODE -ne 0){throw 'Remote GRUB operation failed'}
    return $value
}
Remote 'set -eu; test "$(cat /sys/class/block/sda/size)" = 61440000; test "$(cat /sys/bus/usb/devices/1-1/serial)" = FC2425D40C3E1; blkid /dev/sda1 | grep -q 9CF3-A1A5; grep -q "^/dev/sda1 /media/esp " /proc/mounts; test -s /media/esp/boot/sq2-android1/native-combined-initramfs.cpio.gz'
$cfg='/media/esp/EFI/BOOT/grub.cfg'
$before=(Remote "sha256sum $cfg").Split(' ')[0]
$text=(Remote "cat $cfg") -join "`n"
$pattern='(?ms)^# BEGIN SQ2 GOOGLEBOOK NATIVE\r?\n.*?^# END SQ2 GOOGLEBOOK NATIVE\r?$'
$matches=[regex]::Matches($text,$pattern)
if($matches.Count -ne 1){throw 'Expected exactly one native GRUB section'}
$block=$matches[0].Value -replace "menuentry '[0-9]+\. Googlebook", "menuentry '1. Googlebook"
$rest=[regex]::Replace($text,$pattern,'').TrimEnd()
$rest=$rest -replace '(?m)^set timeout=.*$','set timeout=3' -replace '(?m)^set default=.*$','set default=googlebook-native'
$rest=$rest.Replace("menuentry '1. SQ2 -", "menuentry '2. SQ2 -").Replace("menuentry '2. SQ2 baseline", "menuentry '3. SQ2 baseline").Replace("menuentry '3. Power off'", "menuentry '4. Power off'")
$first=$rest.IndexOf('menuentry ')
if($first -lt 0){throw 'Diagnostic fallback missing'}
$updated=$rest.Substring(0,$first).TrimEnd()+"`n`n"+$block+"`n`n"+$rest.Substring($first)+"`n"
$out=Join-Path $root 'out/android-test/grub-native-first.cfg'
[IO.File]::WriteAllText($out,$updated,[Text.UTF8Encoding]::new($false))
$hash=(Get-FileHash $out -Algorithm SHA256).Hash.ToLowerInvariant()
& scp.exe -O @opts $out "root@${IPAddress}:/tmp/grub-native-first.cfg"
if($LASTEXITCODE -ne 0){throw 'GRUB upload failed'}
$verify=Remote 'sha256sum /tmp/grub-native-first.cfg'
if(-not $verify.StartsWith($hash)){throw 'GRUB transfer checksum mismatch'}
if((Remote "sha256sum $cfg").Split(' ')[0] -ne $before){throw 'GRUB changed during update'}
Remote "set -eu; cp /tmp/grub-native-first.cfg $cfg.new; mv $cfg.new $cfg; sync; sha256sum $cfg; cat $cfg"
[ordered]@{installed_at=(Get-Date -Format o);ip=$IPAddress;sha256=$hash;default='googlebook-native';timeout_seconds=3;first_entry='googlebook-native';previous_sha256=$before} | ConvertTo-Json | Set-Content (Join-Path $root 'out/android-test/grub-default-deployment.json') -Encoding UTF8
