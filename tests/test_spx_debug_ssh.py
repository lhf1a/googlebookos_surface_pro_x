"""Temporary early SSH host fixtures. No hardware, backup or rollback operations."""
import importlib.util
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
spec=importlib.util.spec_from_file_location('spx_debug_pack',ROOT/'scripts/pack-spx-debug-ssh.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)


class DebugSSH(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        for n in ('bin','etc','run','logs','newroot/vendor/etc/init','newroot/system/etc/init','newroot/debug_ramdisk'):
            (self.root/n).mkdir(parents=True,exist_ok=True)
        self.enabled=self.root/'etc/sq2-early-network.enabled';self.enabled.touch()
        (self.root/'cmdline').write_text('sq2.el2=1')
        self.calls=self.root/'calls';self.calls.touch()
        script=(ROOT/'probe/test2/spx-early-ssh').read_text()
        script=script.replace('/proc/cmdline',str(self.root/'cmdline'))
        script=re.sub(r'/(?:etc|run|logs|newroot)/[a-zA-Z0-9_./-]*',
                      lambda match:str(self.root)+match[0],script)
        script=script.replace('mkdir -p /run /logs','mkdir -p '+str(self.root/'run')+' '+str(self.root/'logs'))
        self.helper=self.root/'helper';self.helper.write_text(script)
        self.env=dict(os.environ,PATH=str(self.root/'bin')+':'+os.environ['PATH'],
                      CALLS=str(self.calls),ENABLED=str(self.enabled))
        self.mock('start-ssh','echo listen >> "$CALLS"')
        self.mock('el2-early-network-up','echo network >> "$CALLS"; exit "${NETWORK_RC:-0}"')
        self.mock('spx-early-ssh','echo "helper $*" >> "$CALLS"')
        self.mock('ip','echo "ip $*" >> "$CALLS"; case "$*" in *"addr show dev"*) echo "4: wifi7 inet 192.0.2.20/24 scope global wifi7";; esac')
        self.mock('ipcalc','echo NETWORK=192.0.2.0')
        self.mock('sleep','rm -f "$ENABLED"')
        self.mock('mount','test "$1" = --bind; cp "$2" "$3"')

    def mock(self,name,body):
        path=self.root/'bin'/name;path.write_text('#!/bin/sh\n'+body+'\n');path.chmod(0o755)

    def run_helper(self,action):
        return subprocess.run(['sh',str(self.helper),action],env=self.env,capture_output=True,text=True,timeout=10)

    def test_listener_before_network_without_boot_completed(self):
        result=self.run_helper('start')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(self.calls.read_text().startswith('listen\nnetwork\n'))
        self.assertIn('SPX_EARLY_SSH_NETWORK_READY',result.stdout)
        self.assertNotIn('boot_completed',self.helper.read_text())

    def test_network_failure_is_not_reported_as_connectivity(self):
        self.env['NETWORK_RC']='7'
        result=self.run_helper('start')
        self.assertEqual(result.returncode,7)
        self.assertIn('listener alone is not connectivity',result.stdout)
        self.assertNotIn('NETWORK_READY',result.stdout)

    def test_optout_and_no_marker_leave_network_alone(self):
        (self.root/'cmdline').write_text('sq2.el2=1 sq2.ssh=0')
        self.assertEqual(self.run_helper('start').returncode,0)
        self.assertEqual(self.calls.read_text(),'')
        (self.root/'cmdline').write_text('sq2.el2=1');self.enabled.unlink()
        self.assertEqual(self.run_helper('start').returncode,0)
        self.assertEqual(self.calls.read_text(),'')

    def test_ram_service_overlay_preserves_other_services_and_actions(self):
        rc=self.root/'newroot/vendor/etc/init/wifi.rc'
        rc.write_text('service vendor.wifi_hal_legacy /vendor/bin/wifi\n    class hal\n\nservice keep.me /bin/keep\n    class main\non boot\n    setprop fixture yes\n')
        wc=self.root/'newroot/system/etc/init/wificond.rc'
        wc.write_text('service wificond /system/bin/wificond\n    class main\non boot\n    setprop another yes\n')
        result=self.run_helper('stage')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertNotIn('service vendor.wifi_hal_legacy',rc.read_text())
        self.assertIn('service keep.me /bin/keep\n    class main\non boot\n    setprop fixture yes',rc.read_text())
        self.assertEqual(wc.read_text(),'on boot\n    setprop another yes\n')

    def test_missing_android_owner_is_a_real_error(self):
        self.assertNotEqual(self.run_helper('stage').returncode,0)

    def test_only_direct_lan_rule_not_default_route(self):
        (self.root/'run/sq2-early-network.iface').write_text('wifi7\n')
        result=self.run_helper('lan-watch')
        self.assertEqual(result.returncode,0,result.stderr)
        calls=self.calls.read_text()
        self.assertIn('route replace 192.0.2.0/24 dev wifi7 scope link table main',calls)
        self.assertIn('rule add priority 49 to 192.0.2.0/24 lookup main',calls)
        self.assertNotIn('default',calls)


if __name__=='__main__':unittest.main()
