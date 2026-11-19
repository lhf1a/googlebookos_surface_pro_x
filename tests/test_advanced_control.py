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
