"""Host-only tests: policy/i18n/sorting, router routing, bridge daemon, and packaging."""
import json
import os
from pathlib import Path
import re
import shutil
import signal
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
        cert_pem = self.work / 'test-cert.pem'
        key_pem = self.work / 'test-key.pem'
        if shutil.which('openssl'):
            subprocess.run(
                ['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                 '-keyout', str(key_pem), '-out', str(cert_pem),
                 '-subj', '/CN=spx-test', '-days', '1'],
                check=True, capture_output=True, text=True,
            )
        runner = self.work / 'PolicyAndStringsTest.java'
        runner.write_text(r'''
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.spec.ECGenParameterSpec;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.Locale;
import org.surface.advancedcontrol.DriverPolicy;
import org.surface.advancedcontrol.Strings;

public class PolicyAndStringsTest {
  static void check(boolean b, String msg) {
    if (!b) throw new AssertionError(msg);
  }
  static String makeEd25519BlobB64(byte fill) throws Exception {
    ByteArrayOutputStream baos = new ByteArrayOutputStream();
    DataOutputStream dos = new DataOutputStream(baos);
    byte[] t = "ssh-ed25519".getBytes(StandardCharsets.US_ASCII);
    dos.writeInt(t.length);
    dos.write(t);
    byte[] k = new byte[32];
    Arrays.fill(k, fill);
    dos.writeInt(k.length);
    dos.write(k);
    dos.flush();
    return Base64.getEncoder().encodeToString(baos.toByteArray());
  }
  public static void main(String[] args) throws Exception {
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
    check(DriverPolicy.touchDisabledRequest("abc-123", true)
        .equals("set-touch-disabled\nabc-123\n1\n"), "touchDisableOnReq");
    check(DriverPolicy.touchDisabledRequest("abc-123", false)
        .equals("set-touch-disabled\nabc-123\n0\n"), "touchDisableOffReq");
    check("禁用触摸".equals(zh.touchDisableTitle()), "zh touch disable title");
    check("Disable Touchscreen".equals(en.touchDisableTitle()), "en touch disable title");
    check(zh.touchHomeSubtitle(true, true, false, false, false).contains("已禁用"), "zh home touch disabled");
    check(en.touchHomeSubtitle(true, true, false, false, false).contains("disabled"), "en home touch disabled");
    check(zh.touchValueHardware("045E:0C21", "0123456789abcdef", false, true).contains("已禁用"), "zh hw disabled");
    check(en.touchValueHardware("045E:0C21", "0123456789abcdef", false, true).contains("disabled"), "en hw disabled");

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

    // Debugging requests, key/certificate normalization & bilingual strings
    check(DriverPolicy.adbTcpRequest("abc-123", true).equals("set-adb-tcp\nabc-123\n1\n"), "adbOnReq");
    check(DriverPolicy.adbTcpRequest("abc-123", false).equals("set-adb-tcp\nabc-123\n0\n"), "adbOffReq");
    check(DriverPolicy.sshEnabledRequest("abc-123", true).equals("set-ssh-enabled\nabc-123\n1\n"), "sshOnReq");
    check(DriverPolicy.sshEnabledRequest("abc-123", false).equals("set-ssh-enabled\nabc-123\n0\n"), "sshOffReq");
    check(DriverPolicy.sshPasswordRequest("abc-123", "-Pass@123!").equals("set-ssh-password\nabc-123\n-Pass@123!\n"), "sshPwReq");
    check(DriverPolicy.sshPasswordRequest("abc-123", "").equals("set-ssh-password\nabc-123\n\n"), "sshPwClearReq");
    check(DriverPolicy.clearSshKeysRequest("abc-123").equals("clear-ssh-keys\nabc-123\n"), "sshClearReq");

    for (String badPw : new String[]{"bad\nline", "bad\0null", new String(new char[129]).replace('\0', 'a')}) {
      try { DriverPolicy.sshPasswordRequest("abc-123", badPw); throw new AssertionError("badPw"); }
      catch (IllegalArgumentException expected) {}
    }

    String edB64 = makeEd25519BlobB64((byte) 0x42);
    String edLine1 = "ssh-ed25519 " + edB64 + " user@host";
    String edLine2 = "ssh-ed25519 " + edB64 + " duplicate-comment";
    String rfc4716 = "---- BEGIN SSH2 PUBLIC KEY ----\nComment: \"converted\"\n"
        + edB64.substring(0, 32) + "\n" + edB64.substring(32) + "\n---- END SSH2 PUBLIC KEY ----\n";
    String normEd = DriverPolicy.normalizeSshKeyImport("# header comment\n" + edLine1 + "\n" + edLine2 + "\n" + rfc4716);
    check(normEd.equals(edLine1 + "\n"), "ed dedup: " + normEd);
    check(DriverPolicy.importSshKeysRequest("abc-123", edLine1).equals("import-ssh-keys\nabc-123\n" + edLine1 + "\n"), "importSshReq");

    KeyPairGenerator rsaGen = KeyPairGenerator.getInstance("RSA");
    rsaGen.initialize(2048);
    KeyPair rsaKp = rsaGen.generateKeyPair();
    String rsaPem = "-----BEGIN PUBLIC KEY-----\n"
        + Base64.getMimeEncoder(64, "\n".getBytes(StandardCharsets.US_ASCII)).encodeToString(rsaKp.getPublic().getEncoded())
        + "\n-----END PUBLIC KEY-----\n";
    check(DriverPolicy.normalizeSshKeyImport(rsaPem).startsWith("ssh-rsa "), "rsa spki pem");

    KeyPairGenerator ecGen = KeyPairGenerator.getInstance("EC");
    ecGen.initialize(new ECGenParameterSpec("secp256r1"));
    KeyPair ecKp = ecGen.generateKeyPair();
    String ecPem = "-----BEGIN PUBLIC KEY-----\n"
        + Base64.getMimeEncoder(64, "\n".getBytes(StandardCharsets.US_ASCII)).encodeToString(ecKp.getPublic().getEncoded())
        + "\n-----END PUBLIC KEY-----\n";
    check(DriverPolicy.normalizeSshKeyImport(ecPem).startsWith("ecdsa-sha2-nistp256 "), "ec spki pem");

    if (args.length > 0 && !args[0].isEmpty()) {
      String certContent = new String(Files.readAllBytes(Paths.get(args[0])), StandardCharsets.UTF_8);
      String certSsh = DriverPolicy.normalizeSshKeyImport(certContent);
      check(certSsh.startsWith("ssh-rsa ") && certSsh.endsWith(" imported-cert\n"), "x509 cert: " + certSsh);
    }

    for (String badKey : new String[]{
        "", "   \n# only comment\n", "not-a-key AAAAAAAAAAAAAAAAAAAAAAAA",
        "ssh-ed25519 AAAABBBBCCCCDDDD", "ssh-ed25519 " + edB64 + "\0bad",
        "-----BEGIN CERTIFICATE-----\nnot-closed"
    }) {
      try { DriverPolicy.normalizeSshKeyImport(badKey); throw new AssertionError("badKey: " + badKey); }
      catch (IllegalArgumentException expected) {}
    }

    check("调试".equals(zh.debugTitle()), "zh debug title");
    check("Debugging".equals(en.debugTitle()), "en debug title");
    check(en.debugTitle().equals(jpFallback.debugTitle()), "jp->en debug title");
    check("网络 ADB".equals(zh.debugAdbTitle()), "zh adb title");
    check("Network ADB".equals(en.debugAdbTitle()), "en adb title");
    check("SSH 服务".equals(zh.debugSshTitle()), "zh ssh title");
    check("SSH Service".equals(en.debugSshTitle()), "en ssh title");
    check(zh.debugAdbSummary(true, true, "5555", "192.168.0.1", "wlan0:192.168.1.50").contains("192.168.0.1:5555"), "vm ip in adb summary");

    System.out.println("DRIVER_POLICY_AND_I18N_OK");
  }
}
''')
        subprocess.run(['javac', '--release', '8', '-encoding', 'UTF-8', '-d', str(self.work),
                        str(ROOT / 'probe/advanced-control/DriverPolicy.java'),
                        str(ROOT / 'probe/advanced-control/Strings.java'),
                        str(runner)], check=True)
        run_args = ['java', '-cp', str(self.work), 'PolicyAndStringsTest']
        if cert_pem.is_file():
            run_args.append(str(cert_pem))
        result = subprocess.run(run_args, capture_output=True, text=True)
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

            # 8. Disable Touchscreen: stops iptsd, persists across reboot, keeps hardware discoverable, and re-enables cleanly
            dummy_iptsd = subprocess.Popen(['sleep', '60'])
            try:
                (run_dir / 'iptsd.pid').write_text(f'{dummy_iptsd.pid}\n')
                state, _ = run_once()
                self.assertEqual(state['touch_iptsd_running'], '1')
                self.assertEqual(state['touch_disabled'], '0')

                _, reply = run_once('set-touch-disabled\ntn-dis-bad\n2\n')
                self.assertEqual(reply, ['tn-dis-bad', 'ERROR', 'invalid_arguments'])

                state, reply = run_once('set-touch-disabled\ntn-dis-1\n1\n')
                self.assertEqual(reply[:2], ['tn-dis-1', 'OK'])
                dummy_iptsd.wait(timeout=5)
                self.assertEqual(state['touch_disabled'], '1')
                self.assertEqual(state['touch_iptsd_running'], '0')
                self.assertEqual(state['touch_available'], '1')
                self.assertTrue((run_dir / 'touch-disabled').exists())
                self.assertEqual((data_root / 'data/vendor/spx-advanced-control/touch_disabled').read_text().strip(), '1')
                self.assertFalse((run_dir / 'iptsd.pid').exists())
            finally:
                if dummy_iptsd.poll() is None:
                    dummy_iptsd.kill()
                    dummy_iptsd.wait(timeout=5)

            # Simulate reboot with persisted touch_disabled=1: restore_touch_disabled_on_boot stops early iptsd
            (run_dir / 'touch-disabled').unlink()
            boot_iptsd = subprocess.Popen(['sleep', '60'])
            try:
                (run_dir / 'iptsd.pid').write_text(f'{boot_iptsd.pid}\n')
                state, _ = run_once()
                boot_iptsd.wait(timeout=5)
                self.assertEqual(state['touch_disabled'], '1')
                self.assertEqual(state['touch_iptsd_running'], '0')
                self.assertTrue((run_dir / 'touch-disabled').exists())
            finally:
                if boot_iptsd.poll() is None:
                    boot_iptsd.kill()
                    boot_iptsd.wait(timeout=5)

            # Re-enable touch: clears persistent + runtime flags and calls TOUCH_READY_TOOL
            calls_log.write_text('')
            state, reply = run_once('set-touch-disabled\ntn-dis-0\n0\n')
            self.assertEqual(reply[:2], ['tn-dis-0', 'OK'])
            self.assertEqual(state['touch_disabled'], '0')
            self.assertFalse((run_dir / 'touch-disabled').exists())
            self.assertFalse((data_root / 'data/vendor/spx-advanced-control/touch_disabled').exists())
            self.assertIn('ready', calls_log.read_text())

            # Verify touch boot/resume scripts respect /run/touch-disabled and pass sh -n
            for helper_name in ('touch-services', 'touch-framework-ready', 'touch-resume-watch', 'touch-calibration-boot'):
                helper_src = (ROOT / 'probe/test2' / helper_name).read_text(encoding='utf-8')
                subprocess.run(['sh', '-n'], input=helper_src, text=True, check=True)
                self.assertIn('/run/touch-disabled', helper_src)

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

    def test_bridge_daemon_debug_adb_and_ssh_flow(self):
        bridge = ROOT / 'probe/test2/spx-advanced-control-boot'
        with tempfile.TemporaryDirectory(prefix='spx-debug-bridge-test-') as td:
            root = Path(td)
            data_root = root / 'root'
            sysfs = root / 'sys'
            run_dir = root / 'run'
            etc_dir = root / 'etc'
            home_root = root / 'home_root'
            logs_dir = root / 'logs'
            cmdline = root / 'cmdline'
            cmdline.write_text('console=tty0 androidboot.hardware=android-desktop\n')

            run_dir.mkdir(parents=True, exist_ok=True)
            etc_dir.mkdir(parents=True, exist_ok=True)
            home_root.mkdir(parents=True, exist_ok=True)
            logs_dir.mkdir(parents=True, exist_ok=True)
            (etc_dir / 'shadow').write_text('root:!:0:0:99999:7:::\n')

            app_files = data_root / 'data/user_de/0/org.surface.advancedcontrol/files'
            app_files.mkdir(parents=True, exist_ok=True)

            props_file = root / 'props.json'
            props_file.write_text('{}\n')
            fw_file = root / 'firewall.json'
            fw_file.write_text('{"iptables": [], "ip6tables": []}\n')
            calls_log = root / 'android_calls.log'

            wrapper = root / 'mock-android'
            wrapper.write_text(f'''#!/usr/bin/env python3
import json, pathlib, sys
props_p = pathlib.Path({str(props_file)!r})
fw_p = pathlib.Path({str(fw_file)!r})
calls_p = pathlib.Path({str(calls_log)!r})
args = sys.argv[1:]
with calls_p.open('a') as f:
    f.write(' '.join(args) + '\\n')
if args == ['cmd', 'activity', 'get-current-user']:
    print('0')
elif args[:1] == ['getprop'] and len(args) == 2:
    data = json.loads(props_p.read_text())
    print(data.get(args[1], ''))
elif args[:1] == ['setprop'] and len(args) == 3:
    data = json.loads(props_p.read_text())
    if args[1] == 'ctl.start' and args[2] == 'adbd':
        data['init.svc.adbd'] = 'running'
    elif args[1] == 'ctl.stop' and args[2] == 'adbd':
        data['init.svc.adbd'] = 'stopped'
    else:
        data[args[1]] = args[2]
    props_p.write_text(json.dumps(data))
elif args[:3] == ['settings', 'put', 'global']:
    data = json.loads(props_p.read_text())
    data['settings.' + args[3]] = args[4]
    props_p.write_text(json.dumps(data))
elif args[:1] in (['iptables'], ['ip6tables']):
    tool = args[0]
    rest = args[1:]
    if rest and rest[0] == '-w':
        rest = rest[1:]
    fw = json.loads(fw_p.read_text())
    rules = fw[tool]
    op = rest[0] if rest else ''
    rule = rest[2:]
    if op == '-C':
        sys.exit(0 if rule in rules else 1)
    elif op == '-I':
        rule = rest[3:]
        rules.insert(0, rule)
        fw_p.write_text(json.dumps(fw))
    elif op == '-D':
        if rule in rules:
            rules.remove(rule)
            fw_p.write_text(json.dumps(fw))
        else:
            sys.exit(1)
else:
    sys.exit(2)
''')
            wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)

            mock_ip = root / 'mock-ip'
            mock_ip.write_text('''#!/bin/sh
cat <<'EOF'
1: lo    inet 10.255.255.254/32 scope global lo
3: avf_tap_fixed    inet 192.168.0.1/24 brd 192.168.0.255 scope global avf_tap_fixed
4: wlan0    inet 192.168.1.50/24 brd 192.168.1.255 scope global wlan0
EOF
''')
            mock_ip.chmod(mock_ip.stat().st_mode | stat.S_IXUSR)

            mock_dropbear = root / 'dropbear'
            mock_dropbear.write_text(f'''#!/usr/bin/env python3
import pathlib, subprocess, sys, time
if sys.argv[1:] == ['--daemon-sleep']:
    time.sleep(60)
    sys.exit(0)
pidfile = None
args = sys.argv[1:]
i = 0
while i < len(args):
    if args[i] == '-P' and i + 1 < len(args):
        pidfile = args[i + 1]
        i += 2
    else:
        i += 1
with pathlib.Path({str(calls_log)!r}).open('a') as f:
    f.write('dropbear ' + ' '.join(args) + '\\n')
proc = subprocess.Popen(
    [sys.argv[0], '--daemon-sleep'],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
    start_new_session=True,
)
if pidfile:
    pathlib.Path(pidfile).write_text(str(proc.pid) + '\\n')
''')
            mock_dropbear.chmod(mock_dropbear.stat().st_mode | stat.S_IXUSR)

            mock_dropbearkey = root / 'dropbearkey'
            mock_dropbearkey.write_text('''#!/bin/sh
keyfile=""
while [ $# -gt 0 ]; do
    case "$1" in
        -f) keyfile="$2"; shift 2 ;;
        *) shift ;;
    esac
done
printf 'MOCK-ED25519-HOST-KEY\n' > "$keyfile"
''')
            mock_dropbearkey.chmod(mock_dropbearkey.stat().st_mode | stat.S_IXUSR)

            mock_mkpasswd = root / 'mkpasswd'
            mock_mkpasswd.write_text('''#!/usr/bin/env python3
import hashlib, sys
pw = sys.stdin.readline().rstrip('\\n') if '-P' in sys.argv else sys.argv[-1]
digest = hashlib.sha256(pw.encode('utf-8')).hexdigest()[:32]
print(f'$6$testsalt${digest}')
''')
            mock_mkpasswd.chmod(mock_mkpasswd.stat().st_mode | stat.S_IXUSR)

            def run_once(req_body=None):
                if req_body is not None:
                    (app_files / 'request').write_text(req_body)
                env = dict(
                    os.environ,
                    SPX_AC_ROOT=str(data_root),
                    SPX_AC_SYSFS=str(sysfs),
                    SPX_AC_RUN=str(run_dir),
                    SPX_AC_ETC=str(etc_dir),
                    SPX_AC_HOME_ROOT=str(home_root),
                    SPX_AC_LOGS=str(logs_dir),
                    SPX_AC_CMDLINE=str(cmdline),
                    SPX_AC_ANDROID_WRAPPER=str(wrapper),
                    SPX_AC_IP_BIN=str(mock_ip),
                    SPX_AC_DROPBEAR_BIN=str(mock_dropbear),
                    SPX_AC_DROPBEARKEY_BIN=str(mock_dropbearkey),
                    SPX_AC_MKPASSWD_BIN=str(mock_mkpasswd),
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
                return state, reply, res.stdout

            try:
                # 1. Default boot keeps Network ADB and SSH OFF and reports VM/LAN IPs
                state, _, _ = run_once()
                self.assertEqual(state['adb_tcp_enabled'], '0')
                self.assertEqual(state['adb_tcp_active'], '0')
                self.assertEqual(state['ssh_available'], '1')
                self.assertEqual(state['ssh_enabled'], '0')
                self.assertEqual(state['ssh_running'], '0')
                self.assertEqual(state['ssh_password_set'], '0')
                self.assertEqual(state['ssh_key_count'], '0')
                self.assertEqual(state['debug_vm_ip'], '192.168.0.1')
                self.assertEqual(state['debug_lan_ips'], 'wlan0:192.168.1.50')

                # 2. Enable Network ADB -> sets port 5555, restarts adbd, opens INPUT tcp 5555 without DROP
                state, reply, _ = run_once('set-adb-tcp\ndbg-1\n1\n')
                self.assertEqual(reply, ['dbg-1', 'OK'])
                self.assertEqual(state['adb_tcp_enabled'], '1')
                self.assertEqual(state['adb_tcp_active'], '1')
                self.assertEqual(state['adb_tcp_port'], '5555')
                props = json.loads(props_file.read_text())
                self.assertEqual(props['settings.adb_enabled'], '1')
                self.assertEqual(props['service.adb.tcp.port'], '5555')
                self.assertEqual(props['init.svc.adbd'], 'running')
                fw = json.loads(fw_file.read_text())
                self.assertEqual(len(fw['iptables']), 1)
                self.assertEqual(len(fw['ip6tables']), 1)
                self.assertIn('5555', fw['iptables'][0])
                self.assertIn('ACCEPT', fw['iptables'][0])
                self.assertNotIn('DROP', calls_log.read_text())

                # 3. Simulated reboot restores ADB TCP; rescue boot skips restore
                props_file.write_text('{}\n')
                fw_file.write_text('{"iptables": [], "ip6tables": []}\n')
                state, _, stdout = run_once()
                self.assertIn('SPX_AC_BOOT_ADB_TCP_RESTORED 1', stdout)
                self.assertEqual(state['adb_tcp_active'], '1')

                props_file.write_text('{}\n')
                fw_file.write_text('{"iptables": [], "ip6tables": []}\n')
                cmdline.write_text('console=tty0 sq2.rescue=1\n')
                state, _, stdout = run_once()
                self.assertIn('SPX_AC_BOOT_DEBUG_RESTORE_SKIPPED rescue', stdout)
                self.assertEqual(state['adb_tcp_active'], '0')
                cmdline.write_text('console=tty0\n')

                # 4. Disable Network ADB -> clears firewall rules and resets property to -1
                state, reply, _ = run_once('set-adb-tcp\ndbg-2\n0\n')
                self.assertEqual(reply, ['dbg-2', 'OK'])
                self.assertEqual(state['adb_tcp_enabled'], '0')
                self.assertEqual(state['adb_tcp_active'], '0')
                self.assertEqual(state['adb_tcp_port'], '-1')
                self.assertEqual(json.loads(fw_file.read_text()), {'iptables': [], 'ip6tables': []})

                # 4b. Android mica adb-usb-bridge mode: localhost:5555 at boot -> adb_tcp_active=0;
                # enabling clears service.adb.listen_addrs; disabling restores tcp:localhost:5555.
                usb_bridge = data_root / 'vendor/bin/adb-usb-bridge'
                usb_bridge.parent.mkdir(parents=True, exist_ok=True)
                usb_bridge.write_text('#!/bin/sh\n')
                usb_bridge.chmod(0o755)
                props_file.write_text(json.dumps({
                    'service.adb.tcp.port': '5555',
                    'service.adb.listen_addrs': 'tcp:localhost:5555',
                    'init.svc.adb-usb-bridge': 'running',
                }) + '\n')
                state, _, _ = run_once()
                self.assertEqual(state['adb_tcp_enabled'], '0')
                self.assertEqual(state['adb_tcp_active'], '0')
                state, reply, _ = run_once('set-adb-tcp\ndbg-2b\n1\n')
                self.assertEqual(reply, ['dbg-2b', 'OK'])
                self.assertEqual(state['adb_tcp_active'], '1')
                props = json.loads(props_file.read_text())
                self.assertEqual(props['service.adb.listen_addrs'], '')
                self.assertEqual(props['service.adb.tcp.port'], '5555')
                state, reply, _ = run_once('set-adb-tcp\ndbg-2c\n0\n')
                self.assertEqual(reply, ['dbg-2c', 'OK'])
                self.assertEqual(state['adb_tcp_active'], '0')
                props = json.loads(props_file.read_text())
                self.assertEqual(props['service.adb.listen_addrs'], 'tcp:localhost:5555')
                self.assertEqual(props['service.adb.tcp.port'], '5555')
                usb_bridge.unlink()
                props_file.write_text('{}\n')

                # 5. Enable SSH -> generates host key, starts Dropbear on 22, opens firewall
                state, reply, _ = run_once('set-ssh-enabled\ndbg-3\n1\n')
                self.assertEqual(reply, ['dbg-3', 'OK'])
                self.assertEqual(state['ssh_enabled'], '1')
                self.assertEqual(state['ssh_running'], '1')
                self.assertTrue((data_root / 'data/vendor/spx-advanced-control/ssh/dropbear_ed25519_host_key').is_file())
                self.assertTrue((etc_dir / 'dropbear/dropbear_ed25519_host_key').is_file())
                self.assertTrue((run_dir / 'dropbear.pid').is_file())
                self.assertEqual(len(json.loads(fw_file.read_text())['iptables']), 1)

                # 6. Set SSH password (including leading '-') -> hashes into store & shadow, never stores plaintext
                secret_pw = '-Pass@123!'
                state, reply, _ = run_once(f'set-ssh-password\ndbg-4\n{secret_pw}\n')
                self.assertEqual(reply, ['dbg-4', 'OK'])
                self.assertEqual(state['ssh_password_set'], '1')
                saved_hash = (data_root / 'data/vendor/spx-advanced-control/ssh/root_shadow_hash').read_text().strip()
                self.assertTrue(saved_hash.startswith('$6$'))
                self.assertNotIn(secret_pw, saved_hash)
                self.assertIn(saved_hash, (etc_dir / 'shadow').read_text())

                # 7. Import SSH keys (with dedup) & reject invalid key payload
                k1 = 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIMockKeyData111111111111111111111111 test1@vm'
                k2 = 'ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDMockRsaData2222222222222222222 test2@vm'
                state, reply, _ = run_once(f'import-ssh-keys\ndbg-5\n{k1}\n{k1}\n{k2}\n')
                self.assertEqual(reply, ['dbg-5', 'OK', '2 2'])
                self.assertEqual(state['ssh_key_count'], '2')
                auth_keys = home_root / '.ssh/authorized_keys'
                self.assertEqual(stat.S_IMODE(auth_keys.stat().st_mode), 0o600)
                self.assertEqual(len(auth_keys.read_text().splitlines()), 2)

                _, reply, _ = run_once('import-ssh-keys\ndbg-6\nnot-a-valid-key abc\n')
                self.assertEqual(reply, ['dbg-6', 'ERROR', 'invalid_ssh_key'])

                # 8. Clear SSH keys & clear SSH password
                state, reply, _ = run_once('clear-ssh-keys\ndbg-7\n')
                self.assertEqual(reply, ['dbg-7', 'OK'])
                self.assertEqual(state['ssh_key_count'], '0')
                self.assertEqual(auth_keys.read_text(), '')

                state, reply, _ = run_once('set-ssh-password\ndbg-8\n\n')
                self.assertEqual(reply, ['dbg-8', 'OK'])
                self.assertEqual(state['ssh_password_set'], '0')
                self.assertTrue((etc_dir / 'shadow').read_text().startswith('root:!:'))

                # 9. Re-set password + key, stop SSH -> locks live shadow while preserving saved config
                state, reply, _ = run_once('set-ssh-password\ndbg-9\nkeep-secret\n')
                self.assertEqual(reply, ['dbg-9', 'OK'])
                state, reply, _ = run_once(f'import-ssh-keys\ndbg-10\n{k1}\n')
                self.assertEqual(reply, ['dbg-10', 'OK', '1 1'])

                old_pid = int((run_dir / 'dropbear.pid').read_text().strip())
                state, reply, _ = run_once('set-ssh-enabled\ndbg-11\n0\n')
                self.assertEqual(reply, ['dbg-11', 'OK'])
                self.assertEqual(state['ssh_enabled'], '0')
                self.assertEqual(state['ssh_running'], '0')
                self.assertEqual(state['ssh_password_set'], '1')
                self.assertEqual(state['ssh_key_count'], '1')
                self.assertTrue((etc_dir / 'shadow').read_text().startswith('root:!:'))
                self.assertEqual(json.loads(fw_file.read_text()), {'iptables': [], 'ip6tables': []})
                with self.assertRaises(ProcessLookupError):
                    os.kill(old_pid, 0)

                # 10. Enable SSH again -> restores live shadow & restarts Dropbear; simulated reboot restores SSH
                state, reply, _ = run_once('set-ssh-enabled\ndbg-12\n1\n')
                self.assertEqual(reply, ['dbg-12', 'OK'])
                self.assertFalse((etc_dir / 'shadow').read_text().startswith('root:!:'))
                pid_to_kill = int((run_dir / 'dropbear.pid').read_text().strip())
                os.kill(pid_to_kill, signal.SIGKILL)
                (run_dir / 'dropbear.pid').unlink()
                (etc_dir / 'dropbear/dropbear_ed25519_host_key').unlink()
                # Simulate fresh initramfs RAM /root/.ssh directory on reboot
                if (home_root / '.ssh').is_symlink():
                    (home_root / '.ssh').unlink()
                elif (home_root / '.ssh').is_dir():
                    shutil.rmtree(home_root / '.ssh')
                (home_root / '.ssh').mkdir(mode=0o700)
                state, _, stdout = run_once()
                self.assertIn('SPX_AC_BOOT_SSH_RESTORED 1', stdout)
                self.assertEqual(state['ssh_running'], '1')
                self.assertEqual(state['ssh_key_count'], '1')
                self.assertTrue((etc_dir / 'dropbear/dropbear_ed25519_host_key').is_file())
                self.assertTrue((home_root / '.ssh').is_symlink())
                self.assertIn(k1, (home_root / '.ssh/authorized_keys').read_text())

                # 11. Keys installed externally via ssh-copy-id into /root/.ssh/authorized_keys
                # persist directly in /data and survive reboot without going through import-ssh-keys.
                k3 = 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIMockCopyIdKey3333333333333333333333 copyid@laptop'
                copy_id_res = subprocess.run(
                    ['sh', '-c',
                     'cd "$1" && umask 077 && mkdir -p .ssh && '
                     '{ [ -z "$(tail -c 1 .ssh/authorized_keys 2>/dev/null)" ] || echo >> .ssh/authorized_keys; } && '
                     'cat >> .ssh/authorized_keys',
                     'sh', str(home_root)],
                    input=k3 + '\n',
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(copy_id_res.returncode, 0, copy_id_res.stderr)
                state, reply, _ = run_once('refresh\ndbg-13\n')
                self.assertEqual(reply, ['dbg-13', 'OK'])
                self.assertEqual(state['ssh_key_count'], '2')

                # Simulate another reboot: wipe initramfs /root/.ssh and dropbear pid/host key
                pid_to_kill = int((run_dir / 'dropbear.pid').read_text().strip())
                os.kill(pid_to_kill, signal.SIGKILL)
                (run_dir / 'dropbear.pid').unlink()
                (etc_dir / 'dropbear/dropbear_ed25519_host_key').unlink()
                (home_root / '.ssh').unlink()
                (home_root / '.ssh').mkdir(mode=0o700)
                # Also plant a legacy single-file store key to verify upgrade migration merges cleanly
                k4 = 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIMockLegacyKey44444444444444444444444 legacy@old'
                legacy_ak = data_root / 'data/vendor/spx-advanced-control/ssh/authorized_keys'
                legacy_ak.write_text(k4 + '\n')
                state, _, stdout = run_once()
                self.assertIn('SPX_AC_BOOT_SSH_RESTORED 1', stdout)
                self.assertEqual(state['ssh_key_count'], '3')
                self.assertFalse(legacy_ak.exists())
                restored_ak = (home_root / '.ssh/authorized_keys').read_text()
                self.assertIn(k1, restored_ak)
                self.assertIn(k3, restored_ak)
                self.assertIn(k4, restored_ak)
                self.assertEqual(stat.S_IMODE((home_root / '.ssh/authorized_keys').stat().st_mode), 0o600)

                # Clear keys removes ssh-copy-id keys permanently across reboot
                state, reply, _ = run_once('clear-ssh-keys\ndbg-14\n')
                self.assertEqual(reply, ['dbg-14', 'OK'])
                self.assertEqual(state['ssh_key_count'], '0')
                self.assertEqual((home_root / '.ssh/authorized_keys').read_text(), '')
            finally:
                pid_file = run_dir / 'dropbear.pid'
                if pid_file.is_file():
                    try:
                        os.kill(int(pid_file.read_text().strip()), signal.SIGKILL)
                    except Exception:
                        pass

    def test_apk_build_and_settings_icon_metadata(self):
        with tempfile.TemporaryDirectory(prefix='spx-apk-icon-test-') as td:
            apk_path = Path(td) / 'SurfaceAdvancedControl.apk'
            control.build_apk(apk_path)
            aapt2 = control._find_sdk() / 'android-15/aapt2'
            dump = subprocess.check_output(
                [str(aapt2), 'dump', 'xmltree', str(apk_path), '--file', 'AndroidManifest.xml'],
                text=True,
            )
            self.assertIn('versionCode(0x0101021b)=4', dump)
            self.assertRegex(dump, r'android:icon\(0x01010002\)=@0x7f[0-9a-f]{6}')
            self.assertIn('com.android.settings.icon', dump)
            self.assertRegex(
                dump,
                r'com\.android\.settings\.icon"\)\s*\n\s*A: http://schemas\.android\.com/apk/res/android:resource\(0x01010025\)=@0x7f[0-9a-f]{6}',
            )
            self.assertIn('com.android.settings.icon_tintable', dump)


if __name__ == '__main__':
    unittest.main()
