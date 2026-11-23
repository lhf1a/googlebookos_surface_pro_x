"""Regression tests for userspace s2idle hygiene, boot init SIGABRT fixes,
zero-orphan timeout handling, and display-gated background polling."""
import gzip
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from single_user_payload import ANCHOR, HOOK, cpio_module
import disk_image
import runtime_boot_policy

cpio = cpio_module()


BUSYBOX = shutil.which('busybox')


def _shells():
    shells = [['sh']]
    if BUSYBOX:
        shells.append([BUSYBOX, 'sh'])
    return shells


def _write(path: Path, content: str, mode: int = 0o644) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8')
    path.chmod(mode)
    return path


def _make_fake_root_fixture(root: Path, *, with_testscripts: bool = False, matching_krel: bool = False):
    """Populate a realistic Android root tree matching Surface Pro X donor files."""
    (root / 'debug_ramdisk' / 'sq2-firmware').mkdir(parents=True, exist_ok=True)
    _write(root / 'vendor/firmware/rtl_nic/rtl8156b-2.fw', 'fake-rtl8156b-fw')

    _write(root / 'vendor/etc/init/hw/init.qti.ufs.rc', textwrap.dedent("""\
        on init
            exec u:r:vendor-qti-testscripts:s0 -- /vendor/bin/sh /vendor/bin/init.qti.ufs.debug.sh

        on property:sys.boot_completed=1
            write /sys/bus/platform/devices/1d84000.ufshc/pm_qos_enable 0
    """))
    _write(root / 'vendor/etc/init/hw/init.qcom.rc', textwrap.dedent("""\
        service qti-testscripts /system/bin/sh /product/etc/init.qcom.testscripts.sh
            class late_start
            user root
            disabled
            oneshot
            seclabel u:r:qti-testscripts:s0

        on property:sys.boot_completed=1
            start qcom-post-boot
            start qti-testscripts
    """))
    if with_testscripts:
        _write(root / 'vendor/bin/init.qti.ufs.debug.sh', '#!/vendor/bin/sh\n', 0o755)
        _write(root / 'product/etc/init.qcom.testscripts.sh', '#!/system/bin/sh\n', 0o755)

    _write(root / 'system/etc/init/udsattestation.rc', textwrap.dedent("""\
        service android.system.desktop.security.udsattestation /system/bin/udsattestation
            class main
            oneshot
            user hsm
            group hsm inet
    """))
    _write(root / 'system/etc/init/timberslide.rc', textwrap.dedent("""\
        on post-fs-data
            mkdir /data/misc/timberslide/ 0750 system system

        service timberslide /system/bin/timberslide --name cros_ec --period-ms 4000
           user system
           group system cros_ec

           class core
    """))
    _write(root / 'system/etc/init/ec_crash_collector.rc', textwrap.dedent("""\
        service ec_crash_collector /system/bin/ec_crash_collector
            user system
            group system cros_ec
            oneshot

        on property:sys.boot_completed=1
            start ec_crash_collector
    """))
    _write(root / 'vendor/etc/init/android.hardware.health-service.android-desktop.rc', textwrap.dedent("""\
        service vendor.health-default /vendor/bin/hw/android.hardware.health-service.android-desktop
            class hal
            user system
            group system
            interface aidl vendor.google.desktop.health.IDesktopHealth/default

        service vendor.charger /vendor/bin/hw/android.hardware.health-service.android-desktop --charger
            class charger
            user system
            group system wakelock input

        service vendor.ec-battery-limits-setprop /vendor/bin/hw/ec_battery_limits
            class hal
            user system
            group cros_ec
            oneshot
    """))
    _write(root / 'vendor/etc/init/ec-executor.rc', textwrap.dedent("""\
        service vendor.google.desktop.ec-executor /vendor/bin/hw/vendor.google.desktop.ec-executor
            class hal
            user system
            group root
            interface aidl vendor.google.desktop.IEcExecutor/default
    """))
    _write(root / 'vendor/etc/vintf/manifest/vendor.google.desktop.ec-executor.xml', textwrap.dedent("""\
        <manifest version="9.0" type="device">
            <hal format="aidl">
                <name>vendor.google.desktop</name>
                <version>2</version>
                <fqname>IEcExecutor/default</fqname>
            </hal>
        </manifest>
    """))
    _write(root / 'vendor/etc/init/suspend_notification_service.rc', textwrap.dedent("""\
        service suspend_notification_service /vendor/bin/hw/suspend_notification_service
            interface aidl com.android.desktop.power.ISuspendNotificationService/default
            class hal
            user system
            group system
            oneshot
    """))
    _write(root / 'vendor/etc/vintf/manifest/com.android.desktop.power.suspend_notification_service.xml', textwrap.dedent("""\
        <manifest version="9.0" type="device">
            <hal format="aidl">
                <name>com.android.desktop.power</name>
                <fqname>ISuspendNotificationService/default</fqname>
            </hal>
        </manifest>
    """))
    _write(root / 'vendor/etc/init/suspend_diagnostics.rc', textwrap.dedent("""\
        service suspend_diagnostics /vendor/bin/hw/suspend_diagnostics
            class hal
            user system
            group system
    """))
    _write(root / 'vendor/etc/init/vendor.qti.adsprpc-guestos-service.rc', textwrap.dedent("""\
        service vendor.adsprpcd /vendor/bin/adsprpcd rootpd adsp
           class main
           user system
           group media
    """))
    _write(root / 'vendor/etc/init/vendor.qti.audio-adsprpc-service.rc', textwrap.dedent("""\
        service vendor.audioadsprpcd_audiopd /vendor/bin/audioadsprpcd audiopd adsp
           class main
           user media
           group media

        on early-init
            start vendor.audioadsprpcd_audiopd
    """))
    _write(root / 'vendor/etc/init/vendor.qti.cdsprpc-service.rc', textwrap.dedent("""\
        service vendor.cdsprpcd /vendor/bin/cdsprpcd rootpd cdsp
           class main
           user system
           group system

        on property:vendor.fastrpc.disable.cdsprpcd.daemon=1
           stop vendor.cdsprpcd
    """))
    _write(root / 'vendor/etc/init/vendor.sensors.sscrpcd.rc', textwrap.dedent("""\
        service vendor.sensors /vendor/bin/sscrpcd sensorspd adsp
            class early_hal
            user system
            group system wakelock
            disabled
    """))
    _write(root / 'vendor/etc/init/init.vendor.sensors.rc', textwrap.dedent("""\
        on early-boot
            chown system system /mnt/vendor/persist/sensors
            start vendor.sensors
            chown system system /sys/kernel/boot_adsp/ssr
    """))
    _write(root / 'vendor/etc/init/qseecomd.rc', textwrap.dedent("""\
        on init
            start vendor.qseecomd

        service vendor.qseecomd /vendor/bin/qseecomd
            class core
            user root
            group root drmrpc

        on property:vendor.car.hiber=hiberExit
           stop vendor.qseecomd
           start vendor.qseecomd
    """))
    _write(root / 'vendor/etc/init/touchpad-inhibitor-service.rc', textwrap.dedent("""\
        service touchpad-inhibitor /vendor/bin/hw/touchpad-inhibitor
            class core
            user system
            group input
    """))
    _write(root / 'vendor/etc/init/android.hardware.usb-service.qti.rc', textwrap.dedent("""\
        service vendor.usb-hal /vendor/bin/hw/android.hardware.usb-service.qti
            class hal
            user system
            group system mtp usb
    """))
    _write(root / 'vendor/etc/vintf/manifest/android.hardware.usb-service.qti.xml', textwrap.dedent("""\
        <manifest version="9.0" type="device">
            <hal format="aidl">
                <name>android.hardware.usb</name>
                <version>3</version>
                <fqname>IUsb/default</fqname>
            </hal>
        </manifest>
    """))
    _write(root / 'vendor/etc/vintf/manifest/android.hardware.usb.gadget-service.qti.xml', textwrap.dedent("""\
        <manifest version="9.0" type="device">
            <hal format="aidl">
                <name>android.hardware.usb.gadget</name>
                <fqname>IUsbGadget/default</fqname>
            </hal>
        </manifest>
    """))

    vermagic = b'\x00\x00vermagic=6.18.60-sq2-android1 SMP preempt mod_unload aarch64\x00' if matching_krel else (
        b'\x00\x00vermagic=6.12.81-android16-6-g08dd7534bf44 SMP preempt mod_unload aarch64\x00'
    )
    for part, mod in (('system_dlkm', 'rfkill.ko'), ('vendor_dlkm', 'bam_dma.ko')):
        _write(root / part / 'etc/build.prop', f'ro.{part}.build.id=TEST\n')
        ko = root / part / 'lib/modules' / mod
        ko.parent.mkdir(parents=True, exist_ok=True)
        ko.write_bytes(b'\x7fELF\x00\x00' + vermagic)

    # Donor Dell OEM priv-apps, privapp-permissions, sysconfig, and unrelated Dell USB audio profile.
    for stub in ('DellSupportAssistStub', 'DellDiagnosticsStub', 'DellRepairStub'):
        _write(root / f'product/priv-app/{stub}/{stub}.apk', f'fake-{stub}\n')
    for pkg in ('com.dell.supportassist.alos', 'com.dell.diagnostics', 'com.dell.luxsharerma'):
        _write(root / f'product/etc/permissions/privapp-permissions-{pkg}.xml',
               f'<permissions><privapp-permissions package="{pkg}"/></permissions>\n')
        _write(root / f'product/etc/sysconfig/preinstalled-packages-{pkg}.xml',
               f'<config><install-in-user-type package="{pkg}"/></config>\n')
    _write(root / 'vendor/etc/audio/for_all_boards/DELL_PROFESSIONAL_SOUND_BAR_AE5/audio_config.xml',
           '<audio_config/>\n')


def _run_hygiene_stage(shell_cmd, root: Path, dev: Path, sysfs: Path, krel: str = '6.18.60-sq2-android1'):
    script = (ROOT / 'probe/test2/spx-service-hygiene-stage').read_text(encoding='utf-8')
    fake_mount = textwrap.dedent("""\
        mount() {
            if [ "$1" = "--bind" ]; then
                if [ -d "$2" ] && [ -d "$3" ]; then
                    rm -rf "$3"
                    mkdir -p "$3"
                    cp -a "$2/." "$3/" 2>/dev/null || true
                else
                    cp "$2" "$3"
                fi
                return 0
            fi
            return 1
        }
    """)
    env = os.environ.copy()
    env.update({
        'SPX_HYGIENE_ROOT': str(root),
        'SPX_HYGIENE_STAGE': str(root / 'debug_ramdisk/spx-service-hygiene'),
        'SPX_HYGIENE_DEV': str(dev),
        'SPX_HYGIENE_SYSFS': str(sysfs),
        'SPX_HYGIENE_KREL': krel,
    })
    return subprocess.run(
        shell_cmd,
        input=fake_mount + script,
        env=env,
        text=True,
        capture_output=True,
        check=True,
    )


class S2IdleHygieneTests(unittest.TestCase):
    def test_shell_syntax_both_sh_and_busybox(self):
        scripts = [
            'probe/test2/spx-service-hygiene-stage',
            'probe/test2/spx-oem-cleanup-boot',
            'probe/test2/prepare-native-gpu',
            'probe/test2/lab-input-posture',
            'probe/test2/touch-resume-watch',
            'probe/test2/spx-lower-usb',
            'probe/test2/spx-advanced-control-boot',
            'probe/ota/spx-ota-boot',
            'probe/test2/touch-calibration-boot',
        ]
        for shell_cmd in _shells():
            for rel in scripts:
                with self.subTest(shell=shell_cmd[0], script=rel):
                    subprocess.run([*shell_cmd, '-n', str(ROOT / rel)], check=True)

    def test_runtime_boot_policy_accepts_all_modified_scripts(self):
        mapping = {
            'bin/spx-service-hygiene-stage': 'probe/test2/spx-service-hygiene-stage',
            'bin/spx-oem-cleanup-boot': 'probe/test2/spx-oem-cleanup-boot',
            'bin/prepare-native-gpu': 'probe/test2/prepare-native-gpu',
            'bin/lab-input-posture': 'probe/test2/lab-input-posture',
            'bin/touch-resume-watch': 'probe/test2/touch-resume-watch',
            'bin/spx-lower-usb': 'probe/test2/spx-lower-usb',
            'bin/spx-advanced-control-boot': 'probe/test2/spx-advanced-control-boot',
            'bin/spx-ota-boot': 'probe/ota/spx-ota-boot',
            'bin/touch-calibration-boot': 'probe/test2/touch-calibration-boot',
        }
        for name, rel in mapping.items():
            with self.subTest(name=name):
                src = (ROOT / rel).read_bytes()
                after = runtime_boot_policy.without_hash_checks(name, src)
                self.assertEqual(runtime_boot_policy.without_hash_checks(name, after), after)
                subprocess.run(['sh', '-n'], input=after, check=True)

    def test_service_hygiene_stage_spx_profile_and_idempotency(self):
        for shell_cmd in _shells():
            with self.subTest(shell=shell_cmd[0]), tempfile.TemporaryDirectory() as td:
                td_p = Path(td)
                root = td_p / 'newroot'
                dev = td_p / 'dev'
                sysfs = td_p / 'sys'
                dev.mkdir()
                _make_fake_root_fixture(root, with_testscripts=False, matching_krel=False)

                # Surface Pro X hardware profile: SDSP only, tablet switch (2) + headset (14), mainline USB.
                _write(sysfs / 'firmware/devicetree/base/soc@0/remoteproc@2400000/glink-edge/fastrpc/label', 'sdsp\x00')
                _write(sysfs / 'class/input/event18/device/capabilities/sw', '2\n')
                _write(sysfs / 'class/input/event21/device/capabilities/sw', '14\n')
                (sysfs / 'bus/platform/devices/a600000.usb').mkdir(parents=True)
                (sysfs / 'bus/platform/devices/surface-typec').mkdir(parents=True)

                res = _run_hygiene_stage(shell_cmd, root, dev, sysfs)
                self.assertIn('SPX_SERVICE_HYGIENE_READY', res.stdout)

                # 1. Boot init SIGABRT fixes
                ufs = (root / 'vendor/etc/init/hw/init.qti.ufs.rc').read_text()
                self.assertIn('# spx-hygiene (missing_ufs_debug_script):', ufs)
                self.assertIn('write /sys/bus/platform/devices/1d84000.ufshc/pm_qos_enable 0', ufs)

                qcom = (root / 'vendor/etc/init/hw/init.qcom.rc').read_text()
                self.assertIn('start qcom-post-boot', qcom)
                self.assertIn('# spx-hygiene (missing_qcom_testscripts):', qcom)
                self.assertNotIn('\n    start qti-testscripts\n', qcom)

                # 2. TPM2 absent -> udsattestation disabled
                self.assertIn('    disabled\n', (root / 'system/etc/init/udsattestation.rc').read_text())

                # 3. ChromeOS EC absent -> timberslide/ec_crash_collector/ec-battery-limits/ec-executor disabled
                ts = (root / 'system/etc/init/timberslide.rc').read_text()
                self.assertIn('mkdir /data/misc/timberslide/', ts)
                self.assertIn('    disabled\n', ts)

                ecc = (root / 'system/etc/init/ec_crash_collector.rc').read_text()
                self.assertIn('    disabled\n', ecc)
                self.assertIn('# spx-hygiene (no_cros_ec):', ecc)

                health = (root / 'vendor/etc/init/android.hardware.health-service.android-desktop.rc').read_text()
                health_blocks = health.split('service ')
                self.assertNotIn('disabled', health_blocks[1])  # vendor.health-default untouched
                self.assertNotIn('disabled', health_blocks[2])  # vendor.charger untouched
                self.assertIn('    disabled\n', health_blocks[3])  # vendor.ec-battery-limits-setprop disabled

                ece = (root / 'vendor/etc/init/ec-executor.rc').read_text()
                self.assertIn('    disabled\n', ece)
                self.assertIn('# spx-hygiene (no_cros_ec):     interface aidl', ece)
                ece_xml = (root / 'vendor/etc/vintf/manifest/vendor.google.desktop.ec-executor.xml').read_text()
                self.assertNotIn('IEcExecutor/default', ece_xml)
                self.assertIn('<manifest version="9.0" type="device"/>', ece_xml)

                # 4. Suspend notifier & diagnostics
                sns = (root / 'vendor/etc/init/suspend_notification_service.rc').read_text()
                self.assertIn('    disabled\n', sns)
                self.assertIn('# spx-hygiene (no_suspend_notifier):     interface aidl', sns)
                sns_xml = (root / 'vendor/etc/vintf/manifest/com.android.desktop.power.suspend_notification_service.xml').read_text()
                self.assertNotIn('ISuspendNotificationService/default', sns_xml)
                self.assertIn('    disabled\n', (root / 'vendor/etc/init/suspend_diagnostics.rc').read_text())

                # 5. FastRPC ADSP/CDSP disabled (SDSP untouched)
                self.assertIn('    disabled\n', (root / 'vendor/etc/init/vendor.qti.adsprpc-guestos-service.rc').read_text())
                audio_adsp = (root / 'vendor/etc/init/vendor.qti.audio-adsprpc-service.rc').read_text()
                self.assertIn('    disabled\n', audio_adsp)
                self.assertIn('# spx-hygiene (no_adsp_fastrpc):     start vendor.audioadsprpcd_audiopd', audio_adsp)
                self.assertIn('    disabled\n', (root / 'vendor/etc/init/vendor.qti.cdsprpc-service.rc').read_text())
                sensors_rc = (root / 'vendor/etc/init/init.vendor.sensors.rc').read_text()
                self.assertIn('chown system system /mnt/vendor/persist/sensors', sensors_rc)
                self.assertIn('# spx-hygiene (no_adsp_fastrpc):     start vendor.sensors', sensors_rc)

                # 6. QSEECOM disabled (both pre-service and post-service start triggers commented)
                qsee = (root / 'vendor/etc/init/qseecomd.rc').read_text()
                self.assertIn('    disabled\n', qsee)
                self.assertEqual(qsee.count('# spx-hygiene (no_qseecom_device):'), 2)

                # 7. Touchpad inhibitor disabled
                self.assertIn('    disabled\n', (root / 'vendor/etc/init/touchpad-inhibitor-service.rc').read_text())

                # 8. Downstream QTI USB HAL disabled & VINTF manifests cleared
                self.assertIn('    disabled\n', (root / 'vendor/etc/init/android.hardware.usb-service.qti.rc').read_text())
                self.assertNotIn('IUsb/default', (root / 'vendor/etc/vintf/manifest/android.hardware.usb-service.qti.xml').read_text())
                self.assertNotIn('IUsbGadget/default', (root / 'vendor/etc/vintf/manifest/android.hardware.usb.gadget-service.qti.xml').read_text())

                # 9. Mismatched donor DLKM modules masked, build.prop preserved
                self.assertEqual(list((root / 'system_dlkm/lib/modules').iterdir()), [])
                self.assertEqual(list((root / 'vendor_dlkm/lib/modules').iterdir()), [])
                self.assertIn('TEST', (root / 'system_dlkm/etc/build.prop').read_text())
                self.assertIn('TEST', (root / 'vendor_dlkm/etc/build.prop').read_text())

                # 10. Realtek rtl_nic firmware staged
                self.assertEqual((root / 'debug_ramdisk/sq2-firmware/rtl_nic/rtl8156b-2.fw').read_text(), 'fake-rtl8156b-fw')

                # 11. Donor Dell OEM apps/services masked on non-Dell hardware; USB soundbar profile preserved
                for stub in ('DellSupportAssistStub', 'DellDiagnosticsStub', 'DellRepairStub'):
                    self.assertEqual(list((root / f'product/priv-app/{stub}').iterdir()), [])
                for pkg in ('com.dell.supportassist.alos', 'com.dell.diagnostics', 'com.dell.luxsharerma'):
                    perm = (root / f'product/etc/permissions/privapp-permissions-{pkg}.xml').read_text()
                    self.assertIn('<permissions/>', perm)
                    self.assertNotIn(pkg, perm)
                    cfg = (root / f'product/etc/sysconfig/preinstalled-packages-{pkg}.xml').read_text()
                    self.assertIn('<config/>', cfg)
                    self.assertNotIn(pkg, cfg)
                self.assertIn(
                    '<audio_config/>',
                    (root / 'vendor/etc/audio/for_all_boards/DELL_PROFESSIONAL_SOUND_BAR_AE5/audio_config.xml').read_text(),
                )

                # Idempotency: re-running must leave every single file byte-identical
                snapshot = {
                    p.relative_to(root): p.read_bytes()
                    for p in root.rglob('*')
                    if p.is_file() and 'debug_ramdisk/spx-service-hygiene' not in p.as_posix()
                }
                _run_hygiene_stage(shell_cmd, root, dev, sysfs)
                after = {
                    p.relative_to(root): p.read_bytes()
                    for p in root.rglob('*')
                    if p.is_file() and 'debug_ramdisk/spx-service-hygiene' not in p.as_posix()
                }
                self.assertEqual(snapshot, after)

    def test_service_hygiene_stage_preserves_when_hardware_present(self):
        with tempfile.TemporaryDirectory() as td:
            td_p = Path(td)
            root = td_p / 'newroot'
            dev = td_p / 'dev'
            sysfs = td_p / 'sys'
            dev.mkdir()
            _make_fake_root_fixture(root, with_testscripts=True, matching_krel=True)

            # Symlink real /dev/null char device for hardware-present gates.
            for node in ('tpm0', 'cros_ec', 'suspend_notifier', 'qseecom'):
                os.symlink('/dev/null', dev / node)
            _write(sysfs / 'class/tpm/tpm0/tpm_version_major', '2\n')
            _write(sysfs / 'firmware/devicetree/base/soc@0/remoteproc@17300000/glink-edge/fastrpc/label', 'adsp\x00')
            _write(sysfs / 'firmware/devicetree/base/soc@0/remoteproc@9890000/glink-edge/fastrpc/label', 'cdsp\x00')
            _write(sysfs / 'class/input/event0/device/capabilities/sw', '0 0000000000000015\n')  # bit 0 set (SW_LID)
            (sysfs / 'bus/platform/devices/a600000.dwc3').mkdir(parents=True)
            (sysfs / 'bus/platform/devices/a600000.usb').mkdir(parents=True)
            _write(sysfs / 'class/dmi/id/sys_vendor', 'Dell Inc.\n')

            before = {
                p.relative_to(root): p.read_bytes()
                for p in root.rglob('*')
                if p.is_file()
            }
            res = _run_hygiene_stage(['sh'], root, dev, sysfs)
            self.assertNotIn('SPX_HYGIENE_DISABLE_', res.stdout)
            self.assertNotIn('SPX_HYGIENE_MASK_', res.stdout)
            for rel, content in before.items():
                self.assertEqual((root / rel).read_bytes(), content, f'Unexpected mutation of {rel}')
            self.assertTrue((root / 'system_dlkm/lib/modules/rfkill.ko').is_file())
            self.assertTrue((root / 'vendor_dlkm/lib/modules/bam_dma.ko').is_file())

    def test_run_with_timeout_no_orphans_under_busybox_and_sh(self):
        # Extract run_with_timeout from lab-input-posture and timeout() from touch-calibration-boot
        posture_src = (ROOT / 'probe/test2/lab-input-posture').read_text()
        start = posture_src.index('run_with_timeout() {')
        end = posture_src.index('\nandroid() {', start)
        rwt_fn = posture_src[start:end]

        for shell_cmd in _shells():
            with self.subTest(shell=shell_cmd[0]):
                test_script = rwt_fn + textwrap.dedent("""\

                    set -eu
                    i=0
                    while [ "$i" -lt 40 ]; do
                        out=$(run_with_timeout 17 printf 'val-%s\\n' "$i")
                        [ "$out" = "val-$i" ] || exit 11
                        i=$((i + 1))
                    done
                    rc=0
                    run_with_timeout 17 sh -c 'exit 42' || rc=$?
                    [ "$rc" = 42 ] || exit 12
                    rc=0
                    run_with_timeout 1 sleep 17 || rc=$?
                    [ "$rc" -ne 0 ] || exit 13
                    echo "OK rc_timeout=$rc"
                """)
                res = subprocess.run([*shell_cmd, '-c', test_script], text=True, capture_output=True, timeout=15)
                self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
                self.assertIn('OK rc_timeout=', res.stdout)
                self.assertEqual(res.stderr.strip(), '')
                ps = subprocess.check_output(['ps', '-eo', 'pid,ppid,args'], text=True)
                strays = [line for line in ps.splitlines() if 'sleep 17' in line and 'python' not in line]
                self.assertEqual(strays, [], f'Leaked orphan sleep processes: {strays}')

    def test_lab_input_posture_stops_idle_storm_and_wakes_on_cover_open(self):
        with tempfile.TemporaryDirectory() as td:
            td_p = Path(td)
            sysfs = td_p / 'sys'
            cover = sysfs / 'bus/surface_aggregator/devices/01:0e:01:00:01/state'
            _write(cover, 'laptop\n')
            _write(sysfs / 'class/drm/card0-eDP-1/enabled', 'disabled\n')
            _write(sysfs / 'class/drm/card0-eDP-1/dpms', 'Off\n')

            log_file = td_p / 'calls.log'
            wrapper = _write(td_p / 'android-wrapper', textwrap.dedent(f"""\
                #!/bin/sh
                printf '%s\\n' "$*" >> "{log_file}"
                case "$*" in
                    'getprop persist.sq2.input_posture') printf 'manual\\n' ;;
                    'getprop sys.boot_completed') printf '1\\n' ;;
                    'cmd device_state print-states') printf "DeviceState{{identifier=2, name='OPEN'}}\\n" ;;
                    'cmd device_state base-state '*) exit 0 ;;
                    'cmd power wakeup') exit 0 ;;
                    'dumpsys device_state') printf 'OK\\n' ;;
                esac
            """), 0o755)

            env = os.environ.copy()
            env.update({
                'SPX_POSTURE_SYSFS': str(sysfs),
                'SPX_POSTURE_COVER': str(cover),
                'SPX_POSTURE_ANDROID_WRAPPER': str(wrapper),
                'SPX_POSTURE_SERVER_PID': '3343',
                'SPX_POSTURE_POLL_SLEEP': '0.01',
                'SPX_POSTURE_PROP_TICKS': '3',
                'SPX_POSTURE_MAX_LOOPS': '12',
            })
            res = subprocess.run(['sh', str(ROOT / 'probe/test2/lab-input-posture')], env=env, text=True, capture_output=True, check=True)
            self.assertIn('SQ2_INPUT_POSTURE: physical=laptop base=2 system_server=3343', res.stdout)
            calls = log_file.read_text().splitlines()
            # Display is OFF: only 1 startup getprop check across all 12 loops!
            self.assertEqual(calls.count('getprop persist.sq2.input_posture'), 1, calls)
            self.assertEqual(calls.count('cmd device_state base-state 2'), 1, calls)

            # Scenario 2: cover transitions closed -> laptop while display is OFF triggers immediate wakeup.
            log_file.write_text('')
            _write(cover, 'closed\n')
            env2 = dict(env, SPX_POSTURE_MAX_LOOPS='0', SPX_POSTURE_POLL_SLEEP='0.02')
            p = subprocess.Popen(['sh', str(ROOT / 'probe/test2/lab-input-posture')], env=env2, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if log_file.exists() and 'cmd device_state base-state 0' in log_file.read_text():
                        break
                    time.sleep(0.02)
                self.assertIn('cmd device_state base-state 0', log_file.read_text())
                _write(cover, 'laptop\n')
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if 'cmd power wakeup' in log_file.read_text():
                        break
                    time.sleep(0.02)
                calls2 = log_file.read_text().splitlines()
                self.assertIn('cmd device_state base-state 2', calls2)
                self.assertIn('cmd power wakeup', calls2)
            finally:
                p.terminate()
                p.wait(timeout=5)
                if p.stdout:
                    p.stdout.close()
                if p.stderr:
                    p.stderr.close()

            # Scenario 3: persist.sq2.input_posture=auto exits cleanly without calling base-state.
            log_file.write_text('')
            _write(wrapper, textwrap.dedent(f"""\
                #!/bin/sh
                printf '%s\\n' "$*" >> "{log_file}"
                [ "$*" = "getprop persist.sq2.input_posture" ] && printf 'auto\\n'
            """), 0o755)
            res3 = subprocess.run(['sh', str(ROOT / 'probe/test2/lab-input-posture')], env=env, text=True, capture_output=True, check=True)
            self.assertIn('automatic posture requested', res3.stdout)
            self.assertNotIn('base-state', log_file.read_text())

    def test_touch_resume_watch_defers_while_panel_off_and_restarts_once_on_wake(self):
        with tempfile.TemporaryDirectory() as td:
            td_p = Path(td)
            sysfs = td_p / 'sys'
            run_dir = td_p / 'run'
            data_root = td_p / 'android'
            spi_dev = sysfs / 'devices/spi0.0'
            _write(spi_dev / 'ready', 'ready\n')
            _write(spi_dev / 'resume_count', '0\n')
            _write(run_dir / 'iptsd-hidraw', '/dev/hidraw0\n')
            _write(run_dir / 'iptsd-ready', str(spi_dev / 'ready') + '\n')

            edp_en = _write(sysfs / 'class/drm/card0-eDP-1/enabled', 'disabled\n')
            edp_dpms = _write(sysfs / 'class/drm/card0-eDP-1/dpms', 'Off\n')
            bl_pwr = _write(sysfs / 'class/backlight/backlight/bl_power', '4\n')
            bl_br = _write(sysfs / 'class/backlight/backlight/brightness', '0\n')

            restarts = td_p / 'restarts.log'
            touch_svc = _write(td_p / 'touch-services', f'#!/bin/sh\necho restart >> "{restarts}"\n', 0o755)

            env = os.environ.copy()
            env.update({
                'SPX_TOUCH_SYSFS': str(sysfs),
                'SPX_TOUCH_RUN': str(run_dir),
                'SPX_TOUCH_ROOT': str(data_root),
                'SPX_TOUCH_SERVICES': str(touch_svc),
                'SPX_TOUCH_DISABLED': str(run_dir / 'touch-disabled'),
                'SPX_TOUCH_POLL_SLEEP': '0.02',
            })
            log = td_p / 'watch.log'
            with log.open('w') as lf:
                p = subprocess.Popen(['sh', str(ROOT / 'probe/test2/touch-resume-watch')], env=env, stdout=lf, stderr=lf)
            try:
                time.sleep(0.08)
                # Simulate 5 brief s2idle wakeups while screen is OFF.
                for count in range(1, 6):
                    _write(spi_dev / 'resume_count', f'{count}\n')
                    time.sleep(0.05)
                self.assertFalse(restarts.exists(), 'iptsd restarted while panel was OFF')

                # Turn panel ON -> must coalesce into exactly ONE reinit at count=5.
                _write(edp_en, 'enabled\n')
                _write(edp_dpms, 'On\n')
                _write(bl_pwr, '0\n')
                _write(bl_br, '1200\n')
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if restarts.exists():
                        break
                    time.sleep(0.02)
                time.sleep(0.1)
                self.assertEqual(restarts.read_text().splitlines(), ['restart'])
                self.assertIn('TOUCH_RESUME_REINITIALIZE count=5', log.read_text())

                # Verify persistent touch_disabled=1 skips restart cleanly.
                _write(data_root / 'data/vendor/spx-advanced-control/touch_disabled', '1\n')
                _write(spi_dev / 'resume_count', '6\n')
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if 'TOUCH_RESUME_SKIPPED_DISABLED count=6' in log.read_text():
                        break
                    time.sleep(0.02)
                self.assertIn('TOUCH_RESUME_SKIPPED_DISABLED count=6', log.read_text())
                self.assertTrue((run_dir / 'touch-disabled').exists())
                self.assertEqual(restarts.read_text().splitlines(), ['restart'])
            finally:
                p.terminate()
                p.wait(timeout=5)
                if p.stdout:
                    p.stdout.close()
                if p.stderr:
                    p.stderr.close()

    def test_spx_lower_usb_host_mode_respects_vintf_declaration(self):
        for declared in (False, True):
            with self.subTest(vintf_declared=declared), tempfile.TemporaryDirectory() as td:
                td_p = Path(td)
                root = td_p / 'root'
                role = td_p / 'lower_data_role'
                _write(role, 'device\n')
                gadget = root / 'config/usb_gadget/g1'
                _write(gadget / 'UDC', '')
                (gadget / 'configs/b.1').mkdir(parents=True)
                vintf = root / 'vendor/etc/vintf/manifest/android.hardware.usb-service.qti.xml'
                if declared:
                    _write(vintf, '<manifest version="9.0" type="device"><hal><fqname>IUsb/default</fqname></hal></manifest>\n')
                else:
                    _write(vintf, '<manifest version="9.0" type="device"/>\n')

                calls_log = td_p / 'usb-calls.log'
                wrapper = _write(td_p / 'android', textwrap.dedent(f"""\
                    #!/bin/sh
                    printf '%s\\n' "$*" >> "{calls_log}"
                    case "$*" in
                        '/system/bin/getprop sys.usb.state') printf 'none\\n' ;;
                        '/system/bin/getprop sys.usb.config') printf 'none\\n' ;;
                        '/system/bin/getprop ro.adb.secure') printf '1\\n' ;;
                    esac
                """), 0o755)
                env = os.environ.copy()
                env.update({
                    'SPX_LOWER_USB_ROOT': str(root),
                    'SPX_LOWER_USB_ROLE': str(role),
                    'SPX_LOWER_USB_ANDROID_WRAPPER': str(wrapper),
                    'SPX_USB_VINTF': str(vintf),
                })
                lower_usb = td_p / 'spx-lower-usb'
                shutil.copy2(ROOT / 'probe/test2/spx-lower-usb', lower_usb)
                lower_usb.chmod(0o755)
                res = subprocess.run([str(lower_usb), 'host'], env=env, text=True, capture_output=True, check=True)
                self.assertIn('LOWER_USB_MODE=host', res.stdout)
                self.assertEqual(role.read_text().strip(), 'host')
                calls = calls_log.read_text().splitlines()
                self.assertIn('/system/bin/setprop ctl.start adbd', calls)
                if declared:
                    self.assertIn('/system/bin/setprop sys.usb.configfs 2', calls)
                    self.assertIn('/system/bin/setprop ctl.start vendor.usb-hal', calls)
                else:
                    self.assertIn('/system/bin/setprop sys.usb.configfs 1', calls)
                    self.assertNotIn('/system/bin/setprop ctl.start vendor.usb-hal', calls)

    def test_spx_lower_usb_auto_boot_and_mode_lifecycle(self):
        with tempfile.TemporaryDirectory(prefix='spx-lower-usb-auto-') as td:
            td_p = Path(td)
            root = td_p / 'root'
            sysfs = td_p / 'sys'
            run_dir = td_p / 'run'
            logs_dir = td_p / 'logs'
            run_dir.mkdir(parents=True)
            logs_dir.mkdir(parents=True)

            watch_bin = td_p / 'spx-lower-usb-watch'
            subprocess.run([
                'gcc', '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror',
                str(ROOT / 'probe/test2/spx-lower-usb-watch.c'), '-o', str(watch_bin),
            ], check=True)
            lower_usb = td_p / 'spx-lower-usb'
            shutil.copy2(ROOT / 'probe/test2/spx-lower-usb', lower_usb)
            lower_usb.chmod(0o755)

            # Synthetic sysfs: surface-typec, UDC symlink, DT node, and UCSI port0
            typec_dev = sysfs / 'bus/platform/devices/surface-typec'
            role = _write(typec_dev / 'lower_data_role', 'host\n')
            auto_dp = _write(typec_dev / 'lower_auto_dp', '0\n')
            dp_state = _write(typec_dev / 'lower_dp_state', 'none\n')
            storage = _write(typec_dev / 'storage_ports', '0\n')
            (sysfs / 'firmware/devicetree/base/soc@0/usb@a6f8800/usb@a600000').mkdir(parents=True)
            udc_real = sysfs / 'devices/platform/soc@0/a6f8800.usb/a600000.usb/udc/a600000.usb'
            _write(udc_real / 'state', 'not attached\n')
            (sysfs / 'class/udc').mkdir(parents=True)
            (sysfs / 'class/udc/a600000.usb').symlink_to('../../devices/platform/soc@0/a6f8800.usb/a600000.usb/udc/a600000.usb')
            ucsi_p0 = sysfs / 'devices/platform/spx-ucsi-transport/0019:045E:0922.0001/typec/port0'
            ucsi_p0.mkdir(parents=True)
            (sysfs / 'class/typec').mkdir(parents=True)
            (sysfs / 'class/typec/port0').symlink_to('../../devices/platform/spx-ucsi-transport/0019:045E:0922.0001/typec/port0')

            # Synthetic Android gadget + props wrapper
            gadget = root / 'config/usb_gadget/g1'
            _write(gadget / 'UDC', '')
            (gadget / 'functions/ffs.adb').mkdir(parents=True)
            (gadget / 'configs/b.1').mkdir(parents=True)
            vintf = _write(root / 'vendor/etc/vintf/manifest/android.hardware.usb-service.qti.xml', '<manifest version="9.0" type="device"/>\n')
            props_file = td_p / 'props.txt'
            props_file.write_text('sys.usb.config=none\nsys.usb.state=none\nro.adb.secure=1\ninit.svc.vendor.usb-hal=stopped\n')
            calls_log = td_p / 'android-calls.log'
            wrapper = _write(td_p / 'mock-android', textwrap.dedent(f"""\
                #!/usr/bin/env python3
                from pathlib import Path
                import sys
                props_p = Path({str(props_file)!r})
                udc_p = Path({str(gadget / 'UDC')!r})
                log_p = Path({str(calls_log)!r})
                args = sys.argv[1:]
                with log_p.open('a') as f:
                    f.write(' '.join(args) + '\\n')
                props = dict(line.split('=', 1) for line in props_p.read_text().splitlines() if '=' in line)
                if len(args) >= 2 and args[0] == '/system/bin/getprop':
                    print(props.get(args[1], ''))
                elif len(args) >= 3 and args[0] == '/system/bin/setprop':
                    k, v = args[1], args[2]
                    props[k] = v
                    if k == 'sys.usb.config' and v in ('none', 'adb'):
                        props['sys.usb.state'] = v
                        if v == 'adb':
                            udc_p.write_text(props.get('sys.usb.controller', '') + '\\n')
                        else:
                            udc_p.write_text('')
                    props_p.write_text(''.join(f'{{kk}}={{vv}}\\n' for kk, vv in props.items()))
            """), 0o755)

            env = os.environ.copy()
            env.update({
                'SPX_LOWER_USB_ROOT': str(root),
                'SPX_LOWER_USB_SYSFS': str(sysfs),
                'SPX_LOWER_USB_RUN': str(run_dir),
                'SPX_LOWER_USB_LOGS': str(logs_dir),
                'SPX_LOWER_USB_ROLE': str(role),
                'SPX_LOWER_USB_AUTO_DP': str(auto_dp),
                'SPX_LOWER_USB_DP_STATE': str(dp_state),
                'SPX_LOWER_USB_STORAGE': str(storage),
                'SPX_LOWER_USB_WATCH_BIN': str(watch_bin),
                'SPX_LOWER_USB_ANDROID_WRAPPER': str(wrapper),
                'SPX_USB_VINTF': str(vintf),
            })
            pidfile = run_dir / 'spx-lower-usb-watch.pid'
            try:
                # 1. boot without saved lower-mode defaults to auto, binds ADB, and starts watcher
                res = subprocess.run([str(lower_usb), 'boot'], env=env, text=True, capture_output=True, check=True)
                self.assertIn('LOWER_USB_MODE=auto', res.stdout)
                self.assertIn('mode=auto', res.stdout)
                self.assertIn('watch=running', res.stdout)
                self.assertEqual(role.read_text().strip(), 'device')
                self.assertEqual(auto_dp.read_text().strip(), '1')
                self.assertEqual((gadget / 'UDC').read_text().strip(), 'a600000.usb')
                self.assertTrue(pidfile.is_file())
                wpid = int(pidfile.read_text().strip())
                os.kill(wpid, 0)

                # 2. adb --save while already bound uses idempotent fast-path (no UDC clear), stops watcher
                calls_log.write_text('')
                res = subprocess.run([str(lower_usb), 'adb', '--save'], env=env, text=True, capture_output=True, check=True)
                self.assertIn('LOWER_USB_MODE=adb', res.stdout)
                self.assertIn('watch=stopped', res.stdout)
                self.assertEqual(auto_dp.read_text().strip(), '0')
                self.assertEqual((root / 'data/vendor/surface-usb/lower-mode').read_text().strip(), 'adb')
                self.assertNotIn('sys.usb.config none', calls_log.read_text())
                with self.assertRaises(ProcessLookupError):
                    os.kill(wpid, 0)

                # 3. host --save sets host and persists; boot preserves saved host
                res = subprocess.run([str(lower_usb), 'host', '--save'], env=env, text=True, capture_output=True, check=True)
                self.assertIn('LOWER_USB_MODE=host', res.stdout)
                self.assertEqual(role.read_text().strip(), 'host')
                self.assertEqual((root / 'data/vendor/surface-usb/lower-mode').read_text().strip(), 'host')
                res = subprocess.run([str(lower_usb), 'boot'], env=env, text=True, capture_output=True, check=True)
                self.assertEqual(role.read_text().strip(), 'host')
                self.assertFalse(pidfile.exists())

                # 4. auto --save restores auto mode and watcher
                res = subprocess.run([str(lower_usb), 'auto', '--save'], env=env, text=True, capture_output=True, check=True)
                self.assertIn('LOWER_USB_MODE=auto', res.stdout)
                self.assertEqual(role.read_text().strip(), 'device')
                self.assertEqual(auto_dp.read_text().strip(), '1')
                self.assertEqual((root / 'data/vendor/surface-usb/lower-mode').read_text().strip(), 'auto')
                self.assertTrue(pidfile.is_file())

                # 5. boot storage lock rejects adb and keeps host in auto without starting watcher
                storage.write_text('1\n')
                res_bad = subprocess.run([str(lower_usb), 'adb'], env=env, text=True, capture_output=True)
                self.assertNotEqual(res_bad.returncode, 0)
                self.assertIn('boot storage', res_bad.stderr)
                res_auto = subprocess.run([str(lower_usb), 'auto'], env=env, text=True, capture_output=True, check=True)
                self.assertEqual(role.read_text().strip(), 'host')
                self.assertEqual(auto_dp.read_text().strip(), '0')
                self.assertIn('boot_storage_locked=1', res_auto.stdout)
                self.assertIn('watch=stopped', res_auto.stdout)
            finally:
                if pidfile.is_file():
                    try:
                        os.kill(int(pidfile.read_text().strip()), signal.SIGKILL)
                    except Exception:
                        pass

    def test_disk_image_wires_service_hygiene_and_rtl_nic(self):
        init_src = (ROOT / 'probe/test2/init-googlebook-native').read_bytes().replace(b'\r\n', b'\n')
        init_src = init_src.replace(ANCHOR, HOOK + ANCHOR, 1)
        gpu_src = (ROOT / 'probe/test2/prepare-native-gpu').read_bytes().replace(b'\r\n', b'\n')
        # Simulate base initramfs whose prepare-native-gpu predates rtl_nic staging
        old_gpu = gpu_src.replace(
            b'if [ -d "$target/vendor/firmware/rtl_nic" ] && [ ! -d "$dest/rtl_nic" ]; then\n'
            b'    cp -a "$target/vendor/firmware/rtl_nic" "$dest/rtl_nic"\n'
            b'fi\n',
            b'',
        )
        entries = {
            'init': (0o100755, init_src),
            'bin/apply-single-user': (0o100755, b'#!/bin/busybox sh\n'),
            'bin/prepare-native-gpu': (0o100755, old_gpu),
            'bin/lab-input-posture': (0o100755, b'#!/bin/busybox sh\n'),
            'bin/spx-lower-usb': (0o100755, b'#!/bin/sh\n'),
        }
        raw = b''.join(cpio.entry(n, b, m) for n, (m, b) in entries.items()) + cpio.entry('TRAILER!!!', b'', 0)
        raw += b'\0' * (-len(raw) % 512)
        base = gzip.compress(raw, mtime=0)
        parts = [{'name': 'esp', 'index': 1, 'bytes': 512 * 1024 * 1024}]
        patched, meta, _ = disk_image.storage_payload(base, parts, anonymous=True)
        after = cpio.entries(gzip.decompress(patched))

        self.assertIn('bin/spx-service-hygiene-stage', after)
        self.assertIn('bin/spx-oem-cleanup-boot', after)
        self.assertIn('bin/lab-input-posture', after)
        self.assertIn('bin/spx-lower-usb', after)
        self.assertIn('bin/spx-lower-usb-watch', after)
        self.assertEqual(after['bin/spx-lower-usb-watch'][1][:6], b'\x7fELF\x02\x01')
        self.assertIn('bin/prepare-native-gpu', after)

        patched_init = after['init'][1]
        self.assertIn(b'/bin/spx-service-hygiene-stage > /logs/spx-service-hygiene.txt', patched_init)
        self.assertIn(b'/bin/spx-oem-cleanup-boot > /logs/spx-oem-cleanup-boot.txt 2>&1 &', patched_init)
        self.assertLess(
            patched_init.index(b'/bin/gscd-availability-stage'),
            patched_init.index(b'/bin/spx-service-hygiene-stage'),
        )
        self.assertLess(
            patched_init.index(b'/bin/spx-service-hygiene-stage'),
            patched_init.index(b'/bin/spx-oem-cleanup-boot'),
        )
        self.assertLess(
            patched_init.index(b'/bin/spx-oem-cleanup-boot'),
            patched_init.index(b'exec native-handoff /newroot'),
        )

        patched_gpu = after['bin/prepare-native-gpu'][1]
        self.assertIn(b'vendor/firmware/rtl_nic', patched_gpu)
        self.assertNotIn(b'sha256sum', patched_gpu)
        self.assertNotIn(b'timeout 8 nsenter', after['bin/lab-input-posture'][1])
        self.assertIn(b'run_with_timeout', after['bin/lab-input-posture'][1])

    def test_bridge_daemons_eliminate_busybox_timeout_and_gate_idle_polling(self):
        bare_timeout = re.compile(r'(?<![A-Za-z0-9_])timeout\s+"?\$\{?ANDROID_COMMAND_TIMEOUT')

        ac = (ROOT / 'probe/test2/spx-advanced-control-boot').read_text()
        self.assertIsNone(bare_timeout.search(ac))
        self.assertIn('run_with_timeout "${ANDROID_COMMAND_TIMEOUT:-15}" nsenter', ac)
        self.assertIn('display_is_on', ac)

        ota = (ROOT / 'probe/ota/spx-ota-boot').read_text()
        self.assertIsNone(bare_timeout.search(ota))
        self.assertIn('run_with_timeout "${ANDROID_COMMAND_TIMEOUT:-20}" nsenter', ota)
        self.assertIn('display_is_on', ota)
        self.assertIn('CLAIM_CYCLES', ota)

        cal = (ROOT / 'probe/test2/touch-calibration-boot').read_text()
        self.assertIn('timeout() {', cal)
        self.assertIn('display_is_on', cal)
        self.assertIn('device_legacy=', cal)  # Dirty user migration preserved

        # Verify touch-calibration-boot's timeout() override executes cleanly under busybox/sh.
        t_start = cal.index('timeout() {')
        t_end = cal.index('\ndisplay_is_on() {', t_start)
        t_fn = cal[t_start:t_end]
        for shell_cmd in _shells():
            with self.subTest(shell=shell_cmd[0]):
                res = subprocess.run(
                    [*shell_cmd, '-eu', '-c', t_fn + '\nout=$(timeout 5 printf "cal-ok\\n")\n[ "$out" = cal-ok ]\n'],
                    text=True, capture_output=True, check=True, timeout=10,
                )
                self.assertEqual(res.stderr.strip(), '')

        # Behavioral check: spx-ota-boot skips periodic get-current-user & intent claim while display is OFF.
        with tempfile.TemporaryDirectory() as td:
            td_p = Path(td)
            root = td_p / 'android'
            sysfs = td_p / 'sys'
            esp = td_p / 'esp'
            run_dir = td_p / 'run'
            etc_dir = td_p / 'etc'
            for d in (root / 'data/user_de/0/org.surface.advancedcontrol/files', esp, run_dir, etc_dir):
                d.mkdir(parents=True)
            _write(sysfs / 'class/drm/card0-eDP-1/enabled', 'disabled\n')
            _write(sysfs / 'class/drm/card0-eDP-1/dpms', 'Off\n')
            calls_log = td_p / 'ota-calls.log'
            wrapper = _write(td_p / 'ota-android', textwrap.dedent(f"""\
                #!/bin/sh
                printf '%s\\n' "$*" >> "{calls_log}"
                case "$*" in
                    'cmd activity get-current-user') printf '0\\n' ;;
                    'getprop sys.boot_completed') printf '1\\n' ;;
                esac
            """), 0o755)
            fake_tool = _write(td_p / 'ota-tool', '#!/bin/sh\nexit 0\n', 0o755)
            env = os.environ.copy()
            env.update({
                'SPX_OTA_ROOT': str(root),
                'SPX_OTA_SYSFS': str(sysfs),
                'SPX_OTA_ESP': str(esp),
                'SPX_OTA_RUN': str(run_dir),
                'SPX_OTA_ETC': str(etc_dir),
                'SPX_OTA_TOOL': str(fake_tool),
                'SPX_OTA_LOCK_DIR': str(run_dir / 'ota-lock'),
                'SPX_OTA_TMP_REQ': str(run_dir / 'ota-req'),
                'SPX_OTA_ANDROID_WRAPPER': str(wrapper),
                'SPX_OTA_BOOT_WAIT': '0',
                'SPX_OTA_POLL_SLEEP': '0.01',
            })
            p = subprocess.Popen(['sh', str(ROOT / 'probe/ota/spx-ota-boot')], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                # Wait past startup + >40 poll cycles (exceeds CYCLES -ge 30) while display is OFF.
                time.sleep(0.8)
            finally:
                p.terminate()
                p.wait(timeout=5)
            calls = calls_log.read_text().splitlines()
            # Only the single startup get-current-user call; ZERO periodic calls while display is OFF!
            self.assertEqual(calls.count('cmd activity get-current-user'), 1, calls)

    def test_spx_oem_cleanup_boot_removes_dell_packages_and_is_idempotent(self):
        for shell_cmd in _shells():
            with self.subTest(shell=shell_cmd[0]), tempfile.TemporaryDirectory() as td:
                td_p = Path(td)
                root = td_p / 'android'
                sysfs = td_p / 'sys'
                run_dir = td_p / 'run'
                (root / 'data/vendor').mkdir(parents=True)
                run_dir.mkdir(parents=True)
                _write(sysfs / 'firmware/devicetree/base/model', 'Microsoft Surface Pro X (SQ2)\x00')

                installed_file = td_p / 'installed.txt'
                installed_file.write_text(
                    'package:com.dell.supportassist.alos\n'
                    'package:com.dell.diagnostics\n'
                    'package:com.dell.luxsharerma\n'
                    'package:com.android.settings\n'
                )
                calls_log = td_p / 'calls.log'
                wrapper = _write(td_p / 'oem-android', textwrap.dedent(f"""\
                    #!/bin/sh
                    printf '%s\\n' "$*" >> "{calls_log}"
                    case "$*" in
                        'getprop sys.boot_completed') printf '1\\n' ;;
                        'pm list users') printf 'Users:\\n\\tUserInfo{{0:Owner:4c13}} running\\n' ;;
                        'pm list packages -u'|'pm list packages --user 0'|'pm list packages -e --user 0')
                            cat "{installed_file}"
                            ;;
                        'pm uninstall '*)
                            pkg="${{3:-}}"
                            if [ -z "$pkg" ]; then pkg="${{2:-}}"; fi
                            # Extract last arg as package name
                            for tok in "$@"; do last="$tok"; done
                            grep -Fxv "package:$last" "{installed_file}" > "{installed_file}.tmp" || true
                            mv "{installed_file}.tmp" "{installed_file}"
                            printf 'Success\\n'
                            ;;
                        'am force-stop'*) exit 0 ;;
                    esac
                """), 0o755)

                env = os.environ.copy()
                env.update({
                    'SPX_OEM_CLEANUP_ROOT': str(root),
                    'SPX_OEM_CLEANUP_SYSFS': str(sysfs),
                    'SPX_OEM_CLEANUP_LOCK_DIR': str(run_dir / 'oem-lock'),
                    'SPX_OEM_CLEANUP_ANDROID_WRAPPER': str(wrapper),
                    'SPX_OEM_CLEANUP_BOOT_WAIT_MAX': '2',
                    'SPX_OEM_CLEANUP_BOOT_WAIT_SLEEP': '0',
                })
                res = subprocess.run(
                    [*shell_cmd, str(ROOT / 'probe/test2/spx-oem-cleanup-boot')],
                    env=env, text=True, capture_output=True, check=True, timeout=10,
                )
                self.assertIn('SPX_OEM_CLEANUP_COMPLETE', res.stdout)
                self.assertEqual(installed_file.read_text(), 'package:com.android.settings\n')
                self.assertTrue((root / 'data/vendor/spx-oem-cleanup/done-v1').is_file())

                # Second run exits immediately via stamp without invoking pm again.
                calls_log.write_text('')
                res2 = subprocess.run(
                    [*shell_cmd, str(ROOT / 'probe/test2/spx-oem-cleanup-boot')],
                    env=env, text=True, capture_output=True, check=True, timeout=10,
                )
                self.assertNotIn('pm uninstall', calls_log.read_text())
                self.assertEqual(res2.stdout.strip(), '')

                # Dell hardware skips cleanup completely.
                dell_sysfs = td_p / 'dell-sys'
                _write(dell_sysfs / 'class/dmi/id/sys_vendor', 'Dell Inc.\n')
                env_dell = dict(env, SPX_OEM_CLEANUP_SYSFS=str(dell_sysfs))
                calls_log.write_text('')
                res3 = subprocess.run(
                    [*shell_cmd, str(ROOT / 'probe/test2/spx-oem-cleanup-boot')],
                    env=env_dell, text=True, capture_output=True, check=True, timeout=10,
                )
                self.assertIn('SPX_OEM_CLEANUP_SKIP=dell_hardware', res3.stdout)
                self.assertEqual(calls_log.read_text(), '')

    def test_no_workstation_paths_or_device_ids_in_owned_changes(self):
        home_re = re.compile(r'/home/[A-Za-z0-9._-]+/')
        for rel in (
            'probe/test2/spx-service-hygiene-stage',
            'probe/test2/spx-oem-cleanup-boot',
            'probe/test2/prepare-native-gpu',
            'probe/test2/lab-input-posture',
            'probe/test2/touch-resume-watch',
            'probe/test2/spx-lower-usb',
            'probe/test2/spx-lower-usb-watch.c',
            'probe/test2/spx-advanced-control-boot',
            'probe/ota/spx-ota-boot',
            'probe/test2/touch-calibration-boot',
            'tests/test_s2idle_hygiene.py',
        ):
            with self.subTest(file=rel):
                text = (ROOT / rel).read_text(encoding='utf-8')
                self.assertIsNone(home_re.search(text), f'Hardcoded workstation home path in {rel}')


if __name__ == '__main__':
    unittest.main()
