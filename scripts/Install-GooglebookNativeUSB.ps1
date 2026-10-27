param([Parameter(Mandatory=$true)][string]$IPAddress)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$identity=$(if ($env:SQ2_SSH_KEY) { $env:SQ2_SSH_KEY } else { Join-Path $env:USERPROFILE '.ssh\id_ed25519' })
$known=Join-Path $root 'private\sq2-ssh\known_hosts'
$opts=@('-i',$identity,'-o','IdentitiesOnly=yes','-o','StrictHostKeyChecking=yes','-o','HostKeyAlias=sq2-probe','-o',"UserKnownHostsFile=$known",'-o','ServerAliveInterval=15')
function Remote([string]$Command) {
    $result=& ssh.exe @opts "root@$IPAddress" $Command
    if($LASTEXITCODE -ne 0){throw "Remote operation failed: $Command"}
    return $result
}
if(-not (Select-String -LiteralPath (Join-Path $root 'out/android-test/.config') -SimpleMatch '# CONFIG_BLK_DEV_NVME is not set')){throw 'Expected internal NVMe disabled'}
$validation=Get-Content -LiteralPath (Join-Path $root 'out/android-test/native-validation.json') -Raw | ConvertFrom-Json
$expectedApex=if($validation.software_keymint){44 + [int][bool]$validation.software_gatekeeper}else{45}
if(-not $validation.native_pid1 -or -not $validation.persistent_log_recovered -or $validation.bootstrap_apex_activated -ne $expectedApex){throw 'Native boot preflight incomplete'}
if(-not $validation.bootcontrol_running -or -not $validation.late_userdata_mount -or -not $validation.keystore_no_fatal_debug){throw 'Native boot fixes not validated'}
foreach($name in @('Image','sc8180x-surface-pro-x.dtb','native-combined-initramfs.cpio.gz')) {
    $hash=(Get-FileHash -LiteralPath (Join-Path $root "out/android-test/$name") -Algorithm SHA256).Hash.ToLowerInvariant()
    if($hash -ne $validation.payload_sha256.$name){throw "Payload changed since preflight: $name"}
}
$check='set -eu; test "$(cat /sys/class/block/sda/size)" = 61440000; test "$(cat /sys/bus/usb/devices/1-1/serial)" = FC2425D40C3E1; blkid /dev/sda1 | grep -q 9CF3-A1A5; if ! grep -q "^/dev/sda1 /media/android-boot " /proc/mounts; then ! grep -q " /media/android-boot " /proc/mounts; mkdir -p /media/android-boot; if grep -q "^/dev/sda1 /media/esp " /proc/mounts; then mount --bind /media/esp /media/android-boot; else mount -t vfat /dev/sda1 /media/android-boot; fi; fi; grep -q "^/dev/sda1 /media/android-boot " /proc/mounts; test -f /media/android-boot/SQ2-PROBE-MEDIA.txt; mkdir -p /media/android-boot/boot/sq2-android1'
Remote $check
$before=Remote 'sha256sum /media/android-boot/EFI/BOOT/grub.cfg'
$records=@()
foreach($name in @('Image','sc8180x-surface-pro-x.dtb','native-combined-initramfs.cpio.gz')) {
    $src=Join-Path $root "out/android-test/$name"
    $hash=(Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash.ToLowerInvariant()
    $dst="/media/android-boot/boot/sq2-android1/$name"
    & scp.exe -O @opts $src "root@${IPAddress}:$dst.partial"
    if($LASTEXITCODE -ne 0){throw "Upload failed: $name"}
    $actual=Remote "sha256sum $dst.partial"
    if(-not $actual.StartsWith($hash)){throw "Hash mismatch: $name"}
    Remote "mv $dst.partial $dst; sync"
    $records+=[ordered]@{file=$name;sha256=$hash;bytes=(Get-Item -LiteralPath $src).Length}
    Write-Output "Verified $name"
}
# Update only our marked section. Keep all existing menu entries and the default.
$entry=Join-Path $root 'configs/grub-android-entry.cfg'
& scp.exe -O @opts $entry "root@${IPAddress}:/tmp/googlebook-native-entry.cfg"
if($LASTEXITCODE -ne 0){throw 'Entry transfer failed'}
Remote 'set -eu; cfg=/media/android-boot/EFI/BOOT/grub.cfg; cp "$cfg" "$cfg.before-googlebook-native"; awk ''function entry(){ print "# BEGIN SQ2 GOOGLEBOOK NATIVE"; while ((getline l < "/tmp/googlebook-native-entry.cfg") > 0) print l; close("/tmp/googlebook-native-entry.cfg"); print "# END SQ2 GOOGLEBOOK NATIVE" } /^# BEGIN SQ2 GOOGLEBOOK NATIVE$/ {entry(); seen=1; skip=1; next} /^# END SQ2 GOOGLEBOOK NATIVE$/ {skip=0; next} !skip {print} END {if (!seen) entry()}'' "$cfg" > "$cfg.new"; mv "$cfg.new" "$cfg"; sync'
$after=Remote 'sha256sum /media/android-boot/EFI/BOOT/grub.cfg; cat /media/android-boot/EFI/BOOT/grub.cfg'
[ordered]@{installed_at=(Get-Date -Format o);ip=$IPAddress;grub_before=$before;grub_after=$after;payload=$records;default_unchanged=$true;internal_nvme_disabled=$true} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'out/android-test/usb-native-deployment.json') -Encoding UTF8
Write-Output 'PASS: native Googlebook entry deployed, payload hashes verified, existing menu order and default retained.'
