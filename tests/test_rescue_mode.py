"""End-to-end host tests for Rescue Mode (SSH, Wi-Fi TUI, USB NIC, Android handoff) and Hybrid config."""
import ctypes
import ctypes.util
import gzip
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import disk_image
import full_image
import full_image_anonymous
import full_image_camera
import full_image_rescue
import full_image_sensors
import full_image_typec


def make_exec(path, body):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#!/bin/sh\n' + body + '\n')
    path.chmod(0o755)
    return path


class HybridConfigTests(unittest.TestCase):
    def test_hybrid_kernel_config_is_default_and_has_usb_nic_drivers(self):
        cfg_path = ROOT / 'configs/kernel-6.18-hybrid.config'
        self.assertTrue(cfg_path.is_file())
        self.assertEqual(full_image.DEFAULT_KERNEL_CONFIG, cfg_path)
        text = cfg_path.read_text()
        for opt in (
            'CONFIG_LOCALVERSION="-sq2-android1"',
            'CONFIG_USB_RTL8152=y',
            'CONFIG_USB_USBNET=m',
            'CONFIG_USB_NET_AX8817X=m',
            'CONFIG_USB_NET_AX88179_178A=m',
            'CONFIG_USB_NET_CDCETHER=m',
            'CONFIG_USB_NET_CDC_NCM=m',
            'CONFIG_USB_NET_RNDIS_HOST=m',
            'CONFIG_USB_WDM=m',
            'CONFIG_ATH10K_SNOC=m',
        ):
            self.assertIn(opt + '\n', text)

        entry = disk_image.module('test_build_lts_cfg', 'build-lts.py')
        from unittest.mock import patch
        captured = {}
        with patch.object(entry, 'compile_kernel', side_effect=lambda a: captured.update(config=a.config) or {}):
            import io
            from contextlib import redirect_stdout
            with redirect_stdout(io.StringIO()):
                entry.main(['compile', '--source', '/s', '--build-dir', '/b', '--output', '/o',
                            '--cross-compile', 'aarch64-linux-gnu-', '--core-only'])
        self.assertEqual(captured['config'], cfg_path)

    def test_assemble_runtime_rejects_retired_baseline_mode(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            runtime = root / 'full-image-runtime'
            (runtime / 'files').mkdir(parents=True)
            (runtime / 'files/init').write_bytes(b'#!/bin/sh\n' + full_image.ANCHOR)
            (runtime / 'records.json').write_text('{"init": ["070701", [0, 33261, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0], ""]}')
            from unittest.mock import patch
            with patch.object(full_image, 'module', return_value=type('M', (), {'install': lambda *a: None})()):
                with self.assertRaisesRegex(ValueError, 'hybrid'):
                    full_image.assemble_runtime(root, root / 'out.gz', graphics='baseline')


class RescueSshTests(unittest.TestCase):
    def test_rescue_ssh_guards_normal_boot_sets_000000_and_imports_keys(self):
        script = ROOT / 'probe/rescue/spx-rescue-ssh'
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            bin_dir = root / 'mockbin'
            bin_dir.mkdir()
            calls = root / 'calls.log'

            # Mock mkpasswd using Python's libcrypt or deterministic sha512 hash if host mkpasswd absent.
            mkpasswd_path = shutil.which('mkpasswd')
            if mkpasswd_path:
                make_exec(bin_dir / 'mkpasswd', f'exec "{mkpasswd_path}" "$@"')
            else:
                make_exec(bin_dir / 'mkpasswd', """
python3 - "$2" "$3" <<'PY'
import ctypes, ctypes.util, sys
lc = ctypes.CDLL(ctypes.util.find_library('crypt'))
lc.crypt.restype = ctypes.c_char_p
print(lc.crypt(sys.argv[2].encode(), b'$6$spxrescuetest$').decode())
PY
""")

            make_exec(bin_dir / 'dropbearkey', """
keyfile=""
while [ "$#" -gt 0 ]; do
    if [ "$1" = "-f" ]; then keyfile="$2"; shift 2; else shift; fi
done
printf 'MOCK_ED25519_HOST_KEY\\n' > "$keyfile"
echo "dropbearkey:$keyfile" >> "$TEST_CALLS"
""")

            # Mock dropbear: writes pidfile and sleeps with 'dropbear' in /proc/<pid>/cmdline.
            make_exec(bin_dir / 'dropbear', """
echo "dropbear:$*" >> "$TEST_CALLS"
pidfile=""
while [ "$#" -gt 0 ]; do
    if [ "$1" = "-P" ]; then pidfile="$2"; shift 2; else shift; fi
done
sh -c 'sleep 60' dropbear >/dev/null 2>&1 </dev/null &
echo $! > "$pidfile"
""")

            cmdline = root / 'cmdline'
            (root / 'etc').mkdir()
            (root / 'etc/shadow').write_text('root:!:0:0:99999:7:::\n')
            (root / 'media/esp/rescue').mkdir(parents=True)
            (root / 'media/esp/rescue/authorized_keys').write_text(
                '# comment\r\n'
                'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestKeyOne user@host\r\n'
                'invalid-line-ignored\r\n'
            )
            (root / 'media/esp/authorized_keys').write_text(
                'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestKeyOne user@host\n'
                'ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQTestKeyTwo admin@lab\n'
            )

            env = dict(
                os.environ,
                PATH=f'{bin_dir}:/usr/bin:/bin',
                SPX_RESCUE_PATH=f'{bin_dir}:/usr/bin:/bin',
                SPX_RESCUE_ROOT=str(root),
                SPX_RESCUE_CMDLINE=str(cmdline),
                TEST_CALLS=str(calls),
            )

            # 1. Normal boot without sq2.rescue=1 MUST be refused and leave shadow locked.
            cmdline.write_text('console=tty0 sq2.el2=1 sq2.vulkan=mesa\n')
            r_refuse = subprocess.run(['sh', str(script), 'start'], env=env, capture_output=True, text=True)
            self.assertNotEqual(r_refuse.returncode, 0)
            self.assertEqual((root / 'etc/shadow').read_text(), 'root:!:0:0:99999:7:::\n')

            # 2. Rescue boot enables 000000 password, imports pubkeys, generates host key, starts dropbear.
            cmdline.write_text('console=tty0 sq2.el2=1 sq2.rescue=1\n')
            r_start = subprocess.run(['sh', str(script), 'start'], env=env, capture_output=True, text=True)
            self.assertEqual(r_start.returncode, 0, r_start.stderr + r_start.stdout)
            self.assertIn('SPX_RESCUE_SSH_PASSWORD=default_000000', r_start.stdout)
            self.assertIn('SPX_RESCUE_SSH_KEYS count=2 imported=2', r_start.stdout)
            self.assertIn('SPX_RESCUE_SSH_READY', r_start.stdout)

            # Verify password hash genuinely matches '000000' via libcrypt.
            shadow_line = (root / 'etc/shadow').read_text().strip()
            pw_hash = shadow_line.split(':')[1]
            self.assertTrue(pw_hash.startswith('$6$'), pw_hash)
            libcrypt_name = ctypes.util.find_library('crypt')
            if libcrypt_name:
                lc = ctypes.CDLL(libcrypt_name)
                lc.crypt.restype = ctypes.c_char_p
                self.assertEqual(lc.crypt(b'000000', pw_hash.encode()), pw_hash.encode())
                self.assertNotEqual(lc.crypt(b'wrong', pw_hash.encode()), pw_hash.encode())

            # Verify authorized_keys deduplication, CRLF stripping, and strict permissions.
            ak_path = root / 'root/.ssh/authorized_keys'
            ak_lines = ak_path.read_text().splitlines()
            self.assertEqual(len(ak_lines), 2)
            self.assertNotIn('\r', ak_path.read_text())
            self.assertEqual(ak_path.stat().st_mode & 0o777, 0o600)
            self.assertEqual((root / 'root/.ssh').stat().st_mode & 0o777, 0o700)

            # Verify host key was persisted to ESP and dropbear allows both password and pubkey (no -s/-g).
            self.assertTrue((root / 'media/esp/rescue/dropbear_ed25519_host_key').is_file())
            call_text = calls.read_text()
            db_line = [ln for ln in call_text.splitlines() if ln.startswith('dropbear:')][0]
            self.assertNotIn(' -s', db_line)
            self.assertNotIn(' -g', db_line)
            self.assertIn('-p 22', db_line)

            # 3. Stop cleanly kills dropbear and re-locks root shadow.
            r_stop = subprocess.run(['sh', str(script), 'stop'], env=env, capture_output=True, text=True)
            self.assertEqual(r_stop.returncode, 0, r_stop.stderr)
            self.assertEqual((root / 'etc/shadow').read_text(), 'root:!:0:0:99999:7:::\n')


class RescueNetAndHandoffTests(unittest.TestCase):
    def test_rescue_net_loads_usb_nic_and_wifi_then_hands_off_cleanly(self):
        net_script = ROOT / 'probe/rescue/spx-rescue-net'
        handoff_script = ROOT / 'probe/rescue/spx-rescue-handoff'
        start_script = ROOT / 'probe/rescue/start-android'

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            bin_dir = root / 'mockbin'
            calls = root / 'calls.log'
            release = os.uname().release

            for d in (
                'run/wpa_supplicant', 'logs', 'etc', 'media/esp',
                'sys/bus/usb/devices/1-2:1.0', 'sys/bus/usb/devices/1-2',
                'sys/class/net/wlan0/phy80211',
                'sys/class/net/eth0',
                'sys/devices/pci0/usb1/1-2/net/eth0',
                f'lib/modules/{release}',
                'dev/block/by-name',
            ):
                (root / d).mkdir(parents=True, exist_ok=True)
            (root / 'sys/class/net/eth0/device').symlink_to(root / 'sys/devices/pci0/usb1/1-2')
            (root / 'sys/class/net/eth0/carrier').write_text('1\n')
            (root / 'sys/bus/usb/devices/1-2:1.0/modalias').write_text('usb:v0B95p1790ic02isc06\n')
            (root / 'sys/bus/usb/devices/1-2/devnum').write_text('3\n')
            (root / 'etc/sq2-usb-network.aliases').write_text('usb:v0B95p1790* ax88179_178a\n')
            (root / f'lib/modules/{release}/modules.dep').write_text(
                'kernel/ax88179_178a.ko: kernel/usbnet.ko\n'
            )

            make_exec(bin_dir / 'modprobe', 'echo "modprobe:$*" >> "$TEST_CALLS"')
            make_exec(bin_dir / 'ip', 'echo "ip:$*" >> "$TEST_CALLS"')
            make_exec(bin_dir / 'udhcpc', """
echo "udhcpc:$*" >> "$TEST_CALLS"
pidfile=""
while [ "$#" -gt 0 ]; do
    if [ "$1" = "-p" ]; then pidfile="$2"; shift 2; else shift; fi
done
sh -c 'sleep 60' udhcpc >/dev/null 2>&1 </dev/null &
echo $! > "$pidfile"
""")
            make_exec(bin_dir / 'wpa_supplicant', """
echo "wpa_supplicant:$*" >> "$TEST_CALLS"
pidfile=""
while [ "$#" -gt 0 ]; do
    if [ "$1" = "-P" ]; then pidfile="$2"; shift 2; else shift; fi
done
sh -c 'sleep 60' wpa_supplicant >/dev/null 2>&1 </dev/null &
echo $! > "$pidfile"
""")
            make_exec(bin_dir / 'wpa_cli', """
echo "wpa_cli:$*" >> "$TEST_CALLS"
case "$*" in
    *ping*) exit 1 ;;
    *terminate*) echo OK; exit 0 ;;
    *) echo OK; exit 0 ;;
esac
""")
            make_exec(bin_dir / 'spx-rescue-usb-gadget', 'echo "gadget:$*" >> "$TEST_CALLS"; rm -f "$SPX_RESCUE_ROOT/run/sq2-rescue-usb-gadget.active"')
            make_exec(bin_dir / 'cpu-frequency-start', 'echo "cpu-frequency-start" >> "$TEST_CALLS"')
            make_exec(bin_dir / 'spx-camera-boot', 'echo "spx-camera-boot" >> "$TEST_CALLS"')
            make_exec(bin_dir / 'spx-sensors-boot', 'echo "spx-sensors-boot" >> "$TEST_CALLS"')
            make_exec(bin_dir / 'mock-mount', """
echo "mount:$*" >> "$TEST_CALLS"
# Simulate bind mount by copying source to destination file.
cp "$2" "$3"
""")

            cmdline = root / 'cmdline'
            cmdline.write_text('console=tty0 sq2.el2=1 sq2.vulkan=mesa sq2.rescue=1 sq2.usb=experimental spx.boot_token=' + 'a' * 64 + '\n')

            env = dict(
                os.environ,
                PATH=f'{bin_dir}:/usr/bin:/bin',
                SPX_RESCUE_PATH=f'{bin_dir}:/usr/bin:/bin',
                SPX_RESCUE_ROOT=str(root),
                SPX_RESCUE_CMDLINE=str(cmdline),
                SPX_GADGET_BIN=str(bin_dir / 'spx-rescue-usb-gadget'),
                SPX_RESCUE_NET_BIN=str(net_script),
                SPX_CAMERA_BOOT_BIN=str(bin_dir / 'spx-camera-boot'),
                SPX_SENSORS_BOOT_BIN=str(bin_dir / 'spx-sensors-boot'),
                SPX_MOUNT_BIN=str(bin_dir / 'mock-mount'),
                TEST_CALLS=str(calls),
            )

            # 1. USB NIC load + DHCP pass.
            r_usb = subprocess.run(['sh', str(net_script), 'usb-once'], env=env, capture_output=True, text=True)
            self.assertEqual(r_usb.returncode, 0, r_usb.stderr)
            log = calls.read_text().splitlines()
            self.assertIn('modprobe:usbnet', log)
            self.assertIn('modprobe:ax88179_178a', log)
            self.assertTrue(any(ln.startswith('ip:link set eth0 up') for ln in log))
            self.assertTrue(any('udhcpc:-i eth0' in ln for ln in log))

            # 2. Wi-Fi init starts wpa_supplicant with control interface.
            r_wifi = subprocess.run(['sh', str(net_script), 'wifi-init'], env=env, capture_output=True, text=True)
            self.assertEqual(r_wifi.returncode, 0, r_wifi.stderr)
            self.assertIn('RESCUE_WIFI_READY iface=wlan0', r_wifi.stdout)
            self.assertTrue((root / 'run/sq2-rescue-wpa.pid').is_file())
            self.assertIn('ctrl_interface=', (root / 'run/sq2-rescue-wpa.conf').read_text())

            # Simulate active rescue USB gadget too so we verify handoff tears it down.
            (root / 'run/sq2-rescue-usb-gadget.active').write_text('usb0\n')

            # 3. start-android refuses when rescue is not active, then succeeds once active.
            r_no_active = subprocess.run(['sh', str(start_script)], env=env, capture_output=True, text=True)
            self.assertNotEqual(r_no_active.returncode, 0)

            (root / 'run/sq2-rescue-active').write_text('999\n')
            # Simulate a manual userdata mount conflict check.
            fake_ud = root / 'dev/loop9'
            fake_ud.write_text('')
            (root / 'dev/block/by-name/userdata').symlink_to(fake_ud)
            mounts = root / 'mounts'
            mounts.write_text(f'{fake_ud} /mnt/rescue-data ext4 rw 0 0\n')
            env['SPX_RESCUE_MOUNTS'] = str(mounts)
            r_conflict = subprocess.run(['sh', str(start_script)], env=env, capture_output=True, text=True)
            self.assertNotEqual(r_conflict.returncode, 0)
            self.assertIn('userdata:/mnt/rescue-data', r_conflict.stderr)

            mounts.write_text('')
            r_go = subprocess.run(['sh', str(start_script)], env=env, capture_output=True, text=True)
            self.assertEqual(r_go.returncode, 0, r_go.stderr)
            self.assertIn('SPX_START_ANDROID_REQUESTED mode=normal stop_ssh=0', r_go.stdout)
            self.assertTrue((root / 'run/sq2-rescue-continue').is_file())

            # 4. PID 1 sources spx-rescue-handoff: verify Wi-Fi + USB + gadget handoff & cmdline restoration.
            r_handoff = subprocess.run(
                ['sh', '-c', f'rescue=1; debug=1; smoke=0; ssd_smoke=0; . "{handoff_script}"; echo "VARS rescue=$rescue debug=$debug"'],
                env=env, capture_output=True, text=True,
            )
            self.assertEqual(r_handoff.returncode, 0, r_handoff.stderr + r_handoff.stdout)
            self.assertIn('VARS rescue=0 debug=0', r_handoff.stdout)

            final_log = calls.read_text()
            self.assertIn('wpa_cli:-p', final_log)
            self.assertIn('terminate', final_log)
            self.assertIn('ip:addr flush dev wlan0', final_log)
            self.assertIn('ip:link set wlan0 down', final_log)
            self.assertIn('ip:addr flush dev eth0', final_log)
            self.assertIn('ip:link set eth0 down', final_log)
            self.assertIn('gadget:stop', final_log)
            self.assertIn('cpu-frequency-start', final_log)
            self.assertIn('spx-camera-boot', final_log)
            self.assertIn('spx-sensors-boot', final_log)

            # Verify cmdline had sq2.rescue=1 removed and sq2.video.c2=experimental restored!
            resumed_cmdline = cmdline.read_text().strip().split()
            self.assertNotIn('sq2.rescue=1', resumed_cmdline)
            self.assertIn('sq2.video.c2=experimental', resumed_cmdline)
            self.assertIn('sq2.usb=experimental', resumed_cmdline)
            self.assertIn('spx.boot_token=' + 'a' * 64, resumed_cmdline)


class WifiTuiQemuTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('aarch64-linux-gnu-gcc') and shutil.which('qemu-aarch64'),
                         'Requires aarch64-linux-gnu-gcc and qemu-aarch64')
    def test_compiled_arm64_wifi_tui_scan_connect_disconnect_status(self):
        elf = full_image_rescue.build_wifi_tui_bytes()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            tui = root / 'spx-wifi-tui'
            tui.write_bytes(elf)
            tui.chmod(0o755)

            bin_dir = root / 'bin'
            sys_net = root / 'sys/class/net'
            run_dir = root / 'run'
            (sys_net / 'wlan0/phy80211').mkdir(parents=True)
            (sys_net / 'eth0').mkdir(parents=True)
            run_dir.mkdir()
            calls = root / 'wpa_calls.log'

            # Mock wpa_cli returning UTF-8 hex SSID, duplicate BSSIDs, control-char rogue SSID, WPA2/WPA3/OPEN.
            make_exec(bin_dir / 'wpa_cli', """
echo "$*" >> "$TEST_CALLS"
# Shift past -p <ctrl> -i <iface>
shift 4
case "$1" in
    ping) echo PONG ;;
    scan) echo OK ;;
    scan_results)
        printf 'bssid / frequency / signal level / flags / ssid\\n'
        printf 'aa:bb:cc:dd:ee:01\\t5180\\t-48\\t[WPA2-PSK-CCMP][ESS]\\tOffice\\x20WiFi\\n'
        printf 'aa:bb:cc:dd:ee:02\\t2437\\t-72\\t[WPA2-PSK-CCMP][ESS]\\tOffice\\x20WiFi\\n'
        printf 'aa:bb:cc:dd:ee:03\\t5220\\t-55\\t[WPA2-PSK+SAE-CCMP][ESS]\\t\\xe6\\xb5\\x8b\\xe8\\xaf\\x95WiFi\\n'
        printf 'aa:bb:cc:dd:ee:04\\t2412\\t-60\\t[ESS]\\tGuestOpen\\n'
        printf 'aa:bb:cc:dd:ee:05\\t2462\\t-65\\t[SAE-H2E][ESS]\\tEvil\\x1b[31mAP\\n'
        printf 'aa:bb:cc:dd:ee:06\\t2412\\t-50\\t[WPA2-PSK-CCMP][ESS]\\t\\n'
        ;;
    list_networks)
        printf 'network id / ssid / bssid / flags\\n'
        ;;
    add_network)
        echo 0
        ;;
    set_network|enable_network|select_network|disconnect|remove_network)
        echo OK
        ;;
    status)
        if [ -f "$TEST_CONNECTED" ]; then
            printf 'wpa_state=COMPLETED\\nssid=Office\\x20WiFi\\n'
        else
            printf 'wpa_state=DISCONNECTED\\n'
        fi
        ;;
esac
""")
            make_exec(bin_dir / 'ip', """
case "$*" in
    *"dev wlan0"*)
        if [ -f "$TEST_CONNECTED" ]; then
            echo "3: wlan0    inet 192.168.10.42/24 brd 192.168.10.255 scope global wlan0"
        fi
        ;;
    *"dev eth0"*)
        echo "2: eth0    inet 192.168.50.9/24 brd 192.168.50.255 scope global eth0"
        ;;
esac
""")
            make_exec(bin_dir / 'udhcpc', """
echo "udhcpc:$*" >> "$TEST_CALLS"
touch "$TEST_CONNECTED"
""")

            env = dict(
                os.environ,
                SPX_WPA_CLI=str(bin_dir / 'wpa_cli'),
                SPX_WPA_CTRL=str(run_dir / 'wpa_supplicant'),
                SPX_UDHCPC=str(bin_dir / 'udhcpc'),
                SPX_IP_BIN=str(bin_dir / 'ip'),
                SPX_RUN_DIR=str(run_dir),
                SPX_SYS_NET=str(sys_net),
                TEST_CALLS=str(calls),
                TEST_CONNECTED=str(root / 'connected'),
            )

            # 1. --scan: deduplicated, sorted by signal, UTF-8 decoded, ANSI escape stripped!
            r_scan = subprocess.run(['qemu-aarch64', str(tui), '--scan', '--no-trigger'],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(r_scan.returncode, 0, r_scan.stderr)
            lines = r_scan.stdout.splitlines()
            self.assertEqual(len(lines), 5, r_scan.stdout)  # header + 4 unique non-empty SSIDs
            self.assertIn('Office WiFi', lines[1])
            self.assertIn('-48', lines[1])  # Kept stronger 5GHz AP over -72 duplicate
            self.assertIn('测试WiFi', lines[2])
            self.assertIn('WPA2/3', lines[2])
            self.assertIn('GuestOpen', lines[3])
            self.assertIn('OPEN', lines[3])
            self.assertNotIn('\x1b', r_scan.stdout)  # Control chars sanitized!
            self.assertIn('Evil?[31mAP', lines[4])

            # 2. --connect with UTF-8 SSID & WPA2/3 passphrase passes hex SSID safely.
            (root / 'connected').write_text('1')
            r_conn = subprocess.run(['qemu-aarch64', str(tui), '--connect', '测试WiFi', 'SecretPass123'],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(r_conn.returncode, 0, r_conn.stderr + r_conn.stdout)
            self.assertIn('192.168.10.42', r_conn.stdout)
            log = calls.read_text()
            self.assertIn('set_network 0 ssid e6b58be8af9557694669', log)
            self.assertIn('set_network 0 key_mgmt WPA-PSK SAE', log)
            self.assertIn('set_network 0 ieee80211w 1', log)
            self.assertIn('set_network 0 psk "SecretPass123"', log)
            self.assertIn('udhcpc:-i wlan0', log)

            # 3. --status reports both Wi-Fi and USB Ethernet IPs.
            r_st = subprocess.run(['qemu-aarch64', str(tui), '--status'],
                                  env=env, capture_output=True, text=True)
            self.assertEqual(r_st.returncode, 0, r_st.stderr)
            self.assertIn('wifi_iface=wlan0 state=COMPLETED ssid=Office WiFi ip=192.168.10.42', r_st.stdout)
            self.assertIn('usb_iface=eth0 ip=192.168.50.9', r_st.stdout)


class RescueInitPipelineIntegrationTests(unittest.TestCase):
    def test_full_init_pipeline_preserves_all_anchors_and_syntax(self):
        """Verify rescue install composes cleanly with camera, sensors, typec, anonymous, and disk_image."""
        init_src = (ROOT / 'probe/test2/init-googlebook-native').read_bytes().replace(b'\r\n', b'\n')
        # Add the lines present in the real 6.18 runtime init (qti-system-display-stage, el2-terminal, radio, modules.dep.bb).
        init_src = init_src.replace(
            b'modprobe af_key >> /logs/module-load.txt 2>&1 || fail af-key-module\n',
            b'rm -f /lib/modules/"$(uname -r)"/modules.dep.bb\n'
            b'modprobe af_key >> /logs/module-load.txt 2>&1 || fail af-key-module\n',
            1,
        )
        init_src = init_src.replace(
            b'exec > /dev/console 2>&1\n',
            b'if grep -qw "sq2.el2=1" /proc/cmdline; then\n'
            b' el2-terminal-autostart > /logs/el2-terminal-autostart.txt 2>&1 &\nfi\n'
            b'exec > /dev/console 2>&1\n',
            1,
        )
        init_src = init_src.replace(
            b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n',
            b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n'
            b'el2-terminal-stage > /logs/el2-terminal.txt 2>&1 || fail el2-terminal-stage\n'
            b'radio-stage > /logs/radio-stage.txt 2>&1 || fail radio-stage\n'
            b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n',
            1,
        )
        init_src = init_src.replace(b'exec native-handoff /newroot', full_image.HOOK + b'exec native-handoff /newroot', 1)

        fake_elf = b'\x7fELF\x02\x01' + b'\0' * 12 + (183).to_bytes(2, 'little') + b'\0' * 44
        fake_typec_ko = fake_elf + b'parmtype=boot_storage_ports:uint\0spx_boot_handoff=1\0'

        bodies = {
            'init': init_src,
            'bin/apply-single-user': b'#!/bin/sh\n',
            'bin/dropbear': fake_elf,
            'bin/start-ssh': b'#!/bin/sh\n',
            'etc/shadow': b'root:OldLabShadowHash:0:0:99999:7:::\n',
            'etc/wpa_supplicant.conf': b'network={ssid="OldLabSSID" psk="OldLabSecret"}\n',
            'bin/surface-swap-boot': b'#!/bin/sh\n',
            'etc/spx-typec/phy-spx-qmp-combo.ko': fake_elf,
            'etc/spx-typec/spx-typec.ko': fake_typec_ko,
            'bin/spx-typec-stage': full_image_typec.STAGE,
            'etc/sq2-qti-system/root/bin/hw/vendor.qti.hardware.display.composer-service': b'composer',
            'etc/sq2-qti-system/INSTALL_FILES': b'bin/hw/vendor.qti.hardware.display.composer-service\n',
        }
        records = {n: full_image.new_record(0o100755 if n.startswith('bin/') or n == 'init' else 0o100644) for n in bodies}

        # Camera & Sensors boot hooks (insert before `if [ "$rescue" = 1 ]; then`)
        bodies['init'] = bodies['init'].replace(full_image_camera.BOOT, full_image_camera.BOOT_HOOK + full_image_camera.BOOT, 1)
        bodies['init'] = bodies['init'].replace(full_image_sensors.BOOT, full_image_sensors.BOOT_HOOK + full_image_sensors.BOOT, 1)

        # Type-C init hooks
        bodies['init'] = bodies['init'].replace(
            b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n',
            b'/bin/spx-typec-stage > /logs/spx-typec.txt 2>&1 || echo SPX_TYPEC_STAGE_FAILED\n'
            b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n',
            1,
        )
        bodies['init'] = bodies['init'].replace(
            b'for name in apex bootstrap-apex linkerconfig mnt tmp storage data_mirror; do ',
            b'echo 1 > /sys/bus/platform/devices/surface-typec/events_enabled 2>> /logs/spx-typec.txt || echo SPX_TYPEC_EVENTS_FAILED\n'
            b'for name in apex bootstrap-apex linkerconfig mnt tmp storage data_mirror; do ',
            1,
        )

        # Anonymous privacy cleanup
        full_image_anonymous.apply(records, bodies, full_image.new_record)
        self.assertEqual(bodies['etc/shadow'], b'root:!:0:0:99999:7:::\n')

        # Rescue install (idempotent!)
        meta = full_image_rescue.install(records, bodies, full_image.new_record, compiled_wifi_tui=fake_elf)
        self.assertEqual(meta['rescue_ssh_default_password'], '000000')
        before_second = dict(bodies)
        full_image_rescue.install(records, bodies, full_image.new_record, compiled_wifi_tui=fake_elf)
        self.assertEqual(bodies, before_second)

        # Pack and pass through disk_image.storage_payload (typec_early_handoff + optimize_boot_init + optional_boot_failures)!
        c = disk_image.cpio_module()
        base = gzip.compress(
            b''.join(c.entry(n, bodies[n], records[n][1][1]) for n in sorted(bodies))
            + c.entry('TRAILER!!!', b'', 0),
            mtime=0,
        )
        payload, _, _ = disk_image.storage_payload(base, [dict(name='super', index=1, bytes=2048 * 512)], anonymous=True)
        after = c.entries(gzip.decompress(payload))
        final_init = after['init'][1]

        # Validate shell syntax and exact ordering in final initramfs /init!
        subprocess.run(['sh', '-n'], input=final_init, check=True, capture_output=True)
        self.assertIn(b'if [ "$debug" = 1 ] && [ "$rescue" = 0 ]; then', final_init)
        self.assertIn(b'/bin/spx-rescue-session || true', final_init)
        self.assertIn(b'. /bin/spx-rescue-handoff', final_init)
        # Disk discovery runs before rescue stop; Android mount and native-handoff come after!
        self.assertLess(final_init.index(b'. /bin/prepare-native-ssd'), final_init.index(b'/bin/spx-rescue-session'))
        self.assertLess(final_init.index(b'. /bin/spx-rescue-handoff'), final_init.index(b'mount -t erofs -o ro /dev/mapper/system_a /newroot'))
        self.assertLess(final_init.index(b'mount -t erofs -o ro /dev/mapper/system_a /newroot'), final_init.index(b'exec native-handoff /newroot'))
        # Normal boot shadow remains locked in the final initramfs!
        self.assertEqual(after['etc/shadow'][1], b'root:!:0:0:99999:7:::\n')
        for req in ('bin/spx-wifi-tui', 'bin/wifi-tui', 'bin/start-android', 'bin/continue-android',
                    'bin/spx-rescue-ssh', 'bin/spx-rescue-net', 'bin/spx-rescue-usb-gadget',
                    'bin/spx-rescue-session', 'bin/spx-rescue-shell', 'bin/spx-rescue-handoff'):
            self.assertIn(req, after)


if __name__ == '__main__':
    unittest.main()
