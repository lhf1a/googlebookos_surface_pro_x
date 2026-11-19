"""Host-only tests: policy/i18n/sorting, router routing, bridge daemon, and packaging."""
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import full_image_advanced_control as control


class AdvancedControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.work = Path(cls.tmp.name)
        source = ROOT / 'probe/graphics/sq2-hybrid-vulkan-router.c'
        runner = cls.work / 'runner.c'
        runner.write_text('#include "' + str(source) + '"\n' + r'''
uid_t getuid(void) { return (uid_t)atoi(getenv("APP_UID")); }
int __system_property_get(const char *name, char *value) {
    const char *v = "";
    if (getenv("APP_KEY") && !strcmp(name, getenv("APP_KEY"))) v = getenv("APP_VALUE");
    if (!strcmp(name, "debug.sq2.vulkan.turnip_apps") && getenv("DEBUG_APPS")) v = getenv("DEBUG_APPS");
    if (!strcmp(name, "persist.sq2.vulkan.driver") && getenv("DEFAULT_DRIVER")) v = getenv("DEFAULT_DRIVER");
    strcpy(value, v); return (int)strlen(v);
}
int main(void) {
    struct sq2_vk_decision out;
    char proc[256];
    int rc = evaluate_current_process(&out, proc, sizeof(proc));
    printf("%s %s %s\n", backend_name(out.backend), out.reason, out.package_name);
    return rc;
}
''')
        cls.runner = cls.work / 'runner'
        subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-rdynamic',
                        '-pthread', str(runner), '-ldl', '-o', str(cls.runner)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def route(self, uid=1010123, pkg='com.example.game:renderer', value='qcom',
              key='persist.sq2.vulkan.app.10.com.example.game', debug='', default_driver=''):
        env = dict(os.environ, APP_UID=str(uid), APP_KEY=key, APP_VALUE=value,
                   DEBUG_APPS=debug, DEFAULT_DRIVER=default_driver)
        env.pop('SQ2_VULKAN_DRIVER', None)
        env['SQ2_TEST_VK_POLICY_PATH'] = str(self.work / 'absent-policy')
        result = subprocess.run([pkg], executable=str(self.runner), env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_runtime_exact_package_and_child(self):
        for pkg in ('com.example.game', 'com.example.game:renderer'):
            self.assertEqual(self.route(pkg=pkg), 'qcom prop:app-driver com.example.game')
        self.assertTrue(self.route(pkg='com.example.game2').startswith('turnip default:turnip'))

    def test_runtime_user_isolation(self):
        self.assertTrue(self.route(uid=10123).startswith('turnip default:turnip'))
        self.assertTrue(self.route(uid=10123, key='persist.sq2.vulkan.app.0.com.example.game').startswith('qcom prop:app-driver'))

    def test_no_truncated_or_path_package_alias(self):
        self.assertNotIn('prop:app-driver', self.route(pkg='/tmp/com.example.game'))
        pkg = 'com.' + 'a' * 188
        self.assertNotIn('prop:app-driver', self.route(pkg=pkg, key='persist.sq2.vulkan.app.10.' + pkg[:191]))

    def test_restore_default_and_invalid_value(self):
        for value in ('', 'invalid'):
            self.assertTrue(self.route(value=value).startswith('turnip default:turnip'))
        self.assertTrue(self.route(value='turnip').startswith('turnip prop:app-driver'))

    def test_system_isolated_and_sandbox_not_selected(self):
        for uid in (0, 1000, 101000, 1099000, 1020123):
            self.assertNotIn('prop:app-driver', self.route(uid=uid))

    def test_debug_override_preserved(self):
        self.assertTrue(self.route(debug='com.example.game').startswith('turnip prop:debug.turnip_apps'))

    def test_global_default_driver_and_systemui_guard(self):
        self.assertEqual(
            self.route(uid=10123, pkg='com.example.other', value='', default_driver='qcom'),
            'qcom prop:driver com.example.other',
        )
        self.assertEqual(
            self.route(uid=10123, pkg='com.example.other', value='', default_driver='turnip'),
            'turnip prop:driver com.example.other',
        )
        # Per-app override beats global default.
        self.assertEqual(
            self.route(uid=1010123, pkg='com.example.game', value='turnip', default_driver='qcom'),
            'turnip prop:app-driver com.example.game',
        )
        # SystemUI remains guarded on Turnip when default is Qcom.
        self.assertEqual(
            self.route(uid=10120, pkg='com.android.systemui', value='', default_driver='qcom'),
            'turnip guard:system-on-global-qcom com.android.systemui',
        )

    def test_java_policy_sorting_and_i18n(self):
        runner = self.work / 'PolicyAndStringsTest.java'
        runner.write_text(r'''
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import org.surface.advancedcontrol.DriverPolicy;
import org.surface.advancedcontrol.Strings;

public class PolicyAndStringsTest {
  static void check(boolean b, String msg) {
    if (!b) throw new AssertionError(msg);
  }
  public static void main(String[] args) {
    check(DriverPolicy.propertyKey(1010123, "com.example.game")
        .equals("persist.sq2.vulkan.app.10.com.example.game"), "key");
    check(!DriverPolicy.propertyKey(10123, "com.example.game")
        .equals(DriverPolicy.propertyKey(1010123, "com.example.game")), "user isolation");

    for (String v : new String[]{"", "turnip", "qcom"}) {
      String save = DriverPolicy.saveCommand(1010123, "com.example.game", v, false);
      check(save.contains("setprop 'persist.sq2.vulkan.app.10.com.example.game' '" + v + "'"), "saveCmd");
      check(save.contains("getprop") && !save.contains("force-stop"), "noStop");
      check(DriverPolicy.saveCommand(1010123, "com.example.game", v, true)
          .endsWith("am force-stop --user 10 'com.example.game'"), "stopCmd");

      String req = DriverPolicy.appVulkanRequest("abc-123", 1010123, "com.example.game", v, true);
      check(req.equals("set-app-vulkan\nabc-123\n10\ncom.example.game\n" + v + "\n1\n"), "appReq");
      String defReq = DriverPolicy.defaultVulkanRequest("abc-123", v);
      check(defReq.equals("set-default-vulkan\nabc-123\n" + v + "\n"), "defReq");
    }

    check(DriverPolicy.cpuGovernorRequest("abc-123", "performance")
        .equals("set-cpu-governor\nabc-123\nperformance\n"), "govReq");
    check(DriverPolicy.cpuGovernorRequest("abc-123", "")
        .equals("set-cpu-governor\nabc-123\n\n"), "govResetReq");

    // Customized apps MUST always sort before unmodified apps.
    check(DriverPolicy.isCustomized("turnip") && DriverPolicy.isCustomized("qcom"), "customized");
    check(!DriverPolicy.isCustomized("") && !DriverPolicy.isCustomized(null), "not customized");
    check(DriverPolicy.compareAppRows(true, "Zeta", "com.z", false, "Alpha", "com.a") < 0, "custom first");
    check(DriverPolicy.compareAppRows(false, "Alpha", "com.a", true, "Zeta", "com.z") > 0, "default second");
    check(DriverPolicy.compareAppRows(true, "Alpha", "com.a", true, "Beta", "com.b") < 0, "alpha within custom");

    // Governor parsing deduplicates and rejects invalid tokens.
    List<String> govs = DriverPolicy.parseAvailableGovernors("performance schedutil performance bad;cmd powersave");
    check(govs.equals(Arrays.asList("performance", "schedutil", "powersave")), "gov parse: " + govs);

    // Injection rejection
    for (String pkg : new String[]{"com.example;id", "com.*", "com.example:renderer", "com..bad", "com.1bad", "com.bad\n", "android"}) {
      try { DriverPolicy.propertyKey(10123, pkg); throw new AssertionError(pkg); }
      catch (IllegalArgumentException expected) {}
    }
    for (int uid : new int[]{-1, 0, 1000, 99000, 20000}) {
      try { DriverPolicy.propertyKey(uid, "com.example.game"); throw new AssertionError(); }
      catch (IllegalArgumentException expected) {}
    }
    for (String badGov : new String[]{"perf;id", "sched\nutil", "../etc"}) {
      try { DriverPolicy.normalizeGovernor(badGov); throw new AssertionError(badGov); }
      catch (IllegalArgumentException expected) {}
    }
    for (String badNonce : new String[]{"", "BAD", "a\nb", "a;id"}) {
      try { DriverPolicy.normalizeNonce(badNonce); throw new AssertionError(badNonce); }
      catch (IllegalArgumentException expected) {}
    }

    // Localization: Chinese for zh*, English default for all non-Chinese locales.
    check(Strings.isChinese(Locale.SIMPLIFIED_CHINESE), "zh_CN");
    check(Strings.isChinese(Locale.TRADITIONAL_CHINESE), "zh_TW");
    check(!Strings.isChinese(Locale.US), "en_US");
    check(!Strings.isChinese(Locale.JAPAN), "ja_JP");
    check(!Strings.isChinese(Locale.GERMAN), "de_DE");
    check(!Strings.isChinese(null), "null locale");

    Strings zh = new Strings(Locale.SIMPLIFIED_CHINESE);
    Strings en = new Strings(Locale.US);
    Strings jpFallback = new Strings(Locale.JAPAN);
    check("高级设置".equals(zh.appTitle()), "zh title");
    check("Vulkan 配置".equals(zh.vulkanTitle()), "zh vulkan");
    check("性能模式".equals(zh.perfTitle()), "zh perf");
    check(zh.governorShortName("performance").contains("性能模式"), "zh perf gov");

    check("Advanced Settings".equals(en.appTitle()), "en title");
    check("Vulkan Configuration".equals(en.vulkanTitle()), "en vulkan");
    check("Performance Mode".equals(en.perfTitle()), "en perf");
    check(en.governorShortName("performance").contains("Performance"), "en perf gov");

    // Non-Chinese locale defaults identically to English.
    check(en.appTitle().equals(jpFallback.appTitle()), "jp->en title");
    check(en.vulkanTitle().equals(jpFallback.vulkanTitle()), "jp->en vulkan");
    check(en.perfTitle().equals(jpFallback.perfTitle()), "jp->en perf");
    check(en.touchTitle().equals(jpFallback.touchTitle()), "jp->en touch");

    // Touch request formatting, validation & summaries
    check(DriverPolicy.touchGhostPatchRequest("abc-123", true)
        .equals("set-touch-ghost-patch\nabc-123\n1\n"), "ghostOnReq");
    check(DriverPolicy.touchGhostPatchRequest("abc-123", false)
        .equals("set-touch-ghost-patch\nabc-123\n0\n"), "ghostOffReq");
    check(DriverPolicy.clearTouchCalibrationRequest("abc-123")
        .equals("clear-touch-calibration\nabc-123\n"), "clearReq");
    check(DriverPolicy.exportTouchCalibrationRequest("abc-123")
        .equals("export-touch-calibration\nabc-123\n"), "exportReq");
    check("surface-touch-01234567.spkcal"
        .equals(DriverPolicy.defaultTouchExportFilename("0123456789abcdef")), "exportName");
    check("surface-touch.spkcal".equals(DriverPolicy.defaultTouchExportFilename("")), "exportFallbackName");

    check("导出校准数据".equals(zh.touchExportTitle()), "zh export");
    check("Export Calibration".equals(en.touchExportTitle()), "en export");

    String sampleProfile = "Version=1\nDevice=deadbeef\nCalibrationX=0.0000:0.0000 0.5000:0.5123 1.0000:1.0000\n";
    String sampleBundle = "SPXTOUCH1\nDevice=deadbeef\nNoiseDisabled=0\n[Profile]\n" + sampleProfile + "[/Profile]\n";
    String sampleNoise = "SPXNOISE3 deadbeef 1 1 56 84 0000\n";
    String sampleTap = "SPXTAP1 deadbeef 1 1 56 84 2\n";
    check("bundle".equals(DriverPolicy.detectTouchImportKind(sampleBundle)), "detect bundle");
    check("profile".equals(DriverPolicy.detectTouchImportKind(sampleProfile)), "detect profile");
    check("noise".equals(DriverPolicy.detectTouchImportKind("SPXNOISE1 deadbeef 1 1 56 84 4\n10 11 12 13\n")), "detect noise1");
    check("noise".equals(DriverPolicy.detectTouchImportKind(sampleNoise)), "detect noise3");
    check("tap".equals(DriverPolicy.detectTouchImportKind(sampleTap)), "detect tap");
    check(DriverPolicy.importTouchCalibrationRequest("abc-123", sampleProfile.trim())
        .startsWith("import-touch-calibration\nabc-123\nVersion=1\n"), "importReq");

    for (String badImport : new String[]{"", "hello world\n", "SPXNOISE1\0bad"}) {
      try { DriverPolicy.detectTouchImportKind(badImport); throw new AssertionError(badImport); }
      catch (IllegalArgumentException expected) {}
    }

    DriverPolicy.CurveSummary flat = DriverPolicy.summarizeCurve("0.0000:0.0000 1.0000:1.0000");
    check(flat.points == 2 && flat.identity, "flat curve");
    DriverPolicy.CurveSummary curved = DriverPolicy.summarizeCurve("0.0000:0.0000 0.5000:0.5250 1.0000:1.0000");
    check(curved.points == 3 && !curved.identity && curved.maxDeltaPercent > 2.4, "curved");

    DriverPolicy.NoiseSummary n3 = DriverPolicy.parseNoiseSummary("SPXNOISE3 deadbeef 1 1 56 84 nonzero=4/4704 max=255");
    check(n3 != null && "SPXNOISE3".equals(n3.format) && n3.rows == 56 && n3.cols == 84 && n3.count == 4 && n3.maxLevel == 255, "n3 summary");
    DriverPolicy.TapSummary ts = DriverPolicy.parseTapSummary("SPXTAP1 deadbeef 1 1 56 84 3");
    check(ts != null && ts.rows == 56 && ts.cols == 84 && ts.frames == 3, "tap summary");

    check("触摸设置".equals(zh.touchTitle()), "zh touch");
    check("Touch".equals(en.touchTitle()), "en touch");
    check(zh.touchGhostTitle().contains("幽灵触控"), "zh ghost");
    check(en.touchGhostTitle().contains("Ghost-Touch"), "en ghost");
    check(en.touchValueCoords(true, "0.0:0.0 0.5:0.52 1.0:1.0", "0.0:0.0 1.0:1.0", "", "", "").contains("X 3 pts"), "en coords");

    System.out.println("DRIVER_POLICY_AND_I18N_OK");
  }
}
''')
        subprocess.run(['javac', '--release', '8', '-encoding', 'UTF-8', '-d', str(self.work),
                        str(ROOT / 'probe/advanced-control/DriverPolicy.java'),
                        str(ROOT / 'probe/advanced-control/Strings.java'),
                        str(runner)], check=True)
        result = subprocess.run(['java', '-cp', str(self.work), 'PolicyAndStringsTest'], capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stdout), (0, 'DRIVER_POLICY_AND_I18N_OK\n'), result.stderr)

    def test_bridge_daemon_vulkan_and_performance_flow(self):
        bridge = ROOT / 'probe/test2/spx-advanced-control-boot'
        with tempfile.TemporaryDirectory(prefix='spx-bridge-test-') as td:
            root = Path(td)
            data_root = root / 'root'
            sysfs = root / 'sys'
            cmdline = root / 'cmdline'
            cmdline.write_text('console=tty0 androidboot.hardware=android-desktop\n')

            # Stage vendor markers & policy conf
            for rel, body in (
                ('vendor/lib64/hw/vulkan.turnip.so', b'elf'),
                ('vendor/lib64/hw/vulkan.qti.so', b'elf'),
                ('vendor/etc/surface-vulkan-app-routing.version', b'1\n'),
                ('vendor/etc/sq2-vulkan-policy.conf', b'default = turnip\n'),
            ):
                p = data_root / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(body)

            # Stage dynamic cpufreq policies (policy0 & policy4)
            for pol, avail, cur in (
                ('policy0', 'conservative ondemand userspace powersave performance schedutil\n', 'schedutil\n'),
                ('policy4', 'ondemand userspace powersave performance schedutil\n', 'schedutil\n'),
            ):
                pdir = sysfs / 'devices/system/cpu/cpufreq' / pol
                pdir.mkdir(parents=True, exist_ok=True)
                (pdir / 'scaling_available_governors').write_text(avail)
                (pdir / 'scaling_governor').write_text(cur)

            app_files = data_root / 'data/user_de/0/org.surface.advancedcontrol/files'
            app_files.mkdir(parents=True, exist_ok=True)

            # Mock android wrapper (stores props & logs force-stop)
            props_file = root / 'props.json'
            props_file.write_text('{}\n')
            stops_file = root / 'stops.log'
            wrapper = root / 'mock-android'
            wrapper.write_text(f'''#!/usr/bin/env python3
import json, pathlib, sys
props_p = pathlib.Path({str(props_file)!r})
stops_p = pathlib.Path({str(stops_file)!r})
args = sys.argv[1:]
if args == ['cmd', 'activity', 'get-current-user']:
    print('0')
elif args[:1] == ['getprop'] and len(args) == 2:
    data = json.loads(props_p.read_text())
    print(data.get(args[1], ''))
elif args[:1] == ['setprop'] and len(args) == 3:
    data = json.loads(props_p.read_text())
    data[args[1]] = args[2]
    props_p.write_text(json.dumps(data))
elif args[:2] == ['am', 'force-stop']:
    with stops_p.open('a') as f:
        f.write(' '.join(args) + '\\n')
else:
    sys.exit(2)
''')
            wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)

            def run_once(req_body=None):
                if req_body is not None:
                    (app_files / 'request').write_text(req_body)
                env = dict(
                    os.environ,
                    SPX_AC_ROOT=str(data_root),
                    SPX_AC_SYSFS=str(sysfs),
                    SPX_AC_CMDLINE=str(cmdline),
                    SPX_AC_ANDROID_WRAPPER=str(wrapper),
                    SPX_AC_LOCK_DIR=str(root / 'lock'),
                    SPX_AC_TMP_REQ=str(root / 'tmp.req'),
                    SPX_AC_BOOT_WAIT='0',
                    SPX_AC_ONCE='1',
                )
                res = subprocess.run(['sh', str(bridge)], env=env, text=True, capture_output=True)
                self.assertEqual(res.returncode, 0, res.stderr)
                state = {}
                for line in (app_files / 'state').read_text().splitlines():
                    if '=' in line:
                        k, v = line.split('=', 1)
                        state[k] = v
                reply = (app_files / 'reply').read_text().splitlines() if (app_files / 'reply').is_file() else []
                return state, reply, res.stdout

            # 1. Initial state publish: common governors intersection excludes conservative (only on policy0)
            state, _, _ = run_once()
            self.assertEqual(state['bridge_version'], '1')
            self.assertEqual(state['vulkan_available'], '1')
            self.assertEqual(state['cpu_available_governors'], 'ondemand userspace powersave performance schedutil')
            self.assertEqual(state['cpu_current_governor'], 'schedutil')

            # 2. Set default Vulkan to qcom, then turnip, then auto ("")
            state, reply, _ = run_once('set-default-vulkan\nnonce-1\nqcom\n')
            self.assertEqual(reply[:2], ['nonce-1', 'OK'])
            self.assertEqual(state['vulkan_default'], 'qcom')
            self.assertEqual(json.loads(props_file.read_text())['persist.sq2.vulkan.driver'], 'qcom')

            state, reply, _ = run_once('set-default-vulkan\nnonce-2\nturnip\n')
            self.assertEqual(reply[:2], ['nonce-2', 'OK'])
            self.assertEqual(state['vulkan_default'], 'turnip')

            # 3. Set per-app Vulkan with force-stop + reject cross-user
            state, reply, _ = run_once('set-app-vulkan\nnonce-3\n0\ncom.example.game\nqcom\n1\n')
            self.assertEqual(reply[:2], ['nonce-3', 'OK'])
            self.assertEqual(json.loads(props_file.read_text())['persist.sq2.vulkan.app.0.com.example.game'], 'qcom')
            self.assertIn('force-stop --user 0 com.example.game', stops_file.read_text())

            _, reply, _ = run_once('set-app-vulkan\nnonce-4\n10\ncom.example.game\nqcom\n0\n')
            self.assertEqual(reply[:2], ['nonce-4', 'ERROR'])

            # 4. Set CPU governor to performance -> writes both policies & persists
            state, reply, _ = run_once('set-cpu-governor\nnonce-5\nperformance\n')
            self.assertEqual(reply[:2], ['nonce-5', 'OK'])
            self.assertEqual(state['cpu_current_governor'], 'performance')
            self.assertEqual(state['cpu_saved_governor'], 'performance')
            self.assertEqual((sysfs / 'devices/system/cpu/cpufreq/policy0/scaling_governor').read_text().strip(), 'performance')
            self.assertEqual((sysfs / 'devices/system/cpu/cpufreq/policy4/scaling_governor').read_text().strip(), 'performance')

            # Reject unsupported governor
            _, reply, _ = run_once('set-cpu-governor\nnonce-6\nturbo_fake\n')
            self.assertEqual(reply[:2], ['nonce-6', 'ERROR'])

            # 5. Simulate reboot: reset sysfs to schedutil -> boot restores persisted performance!
            for pol in ('policy0', 'policy4'):
                (sysfs / 'devices/system/cpu/cpufreq' / pol / 'scaling_governor').write_text('schedutil\n')
            state, _, stdout = run_once()
            self.assertIn('SPX_AC_BOOT_CPU_RESTORED performance', stdout)
            self.assertEqual(state['cpu_current_governor'], 'performance')

            # 6. Simulate rescue boot: must NOT restore performance
            for pol in ('policy0', 'policy4'):
                (sysfs / 'devices/system/cpu/cpufreq' / pol / 'scaling_governor').write_text('schedutil\n')
            cmdline.write_text('console=tty0 sq2.rescue=1\n')
            state, _, stdout = run_once()
            self.assertIn('SPX_AC_BOOT_CPU_RESTORE_SKIPPED', stdout)
            self.assertEqual(state['cpu_current_governor'], 'schedutil')

            # 7. Clear persisted governor back to system default
            cmdline.write_text('console=tty0\n')
            state, reply, _ = run_once('set-cpu-governor\nnonce-7\n\n')
            self.assertEqual(reply[:2], ['nonce-7', 'OK'])
            self.assertEqual(state['cpu_saved_governor'], '')
            self.assertEqual(state['cpu_current_governor'], 'schedutil')

    def test_bridge_daemon_touch_flow(self):
        import hashlib
        bridge = ROOT / 'probe/test2/spx-advanced-control-boot'
        with tempfile.TemporaryDirectory(prefix='spx-touch-bridge-test-') as td:
            root = Path(td)
            data_root = root / 'root'
            sysfs = root / 'sys'
            run_dir = root / 'run'
            etc_dir = root / 'etc'
            cmdline = root / 'cmdline'
            cmdline.write_text('console=tty0 androidboot.hardware=android-desktop\n')

            app_files = data_root / 'data/user_de/0/org.surface.advancedcontrol/files'
            app_files.mkdir(parents=True, exist_ok=True)
            tcb_files = data_root / 'data/user_de/0/org.surface.touchcalibration.unified/files'
            tcb_files.mkdir(parents=True, exist_ok=True)

            # Stage sysfs/run touch identity inputs matching resolve_touch_device().
            run_dir.mkdir(parents=True, exist_ok=True)
            etc_dir.mkdir(parents=True, exist_ok=True)
            (etc_dir / 'iptsd.conf').write_text('[Touchscreen]\nDisable=false\n')
            (run_dir / 'iptsd-hidraw').write_text('/dev/hidraw3\n')
            (run_dir / 'iptsd-hid-id').write_text('045E:0C1A\n')
            ndir = run_dir / 'iptsd-noise'
            ndir.mkdir(parents=True, exist_ok=True)
            (ndir / 'version').write_text('3\n')

            hid_dev = sysfs / 'class/hidraw/hidraw3/device'
            hid_dev.mkdir(parents=True, exist_ok=True)
            uevent_bytes = b'HID_NAME=Surface Touch\nHID_ID=0018:0000045E:00000C1A\n'
            rdesc_bytes = b'\x05\x0d\x09\x04\xa1\x01\xc0'
            (hid_dev / 'uevent').write_bytes(uevent_bytes)
            (hid_dev / 'report_descriptor').write_bytes(rdesc_bytes)

            edp = sysfs / 'class/drm/card0-eDP-1'
            edp.mkdir(parents=True, exist_ok=True)
            edid_bytes = b'EDID' * 32
            (edp / 'status').write_text('connected\n')
            (edp / 'edid').write_bytes(edid_bytes)

            dmi = sysfs / 'class/dmi/id'
            dmi.mkdir(parents=True, exist_ok=True)
            uuid_bytes = b'12345678-1234-1234-1234-1234567890ab\n'
            (dmi / 'product_uuid').write_bytes(uuid_bytes)

            dev_id = hashlib.sha256(
                uuid_bytes + edid_bytes + b'HID_ID=0018:0000045E:00000C1A\n' + rdesc_bytes
            ).hexdigest()

            touch_store = data_root / 'data/vendor/sq2-touch-calibration'
            touch_store.mkdir(parents=True, exist_ok=True)
            prof_text = (
                f'Version=1\nDevice={dev_id}\n'
                'CalibrationX=0.0000:0.0000 0.5000:0.5150 1.0000:1.0000\n'
                'CalibrationY=0.0000:0.0000 1.0000:1.0000\n'
                'CalibrationXTop=\nCalibrationTopStart=0.1\nCalibrationTopEnd=0.3\n'
                'ContactSizeMax=2.4\n'
            )
            noise1_text = f'SPXNOISE1 {dev_id} 1 1 56 84 4\n10 11 12 13\n'
            noise3_text = f'SPXNOISE3 {dev_id} 1 1 56 84 00ff0a01\n'
            tap_text = f'SPXTAP1 {dev_id} 1 1 56 84 2\n'
            (touch_store / f'{dev_id}.profile').write_text(prof_text)
            (touch_store / f'{dev_id}.noise').write_text(noise3_text)
            (touch_store / f'{dev_id}.tap').write_text(tap_text)
            (ndir / 'active').write_text(noise3_text)
            (ndir / 'tap-active').write_text(tap_text)

            calls_log = root / 'tool_calls.log'
            wrapper = root / 'mock-android'
            wrapper.write_text('#!/usr/bin/env python3\nimport sys\nif sys.argv[1:4]==["cmd","activity","get-current-user"]:\n print("0")\nelif sys.argv[1:2]==["getprop"]:\n print("")\n')
            wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)

            calib_tool = root / 'mock-calib'
            calib_tool.write_text(f'''#!/usr/bin/env python3
import pathlib, sys
log = pathlib.Path({str(calls_log)!r})
body = sys.stdin.read()
with log.open('a') as f: f.write('calib:' + ' '.join(sys.argv[1:]) + '\\n')
if sys.argv[1] == 'validate':
    if 'Version=1' not in body or sys.argv[2] not in body:
        sys.exit(2)
    print('[Touchscreen]\\nCalibrated=true')
elif sys.argv[1] == 'publish':
    p = pathlib.Path(sys.argv[2]) / sys.argv[3]
    p.write_text(body)
''')
            calib_tool.chmod(calib_tool.stat().st_mode | stat.S_IXUSR)

            noise_tool = root / 'mock-noise'
            noise_tool.write_text(f'''#!/usr/bin/env python3
import pathlib, sys
log = pathlib.Path({str(calls_log)!r})
body = sys.stdin.read()
with log.open('a') as f: f.write('noise:' + ' '.join(sys.argv[1:]) + '\\n')
first = next((l for l in body.splitlines() if l.strip()), '')
if not first.startswith(('SPXNOISE1 ', 'SPXNOISE2 ', 'SPXNOISE3 ')) or sys.argv[1] not in first:
    sys.exit(2)
sys.stdout.write(body)
''')
            noise_tool.chmod(noise_tool.stat().st_mode | stat.S_IXUSR)

            tap_tool = root / 'mock-tap'
            tap_tool.write_text(f'''#!/usr/bin/env python3
import pathlib, sys
log = pathlib.Path({str(calls_log)!r})
body = sys.stdin.read()
with log.open('a') as f: f.write('tap:' + ' '.join(sys.argv[1:]) + '\\n')
first = next((l for l in body.splitlines() if l.strip()), '')
if not first.startswith('SPXTAP1 ') or sys.argv[1] not in first:
    sys.exit(2)
sys.stdout.write(body)
''')
            tap_tool.chmod(tap_tool.stat().st_mode | stat.S_IXUSR)

            ready_tool = root / 'mock-ready'
            ready_tool.write_text(f'''#!/usr/bin/env python3
import pathlib, sys
with pathlib.Path({str(calls_log)!r}).open('a') as f:
    f.write('ready\\n')
''')
            ready_tool.chmod(ready_tool.stat().st_mode | stat.S_IXUSR)

            def run_once(req_body=None):
                if req_body is not None:
                    (app_files / 'request').write_text(req_body)
                env = dict(
                    os.environ,
                    SPX_AC_ROOT=str(data_root),
                    SPX_AC_SYSFS=str(sysfs),
                    SPX_AC_RUN=str(run_dir),
                    SPX_AC_ETC=str(etc_dir),
                    SPX_AC_CMDLINE=str(cmdline),
                    SPX_AC_ANDROID_WRAPPER=str(wrapper),
                    SPX_AC_CALIB_TOOL=str(calib_tool),
                    SPX_AC_NOISE_TOOL=str(noise_tool),
                    SPX_AC_TAP_TOOL=str(tap_tool),
                    SPX_AC_READY_TOOL=str(ready_tool),
                    SPX_AC_LOCK_DIR=str(root / 'lock'),
                    SPX_AC_TMP_REQ=str(root / 'tmp.req'),
                    SPX_AC_TMP_TOUCH=str(root / 'tmp.touch'),
                    SPX_AC_BOOT_WAIT='0',
                    SPX_AC_ONCE='1',
                )
                res = subprocess.run(['sh', str(bridge)], env=env, text=True, capture_output=True)
                self.assertEqual(res.returncode, 0, res.stderr)
                state = {}
                for line in (app_files / 'state').read_text().splitlines():
                    if '=' in line:
                        k, v = line.split('=', 1)
                        state[k] = v
                reply = (app_files / 'reply').read_text().splitlines() if (app_files / 'reply').is_file() else []
                return state, reply

            # 1. Initial state includes Touch hardware + profile + SPXNOISE3 summary + tap
            state, _ = run_once()
            self.assertEqual(state['touch_available'], '1')
            self.assertEqual(state['touch_hid_id'], '045E:0C1A')
            self.assertEqual(state['touch_device_id'], dev_id)
            self.assertEqual(state['touch_profile_saved'], '1')
            self.assertIn('0.5000:0.5150', state['touch_profile_x'])
            self.assertEqual(state['touch_profile_size_max'], '2.4')
            self.assertEqual(state['touch_noise_saved'], '1')
            self.assertEqual(state['touch_noise_enabled'], '1')
            self.assertEqual(state['touch_noise_info'], f'SPXNOISE3 {dev_id} 1 1 56 84 nonzero=3/4704 max=255')
            self.assertEqual(state['touch_tap_saved'], '1')
            self.assertTrue(state['touch_tap_line'].startswith('SPXTAP1 '))

            # 2. Toggle ghost-touch patch OFF: creates .disabled, keeps .noise file, clears active
            state, reply = run_once('set-touch-ghost-patch\ntn-1\n0\n')
            self.assertEqual(reply[:2], ['tn-1', 'OK'])
            self.assertEqual(state['touch_noise_saved'], '1')
            self.assertEqual(state['touch_noise_enabled'], '0')
            self.assertTrue((touch_store / f'{dev_id}.noise').is_file())
            self.assertTrue((touch_store / f'{dev_id}.noise.disabled').exists())
            self.assertFalse((ndir / 'active').exists())
            self.assertEqual((ndir / 'command').read_text().strip(), f'reload {dev_id} tn-1')

            # Simulate iptsd restart while disabled: touch-calibration-boot honors .disabled
            state, _ = run_once()
            self.assertEqual(state['touch_noise_enabled'], '0')

            # 3. Toggle ghost-touch patch ON: removes .disabled and restores active
            state, reply = run_once('set-touch-ghost-patch\ntn-2\n1\n')
            self.assertEqual(reply[:2], ['tn-2', 'OK'])
            self.assertEqual(state['touch_noise_enabled'], '1')
            self.assertFalse((touch_store / f'{dev_id}.noise.disabled').exists())
            self.assertTrue((ndir / 'active').is_file())

            # 4. Reject mismatched device file & invalid noise payload without overwriting saved files
            other_dev = '0' * 64
            _, reply = run_once(f'import-touch-calibration\ntn-3\nVersion=1\nDevice={other_dev}\nCalibrationX=\n')
            self.assertEqual(reply[:2], ['tn-3', 'ERROR'])
            self.assertEqual(reply[2], 'device_mismatch')

            _, reply = run_once('import-touch-calibration\ntn-4\nVersion=1\nCalibrationX=bad\n')
            self.assertEqual(reply[:2], ['tn-4', 'ERROR'])
            self.assertEqual(reply[2], 'invalid_calibration_file')

            # 5. Import valid SPXNOISE1, SPXTAP1, and .profile files
            state, reply = run_once(f'import-touch-calibration\ntn-5\n{noise1_text}')
            self.assertEqual(reply, ['tn-5', 'OK', 'noise'])
            self.assertEqual(state['touch_noise_info'], f'SPXNOISE1 {dev_id} 1 1 56 84 4')

            tap_new = f'SPXTAP1 {dev_id} 1 1 56 84 5\n'
            state, reply = run_once(f'import-touch-calibration\ntn-6\n{tap_new}')
            self.assertEqual(reply, ['tn-6', 'OK', 'tap'])
            self.assertEqual(state['touch_tap_line'], tap_new.strip())

            prof_new = prof_text.replace('ContactSizeMax=2.4', 'ContactSizeMax=2.0')
            state, reply = run_once(f'import-touch-calibration\ntn-7\n{prof_new}')
            self.assertEqual(reply, ['tn-7', 'OK', 'profile'])
            self.assertEqual(state['touch_profile_size_max'], '2.0')
            self.assertIn('Calibrated=true', (run_dir / 'iptsd-calibrated.conf').read_text())
            self.assertIn('CalibrationX=', (tcb_files / 'context').read_text())
            self.assertIn('ready', calls_log.read_text())

            # 5b. Export single unified SPXTOUCH1 bundle (preserve NoiseDisabled=1 state too)
            state, reply = run_once('set-touch-ghost-patch\ntn-exp-dis\n0\n')
            self.assertEqual(reply[:2], ['tn-exp-dis', 'OK'])
            _, reply = run_once('export-touch-calibration\ntn-exp-all\n')
            self.assertEqual(reply[:2], ['tn-exp-all', 'OK'])
            bundle_text = '\n'.join(reply[2:]) + '\n'
            self.assertTrue(bundle_text.startswith('SPXTOUCH1\n'))
            self.assertIn(f'Device={dev_id}\n', bundle_text)
            self.assertIn('NoiseDisabled=1\n', bundle_text)
            self.assertIn('[Profile]\n', bundle_text)
            self.assertIn('[Noise]\n', bundle_text)
            self.assertIn('[Tap]\n', bundle_text)

            # Reject mismatched / truncated bundle cleanly before mutating store
            _, reply = run_once(f'import-touch-calibration\ntn-bad-b1\nSPXTOUCH1\nDevice={other_dev}\n[Profile]\nVersion=1\n[/Profile]\n')
            self.assertEqual(reply, ['tn-bad-b1', 'ERROR', 'device_mismatch'])
            _, reply = run_once(f'import-touch-calibration\ntn-bad-b2\nSPXTOUCH1\nDevice={dev_id}\n[Profile]\nVersion=1\n')
            self.assertEqual(reply, ['tn-bad-b2', 'ERROR', 'invalid_calibration_file'])

            # 6. Clear calibration data -> wipes saved files & resets context, then rejects toggle ON/export until restored
            state, reply = run_once('clear-touch-calibration\ntn-8\n')
            self.assertEqual(reply[:2], ['tn-8', 'OK'])
            self.assertEqual(state['touch_profile_saved'], '0')
            self.assertEqual(state['touch_noise_saved'], '0')
            self.assertEqual(state['touch_noise_enabled'], '0')
            self.assertEqual(state['touch_tap_saved'], '0')
            self.assertFalse((touch_store / f'{dev_id}.profile').exists())
            self.assertFalse((touch_store / f'{dev_id}.noise').exists())
            self.assertFalse((touch_store / f'{dev_id}.tap').exists())

            _, reply = run_once('set-touch-ghost-patch\ntn-9\n1\n')
            self.assertEqual(reply, ['tn-9', 'ERROR', 'no_noise_patch'])
            _, reply = run_once('export-touch-calibration\ntn-10\n')
            self.assertEqual(reply, ['tn-10', 'ERROR', 'not_found'])

            # 7. Import the exported single SPXTOUCH1 bundle -> restores all 3 sections + NoiseDisabled=1
            state, reply = run_once(f'import-touch-calibration\ntn-11\n{bundle_text}')
            self.assertEqual(reply, ['tn-11', 'OK', 'bundle'])
            self.assertEqual(state['touch_profile_saved'], '1')
            self.assertEqual(state['touch_profile_size_max'], '2.0')
            self.assertEqual(state['touch_noise_saved'], '1')
            self.assertEqual(state['touch_noise_enabled'], '0')
            self.assertEqual(state['touch_tap_saved'], '1')
            self.assertEqual(state['touch_tap_line'], tap_new.strip())
            self.assertTrue((touch_store / f'{dev_id}.noise.disabled').exists())

    def test_install_idempotent_and_hooks_init(self):
        records = {}
        init_base = b'#!/bin/sh\necho hi\nexec native-handoff /newroot\n'
        bodies = {'etc/sq2-qti-system/INSTALL_FILES': b'original\n', 'init': init_base}
        control.install(records, bodies, lambda mode: mode, apk=b'apk-fixture')
        original = dict(bodies)
        control.install(records, bodies, lambda mode: mode, apk=b'apk-fixture')
        self.assertEqual(bodies, original)
        self.assertEqual(bodies['init'].count(control.INIT_HOOK), 1)
        self.assertIn(control.INIT_HOOK + control.INIT_ANCHOR, bodies['init'])
        self.assertEqual(bodies['etc/sq2-qti-system/INSTALL_FILES'].splitlines(), [b'original', control.APK.encode()])
        self.assertEqual(records['etc/sq2-qti-system/root/' + control.APK], 0o100644)
        self.assertEqual(records[control.BRIDGE_DST], 0o100755)
        self.assertEqual(bodies['etc/googlebook-security/etc/surface-vulkan-app-routing.version'], b'1\n')

    def test_build_failure_propagates_without_payload_changes(self):
        bodies = {'etc/sq2-qti-system/INSTALL_FILES': b'original\n'}
        with patch.object(control, 'build_apk', side_effect=RuntimeError('compiler failed')):
            with self.assertRaisesRegex(RuntimeError, 'compiler failed'):
                control.install({}, bodies, lambda mode: mode)
        self.assertEqual(bodies, {'etc/sq2-qti-system/INSTALL_FILES': b'original\n'})

    def test_reuse_rebuilds_router_but_keeps_other_components(self):
        import full_image
        import full_image_camera
        import full_image_sensors
        import full_image_reuse as reuse
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            roots = {role: root / role for role in ('kernel', 'camera', 'sensors', 'mesa', 'glue')}
            names = {'kernel': ['Image', 'kernel.config', 'Module.symvers', 'System.map', 'build-audit.json',
                                'modules-stage/lib/modules/test/example.ko'],
                     'camera': ['CAMERA.json'], 'sensors': ['sensors-bundle.json'],
                     'mesa': [*reuse.MESA, 'vulkan.turnip.so'], 'glue': list(reuse.GLUE.values())}
            for role, files in names.items():
                for name in files:
                    path = roots[role] / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b'fixture')
            (roots['kernel'] / 'build-audit.json').write_text(json.dumps({
                'source_compiled': True, 'kernel_release': 'test', 'commands': [{'exit': 0}]}))
            manifest = root / 'reuse.json'
            manifest.write_text(json.dumps({'schema': 1, 'roots': {k: str(v) for k, v in roots.items()},
                                            'sha256': reuse.inventory(roots)}))
            with patch.object(full_image, 'policy', return_value={'kernel_release': 'test'}), \
                 patch.object(full_image_camera, 'load'), patch.object(full_image_sensors, 'load'):
                _, replacements, graphics, _ = reuse.load(manifest)
            self.assertNotIn('vulkan_router_bytes', graphics)
            self.assertEqual(graphics['gralloc_adapter_bytes'], b'fixture')
            self.assertEqual(graphics['turnip_bytes'], b'fixture')
            self.assertEqual(len(replacements), len(reuse.MESA))


if __name__ == '__main__':
    unittest.main()
