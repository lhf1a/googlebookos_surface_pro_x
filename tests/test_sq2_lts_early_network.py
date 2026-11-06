"""Host fixtures for the real shell helpers; no wireless/hardware success claim."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('packer', ROOT/'scripts/pack-sq2-lts-early-network.py')
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)


class EarlyNetwork(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for n in ('run', 'logs', 'etc', 'bin'):
            (self.root/n).mkdir()
        self.calls = self.root/'calls'
        self.calls.touch()
        (self.root/'cmdline').write_text('sq2.el2=1 nohibernate\n')
        (self.root/'etc/sq2-early-network.enabled').touch()
        source = (ROOT/'probe/test2/el2-early-network').read_text()
        source = source.replace('/proc/cmdline', str(self.root/'cmdline'))
        for prefix in ('/etc/', '/run/', '/logs'):
            source = source.replace(prefix, str(self.root)+prefix)
        source = source.replace('mkdir -p /run ', 'mkdir -p '+str(self.root/'run')+' ')
        self.helper = self.root/'helper'
        self.helper.write_text(source)
        self.env = dict(os.environ, PATH=str(self.root/'bin')+':'+os.environ['PATH'],
                        CALLS=str(self.calls))
        self.mock('start-ssh', 'echo ssh >> "$CALLS"')
        self.mock('el2-early-network-up', 'echo network >> "$CALLS"; exit "${NETWORK_RC:-0}"')

    def mock(self, name, body):
        p = self.root/'bin'/name
        p.write_text('#!/bin/sh\n'+body+'\n')
        p.chmod(0o755)

    def run_helper(self, action):
        return subprocess.run(['sh', str(self.helper), action], env=self.env,
                              capture_output=True, text=True, timeout=15)

    def test_start_order_and_network_failure(self):
        p = self.run_helper('start')
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(self.calls.read_text(), 'ssh\nnetwork\n')
        self.assertIn('SQ2_EARLY_NETWORK_READY', p.stdout)
        self.calls.write_text('')
        self.env['NETWORK_RC'] = '7'
        p = self.run_helper('start')
        self.assertEqual(p.returncode, 7)
        self.assertEqual(self.calls.read_text(), 'ssh\nnetwork\n')
        self.assertIn('exit=7', p.stdout)

    def test_opt_out_and_non_el2(self):
        for command in ('sq2.el2=1 sq2.ssh=0', 'sq2.el2=experimental', 'sq2.el2=0'):
            (self.root/'cmdline').write_text(command)
            self.assertEqual(self.run_helper('start').returncode, 0)
            self.assertEqual(self.calls.read_text(), '')
        (self.root/'cmdline').write_text('sq2.el2=1')
        (self.root/'etc/sq2-early-network.enabled').unlink()
        self.assertEqual(self.run_helper('start').returncode, 0)
        self.assertEqual(self.calls.read_text(), '')

    def sleeper(self, token):
        p = subprocess.Popen(['python3', '-c', 'import time;time.sleep(30)', token])
        # Reap promptly just like init, so kill -0 does not see a fixture zombie.
        t = threading.Thread(target=p.wait, daemon=True)
        t.start()
        def cleanup():
            if p.poll() is None:
                p.terminate()
            t.join(timeout=3)
        self.addCleanup(cleanup)
        return p

    def test_handoff_only_owned_clients(self):
        clients = []
        for n in ('sq2-early-udhcpc.pid', 'sq2-early-wpa.pid'):
            f = self.root/'run'/n
            p = self.sleeper(str(f))
            f.write_text(str(p.pid))
            clients.append(p)
        foreign = self.sleeper('android-owned')
        result = self.run_helper('handoff')
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertTrue(all(p.poll() is not None for p in clients))
        self.assertIsNone(foreign.poll())
        self.assertIn('SSH listener retained', result.stdout)
        self.assertEqual(self.calls.read_text(), '')

    def test_pid_reuse_and_malformed_pid_rejected(self):
        foreign = self.sleeper('android-owned')
        f = self.root/'run/sq2-early-udhcpc.pid'
        for value in (str(foreign.pid), '1', '-1', 'not-a-pid'):
            f.write_text(value)
            self.assertNotEqual(self.run_helper('handoff').returncode, 0)
            self.assertIsNone(foreign.poll())

    def test_empty_handoff(self):
        self.assertEqual(self.run_helper('handoff').returncode, 0)

    def test_real_network_helper_dynamic_interface_and_dhcp_error(self):
        source = (ROOT/'probe/test2/network-up').read_bytes().replace(b'\r\n', b'\n')
        network = packer.prepare_network_up(source).decode()
        for prefix in ('/run/', '/logs/', '/etc/', '/sys/class/net'):
            network = network.replace(prefix, str(self.root)+prefix)
        network = network.replace('mkdir -p /logs ', 'mkdir -p '+str(self.root/'logs')+' ')
        (self.root/'sys/class/net/wifi7/phy80211').mkdir(parents=True)
        script = self.root/'network-up'
        script.write_text(network)
        for name in ('wifi-services', 'modprobe', 'ip', 'wpa_supplicant'):
            self.mock(name, 'echo "'+name+' $*" >> "$CALLS"')
        self.mock('timeout', 'shift; exec "$@"')
        self.mock('wpa_cli', 'case "$*" in *ping) exit 1;; *status) echo wpa_state=COMPLETED;; esac')
        self.mock('udhcpc', 'echo "udhcpc $*" >> "$CALLS"; exit "${DHCP_RC:-0}"')
        for status in ('0', '9'):
            self.calls.write_text('')
            self.env['DHCP_RC'] = status
            p = subprocess.run(['sh', str(script)], env=self.env, capture_output=True, text=True)
            self.assertEqual(p.returncode, int(status), p.stderr)
            calls = self.calls.read_text()
            self.assertIn('ip link set wifi7 up', calls)
            self.assertIn('udhcpc -i wifi7 ', calls)
            self.assertIn('-P '+str(self.root/'run/sq2-early-wpa.pid'), calls)
            self.assertIn('-O '+str(self.root/'run/sq2-early-wpa'), calls)
            self.assertIn('-p '+str(self.root/'run/sq2-early-udhcpc.pid'), calls)
            self.assertIn('-s /bin/el2-early-dhcp-hook', calls)
            self.assertNotIn('wlan0', calls)
            self.assertEqual((self.root/'run/sq2-early-network.iface').read_text(), 'wifi7\n')

    def test_unknown_network_source_is_rejected(self):
        source = (ROOT/'probe/test2/network-up').read_bytes().replace(b'\r\n', b'\n')
        with self.assertRaises(AssertionError):
            packer.prepare_network_up(source.replace(b'wpa_supplicant -B', b'changed-supplicant -B'))

    def test_dhcp_console_and_log_output(self):
        source = (ROOT/'probe/test2/el2-early-dhcp-hook').read_text()
        source = source.replace('/bin/dhcp-hook', str(self.root/'bin/dhcp-hook'))
        source = source.replace('/logs', str(self.root/'logs')).replace('/dev/console', str(self.root/'console'))
        script = self.root/'dhcp-report'
        script.write_text(source)
        self.mock('dhcp-hook', 'echo "configure $*" >> "$CALLS"; exit "${HOOK_RC:-0}"')
        self.mock('ip', 'echo "observed $*"')
        env = dict(self.env, interface='wifi7', ip='192.0.2.20', subnet='255.255.255.0',
                   router='192.0.2.1', dns='192.0.2.53', serverid='192.0.2.1', lease='3600',
                   psk='DO_NOT_PRINT_THIS')
        for status in ('0', '7'):
            env['HOOK_RC'] = status
            result = subprocess.run(['sh', str(script), 'bound'], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, int(status), result.stderr)
            for field in ('address=192.0.2.20', 'gateway=192.0.2.1', 'dns=192.0.2.53',
                          'server=192.0.2.1', 'lease_seconds=3600', 'hook_exit='+status):
                self.assertIn(field, result.stdout)
                self.assertIn(field, (self.root/'logs/early-dhcp.txt').read_text())
                self.assertIn(field, (self.root/'console').read_text())
            self.assertNotIn('DO_NOT_PRINT_THIS', result.stdout)


if __name__ == '__main__':
    unittest.main()
