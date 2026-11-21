import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests'))

import full_image_hygiene_runtime
import full_image_radio
import test_s2idle_hygiene


def _write(path: Path, content: str | bytes, mode: int | None = None) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(content, bytes):
        path.write_bytes(content)
    else:
        path.write_text(content, encoding='utf-8')
    if mode is not None:
        path.chmod(mode)
    return path


def _fake_arm64_elf() -> bytes:
    header = bytearray(64)
    header[0:4] = b'\x7fELF'
    header[4] = 2   # ELFCLASS64
    header[5] = 1   # ELFDATA2LSB
    header[6] = 1   # EV_CURRENT
    header[16:18] = (3).to_bytes(2, 'little')    # ET_DYN
    header[18:20] = (183).to_bytes(2, 'little')  # EM_AARCH64
    return bytes(header)


class RuntimeErrorConvergenceTests(unittest.TestCase):
    def test_kernel_patches_and_config_for_cgroup_and_quota2(self):
        p_cg = (ROOT / 'patches/spx-portable-boot/0010-cgroup-v1-allow-cap-sys-nice-attach.patch').read_text(encoding='utf-8')
        self.assertIn('kernel/cgroup/cgroup-v1.c', p_cg)
        self.assertIn('!file_ns_capable(of->file, tcred->user_ns, CAP_SYS_NICE)', p_cg)

        p_q2 = (ROOT / 'patches/spx-portable-boot/0011-netfilter-xt-quota2.patch').read_text(encoding='utf-8')
        self.assertIn('include/uapi/linux/netfilter/xt_quota2.h', p_q2)
        self.assertIn('net/netfilter/xt_quota2.c', p_q2)
        self.assertIn('.revision\t= 3,', p_q2)
        self.assertIn('NFPROTO_IPV4', p_q2)
        self.assertIn('NFPROTO_IPV6', p_q2)
        self.assertIn('struct proc_ops q2_counter_fops', p_q2)
        self.assertIn('pde_data(file_inode(file))', p_q2)

        kcfg = (ROOT / 'configs/kernel-6.18-hybrid.config').read_text(encoding='utf-8')
        self.assertIn('CONFIG_NETFILTER_XT_MATCH_QUOTA2=y', kcfg)
        self.assertIn('CONFIG_NETFILTER_XT_MATCH_QUOTA2_LOG=y', kcfg)

    def test_radio_overlay_disables_missing_satellite_service(self):
        xml = (ROOT / 'probe/lte/overlay/res/values/config.xml').read_text(encoding='utf-8')
        self.assertIn('<string name="config_satellite_service_package" translatable="false"></string>', xml)
        self.assertIn('<string name="config_satellite_gateway_service_package" translatable="false"></string>', xml)
        self.assertIn('<bool name="config_oem_enabled_satellite_access_allow">false</bool>', xml)

    def test_radio_modem_implements_valid_modem_activity_info(self):
        src = (ROOT / 'probe/lte/sq2-radio.cpp').read_text(encoding='utf-8')
        self.assertIn('Status getModemActivityInfo(int32_t s) override', src)
        self.assertIn('CLOCK_BOOTTIME', src)
        self.assertIn('lte.txmModetimeMs = {0, 0, 0, 0, 0};', src)
        self.assertIn('r->getModemActivityInfoResponse(info(s), stats);', src)

    def test_systemui_overlay_disables_pixel_google_battery_probes(self):
        manifest = (ROOT / 'probe/power/systemui-overlay/AndroidManifest.xml').read_text(encoding='utf-8')
        self.assertIn('android:targetPackage="com.android.systemui"', manifest)
        self.assertIn('android:isStatic="true"', manifest)
        xml = (ROOT / 'probe/power/systemui-overlay/res/values/config.xml').read_text(encoding='utf-8')
        for name in (
            'config_battery_defender_warning_enabled',
            'config_adaptive_charging_warning_enabled',
            'config_incompatible_charging_warning_enabled',
            'config_insufficient_charging_warning_enabled',
        ):
            self.assertIn(f'<bool name="{name}">false</bool>', xml)

    def test_ec_executor_stub_contract(self):
        src = (ROOT / 'probe/power/spx-ec-executor-stub.cpp').read_text(encoding='utf-8')
        self.assertIn('"vendor.google.desktop.IEcExecutor"', src)
        self.assertIn('"vendor.google.desktop.IEcExecutor/default"', src)
        self.assertIn('2dfc14cf19e97a887f17030ea7a15ed2944e9b48', src)
        self.assertIn('kTxFetchSmartBatteryInfo = FIRST_CALL_TRANSACTION + 19;', src)
        self.assertIn('writeDefaultSmartBatteryInfo(out)', src)
        self.assertIn('AIBinder_markVintfStability(binder);', src)
        self.assertIn('EX_UNSUPPORTED_OPERATION', src)

    def test_hygiene_stage_installs_ec_executor_stub_when_available_and_is_idempotent(self):
        for shell_cmd in test_s2idle_hygiene._shells():
            with self.subTest(shell=shell_cmd[0]), tempfile.TemporaryDirectory() as td:
                td_p = Path(td)
                root = td_p / 'newroot'
                dev = td_p / 'dev'
                sysfs = td_p / 'sys'
                dev.mkdir()
                test_s2idle_hygiene._make_fake_root_fixture(root, with_testscripts=False, matching_krel=False)
                _write(root / 'vendor/bin/hw/vendor.google.desktop.ec-executor', b'donor-ec-executor', 0o755)
                stub_src = _write(td_p / 'spx-ec-executor-stub', b'stub-ec-executor-bin', 0o755)

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
                    'SPX_HYGIENE_KREL': '6.18.60-sq2-android1',
                    'SPX_HYGIENE_EC_STUB': str(stub_src),
                })
                res = subprocess.run(shell_cmd, input=fake_mount + script, env=env, text=True, capture_output=True, check=True)
                self.assertIn('SPX_HYGIENE_STUB_SVC=vendor/etc/init/ec-executor.rc reason=no_cros_ec', res.stdout)
                self.assertEqual((root / 'vendor/bin/hw/vendor.google.desktop.ec-executor').read_bytes(), b'stub-ec-executor-bin')
                ec_rc = (root / 'vendor/etc/init/ec-executor.rc').read_text(encoding='utf-8')
                self.assertIn('restorecon /vendor/bin/hw/vendor.google.desktop.ec-executor', ec_rc)
                self.assertIn('group system', ec_rc)
                self.assertNotIn('disabled', ec_rc)
                ec_xml = (root / 'vendor/etc/vintf/manifest/vendor.google.desktop.ec-executor.xml').read_text(encoding='utf-8')
                self.assertIn('IEcExecutor/default', ec_xml)

                # Idempotency check
                before = (root / 'vendor/etc/init/ec-executor.rc').read_bytes()
                subprocess.run(shell_cmd, input=fake_mount + script, env=env, text=True, capture_output=True, check=True)
                self.assertEqual((root / 'vendor/etc/init/ec-executor.rc').read_bytes(), before)

    def test_full_image_hygiene_and_radio_installers(self):
        records = {
            'etc/sq2-qti-system/INSTALL_FILES': [0, [0, 0o100644]],
            'etc/sq2-radio/sq2-radio': [0, [0, 0o100600]],
            'etc/sq2-radio/sq2-radio-overlay.apk': [0, [0, 0o100600]],
        }
        bodies = {
            'etc/sq2-qti-system/INSTALL_FILES': b'app/SurfaceAdvancedControl/SurfaceAdvancedControl.apk\n',
            'etc/sq2-radio/sq2-radio': b'old-radio',
            'etc/sq2-radio/sq2-radio-overlay.apk': b'old-overlay',
        }
        new_rec = lambda mode: [0, [0, mode]]
        elf = _fake_arm64_elf()

        res_h = full_image_hygiene_runtime.install(
            records, bodies, new_rec, sysui_overlay=b'PK-sysui-rro', ec_stub=elf,
        )
        self.assertEqual(res_h['sysui_overlay'], 'etc/sq2-qti-system/root/overlay/spx-systemui-overlay.apk')
        self.assertEqual(res_h['ec_executor_stub'], 'etc/spx-hygiene/spx-ec-executor-stub')
        self.assertIn(b'overlay/spx-systemui-overlay.apk', bodies['etc/sq2-qti-system/INSTALL_FILES'].splitlines())
        self.assertEqual(bodies['etc/spx-hygiene/spx-ec-executor-stub'], elf)

        res_r = full_image_radio.install(
            records, bodies, new_rec, radio_bin=elf, radio_overlay=b'PK-radio-rro',
        )
        self.assertEqual(res_r['radio_bin'], 'etc/sq2-radio/sq2-radio')
        self.assertEqual(res_r['radio_overlay'], 'etc/sq2-radio/sq2-radio-overlay.apk')
        self.assertEqual(bodies['etc/sq2-radio/sq2-radio'], elf)
        self.assertEqual(bodies['etc/sq2-radio/sq2-radio-overlay.apk'], b'PK-radio-rro')

    def test_no_workstation_home_paths(self):
        home_re = re.compile(r'/home/[A-Za-z0-9._-]+/')
        for rel in (
            'patches/spx-portable-boot/0010-cgroup-v1-allow-cap-sys-nice-attach.patch',
            'patches/spx-portable-boot/0011-netfilter-xt-quota2.patch',
            'probe/lte/overlay/res/values/config.xml',
            'probe/lte/sq2-radio.cpp',
            'probe/power/spx-ec-executor-stub.cpp',
            'probe/power/systemui-overlay/AndroidManifest.xml',
            'probe/power/systemui-overlay/res/values/config.xml',
            'probe/test2/spx-service-hygiene-stage',
            'scripts/full_image_hygiene_runtime.py',
            'scripts/full_image_radio.py',
            'tests/test_runtime_error_convergence.py',
        ):
            with self.subTest(file=rel):
                self.assertIsNone(home_re.search((ROOT / rel).read_text(encoding='utf-8')))


if __name__ == '__main__':
    unittest.main()
