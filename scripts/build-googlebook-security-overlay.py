#!/usr/bin/env python3
"""Build a minimal RAM vendor overlay for the software KeyMint experiment."""
from pathlib import Path
import hashlib
import json
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]
B = Path('/root/.cache/sq2-probe-build')
donor = ROOT / 'out/cuttlefish-security'
manifest = json.loads((donor / 'manifest.json').read_text())
for name, entry in manifest['files'].items():
    assert hashlib.sha256((donor / name).read_bytes()).hexdigest() == entry['sha256']
vendor = B / 'googlebook/vendor'
out = Path(tempfile.mkdtemp(prefix='googlebook-security.', dir=B))
out.chmod(0o755)  # This directory becomes the overlay's /vendor root.

def write(name, content, mode=0o644):
    path = out / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content if isinstance(content, bytes) else content.encode())
    path.chmod(mode)

write('bin/hw/android.hardware.security.keymint-service', (donor / 'android.hardware.security.keymint-service').read_bytes(), 0o755)
write('lib64/sq2_keymint/libcrypto.so', (donor / 'libcrypto.so').read_bytes())
# AOSP BootControl uses the RAM misc metadata and does not call ChromeOS firmware.
write('bin/hw/android.hardware.boot-service.android-desktop', (donor / 'boot-service.default').read_bytes(), 0o755)
boot_rc = (vendor / 'etc/init/android.hardware.boot-service.android-desktop.rc').read_text()
assert 'seclabel' not in boot_rc
write('etc/init/android.hardware.boot-service.android-desktop.rc', boot_rc + '    seclabel u:r:hal_bootctl_default:s0\n')
write('apex/com.android.hardware.gatekeeper.nonsecure.apex', (donor / 'com.android.hardware.gatekeeper.nonsecure.apex').read_bytes())
# Optional Weaver hardware is absent; declaring it makes LockSettings wait
# indefinitely. Use the official software Gatekeeper instead of Trusty.
for component in ('android.hardware.gatekeeper-service.trusty', 'android.hardware.weaver-service.android-desktop'):
    for folder, suffix in (('etc/init', '.rc'), ('etc/vintf/manifest', '.xml')):
        name = folder + '/' + component + suffix
        assert (vendor / name).is_file(), name
        write(name, '# SQ2 lab: supplied by software Gatekeeper or absent hardware.\n' if suffix == '.rc' else '<manifest version="9.0" type="device"/>\n')
# The mica QSH plugin waits forever for an incompatible sensors QMI service.
# Keep dynamic sensors available while physical SQ2 motion sensors are ported.
# This is not a claim that accelerometer/gyroscope/light sensors work.
write('etc/sensors/hals.conf', 'sensors.dynamic_sensor_hal.so\n')
# SQ2 has eight CPUs and no validated cpufreq policy yet. Use real scheduler
# weight hints; do not write mica's CPU8 paths or 4 GHz frequency limits.
write('etc/powerhint.json', (ROOT / 'configs/powerhint.sq2.json').read_bytes())
# The adapter exposes the resident ath10k interface; stock AIDL supplicant and
# wificond perform real nl80211 scanning, association and IP provisioning.
wifi_adapter = ROOT / 'out/connectivity/libsq2-wifi-hal.so'
assert wifi_adapter.is_file(), 'Build scripts/build-sq2-connectivity.sh first'
write('lib64/libsq2-wifi-hal.so', wifi_adapter.read_bytes())
wifi_rc = (vendor / 'etc/init/android.hardware.wifi-service.rc').read_text()
write('etc/init/android.hardware.wifi-service.rc', wifi_rc +
      '    setenv LD_PRELOAD /vendor/lib64/libsq2-wifi-hal.so\n')
audio = ROOT / 'out/cuttlefish-audio'
audio_manifest = json.loads((audio / 'manifest.json').read_text())
for name, entry in audio_manifest['files'].items():
    data = (audio / name).read_bytes()
    assert hashlib.sha256(data).hexdigest() == entry['sha256'], name
    write(name, data)
# The virtual APEX owns Bluetooth audio. Do not advertise an absent QTI DSP.
for name in ('bluetooth_audio.xml', 'soundtrigger.qti.xml'):
    assert (vendor / 'etc/vintf/manifest' / name).is_file(), name
    write('etc/vintf/manifest/' + name, '<manifest version="1.0" type="device"/>\n')
for component in ('keymint', 'secureclock', 'sharedsecret'):
    name = f'android.hardware.security.{component}-service.xml'
    write('etc/vintf/manifest/sq2-' + name, (donor / name).read_bytes())
write('etc/init/sq2-software-keymint.rc', '''# Development only: AOSP software KeyMint, no hardware-backed secrets.
on early-init
    setprop sys.use_memfd true

service vendor.keymint-default /vendor/bin/hw/android.hardware.security.keymint-service
    class early_hal
    user nobody
    seclabel u:r:hal_keymint_default:s0
    setenv LD_LIBRARY_PATH /vendor/lib64/sq2_keymint:/vendor/lib64
''')
# apply-security-overlay hides the absent StrongBox APEX using a RAM whiteout.
# Gate only the unsupported vendor module-loader wait. The SQ2 bootstrap has
# already loaded its own matching drivers and firmware; do not assert that
# the original 6.12 Android modules have loaded on the SQ2 6.3 kernel.
name = 'etc/init/hw/init.qti.kernel.rc'
content = (vendor / name).read_text()
before = 'on post-fs-data\n    # Late attach SOCCP and start ADSP and CDSP\n    wait_for_prop vendor.all.modules.ready 1\n    restart start-subsys'
assert content.count(before) == 1
content = content.replace(before, 'on post-fs-data && property:ro.boot.sq2.stock_modules=1\n    wait_for_prop vendor.all.modules.ready 1\n    restart start-subsys')
write(name, content)
(ROOT / 'out/android-test/security-overlay-path.txt').write_text(str(out) + '\n')
result = {'source': manifest['source'], 'software_keymint': True, 'hardware_backed': False,
          'virtual_audio': True, 'physical_audio_ported': False,
          'bootcontrol': 'AOSP default with RAM misc metadata',
          'audio_source': audio_manifest['source'],
          'software_gatekeeper': True, 'weaver_present': False, 'path': str(out),
          'files': {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in sorted(out.rglob('*')) if p.is_file()}}
(ROOT / 'out/android-test/security-overlay-manifest.json').write_text(json.dumps(result, indent=2) + '\n')
print('Prepared software KeyMint RAM overlay:', out)
