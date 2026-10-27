#!/usr/bin/env python3
"""Verify recovered native-boot logs and bind the result to the tested payload."""
from pathlib import Path
import json,hashlib,re
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'out/android-test'
logs=sorted((out/'qemu-native-saved').glob('logs/googlebook-native-*/kernel-live.txt'),key=lambda p:p.stat().st_mtime)
latest=logs[-1]
text=latest.read_text(errors='replace')
assert 'PASS: memfd ashmem' in (latest.parent/'memfd-compat.txt').read_text(), 'memfd compatibility test failed'
assert 'SQ2_NATIVE_HANDOFF' in (out/'qemu-native-console.log').read_text(errors='replace')
assert 'init second stage started' in text
software_security = 'software KeyMint RAM overlay' in (latest.parent/'status.txt').read_text()
software_gatekeeper = software_security and json.loads((out/'security-overlay-manifest.json').read_text()).get('software_gatekeeper', False)
expected_apex = (44 + int(software_gatekeeper)) if software_security else 45
assert f'Activated {expected_apex} packages' in text, 'Expected bootstrap APEX activation not reached'
assert (latest.parent/'properties-latest.txt').exists(), 'Automatic service snapshot missing'
props=(latest.parent/'properties-latest.txt').read_text()
assert '[init.svc.vendor.boot-default]: [running]' in props, 'BootControl not running'
assert '[init.svc_debug.no_fatal.keystore2]: [true]' in props, 'Keystore development reboot override absent'
if software_security:
    assert '[init.svc.vendor.keymint-default]: [running]' in props, 'Software KeyMint not running'
    services = (latest.parent/'service-checks-latest.txt').read_text()
    assert 'Service android.hardware.security.keymint.IKeyMintDevice/default: found' in services, 'KeyMint Binder service missing'
    assert 'Service android.security.maintenance: found' in services, 'Keystore maintenance service missing'
    if software_gatekeeper:
        assert 'Service android.hardware.gatekeeper.IGatekeeper/default: found' in services, 'Software Gatekeeper missing'
    assert not re.search(r"Service 'keystore2'.*received SIGABRT", text), 'Keystore still crashing'
assert any("Command 'mount_all --late'" in line and 'succeeded' in line for line in text.splitlines()), 'Late userdata mount failed'
assert 'InitFatalReboot' not in text, 'Unexpected Android fatal reboot'
if 'CONFIG_NETFILTER=y' in (out/'.config').read_text():
    # This QEMU machine has no MSM GPU. SurfaceFlinger failures can restart
    # zygote and SIGKILL netd, so a final running snapshot is not a stability
    # test here. Reject netd's own initialization exits and require progress
    # through routing initialization; verify stability on the physical SQ2.
    logcat=(latest.parent/'logcat-latest.txt').read_text(errors='replace')
    assert 'Initializing RouteController:' in logcat, 'netd routing initialization not observed'
    assert not re.search(r"Service 'netd'.*exited with status [1-9]", text), 'netd initialization still fails'
uptimes=[int(m.group(1))/1000000 for m in re.finditer(r'^\d+,\d+,(\d+),',text,re.M)]
assert '# CONFIG_BLK_DEV_NVME is not set' in (out/'.config').read_text()
result={'native_pid1':True,'second_stage_init':True,'bootstrap_apex_activated':expected_apex,'persistent_log_recovered':True,
        'software_keymint':software_security,
        'software_gatekeeper':software_gatekeeper,
        'bootcontrol_running':True,'late_userdata_mount':True,'keystore_no_fatal_debug':True,
        'observed_guest_uptime_seconds':max(uptimes,default=0),
        'keystore_sigabrt_count':len(re.findall(r"Service 'keystore2'.*received SIGABRT",text)),
        'desktop_boot_verified':False,'kernel_log':str(latest),
        'netd_final_snapshot_running':'[init.svc.netd]: [running]' in props,
        'netd_stability_verified':False,
        'payload_sha256':{name:hashlib.sha256((out/name).read_bytes()).hexdigest() for name in ['Image','native-initramfs.cpio.gz','native-combined-initramfs.cpio.gz','sc8180x-surface-pro-x.dtb']}}
(out/'native-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(f'PASS: native Android init, {expected_apex} bootstrap APEXes, persistent kernel and service logs; desktop unverified')
