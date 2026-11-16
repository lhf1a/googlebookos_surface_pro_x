"""Run the real first-boot shell flow with Android command leaves mocked."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class FirstBoot(unittest.TestCase):
    def run_helper(self, *, packages="com.google.android.setupwizard\ncom.google.android.desktop.setupwizard", user="0", ready="1", fail="", stamped=False):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); log=root/"calls"; stamp=root/"stamp"
            if stamped: stamp.write_text("v1\n")
            src=(ROOT/"probe/test2/surface-first-boot").read_text()
            leaf='android() { timeout 15 nsenter -t 1 -m -r -- /system/bin/"$@"; }'
            mock='''android() {
    printf '%s\n' "$*" >> "$LOG"
    [ -z "$FAIL" ] || [ "$*" != "$FAIL" ] || return 23
    case "$*" in
        'getprop sys.boot_completed') echo "$READY" ;;
        'am get-current-user') echo "$USER_ID" ;;
        'pm list packages --user 0') [ -z "$PACKAGES" ] || printf '%s\n' "$PACKAGES" | sed 's/^/package:/' ;;
    esac
    return 0
}
sleep() { :; }'''
            assert src.count(leaf)==1
            src=src.replace(leaf,mock).replace('stamp=/proc/1/root/data/system/surface-first-boot-v1','stamp="$STAMP"')
            script=root/'helper';script.write_text(src)
            env=dict(os.environ,LOG=str(log),STAMP=str(stamp),FAIL=fail,READY=ready,USER_ID=user,PACKAGES=packages)
            result=subprocess.run(['sh',str(script)],env=env,capture_output=True,text=True)
            return result,log.read_text().splitlines(),stamp.exists(),stamp.stat().st_mode & 0o777 if stamp.exists() else None
    def test_fresh(self):
        p,c,stamp,mode=self.run_helper()
        self.assertEqual(p.returncode,0,p.stderr);self.assertTrue(stamp);self.assertEqual(mode,0o600)
        self.assertIn('settings put global device_provisioned 1',c)
        self.assertIn('settings --user 0 put secure user_setup_complete 1',c)
        self.assertEqual(sum(x.startswith('pm disable-user') for x in c),2)
        self.assertIn('am start --user 0 -a android.intent.action.MAIN -c android.intent.category.HOME',c)
        for forbidden in ['captive_portal','wifi','account','locksettings','set-home-activity']:
            self.assertNotIn(forbidden,'\n'.join(c))
    def test_absent_packages(self):
        for packages,count in [('',0),('com.google.android.setupwizard',1),('com.google.android.setupwizard.extra',0)]:
            p,c,stamp,_=self.run_helper(packages=packages)
            self.assertEqual(p.returncode,0,p.stderr);self.assertTrue(stamp)
            self.assertEqual(sum(x.startswith('pm disable-user') for x in c),count)
    def test_completed_is_untouched(self):
        p,c,stamp,_=self.run_helper(stamped=True)
        self.assertEqual(p.returncode,0);self.assertEqual(c,['getprop sys.boot_completed'])
    def test_command_failures_retry_without_stamp(self):
        for command in ['settings put global device_provisioned 1', 'settings --user 0 put secure user_setup_complete 1', 'pm list packages --user 0','pm disable-user --user 0 com.google.android.setupwizard','am start --user 0 -a android.intent.action.MAIN -c android.intent.category.HOME']:
            p,c,stamp,_=self.run_helper(fail=command)
            self.assertEqual(p.returncode,23,command);self.assertFalse(stamp)
    def test_wrong_user(self):
        p,c,stamp,_=self.run_helper(user='10')
        self.assertEqual(p.returncode,1);self.assertFalse(stamp)
        self.assertFalse(any(x.startswith('settings') for x in c))
    def test_not_ready(self):
        p,c,stamp,_=self.run_helper(ready='0')
        self.assertEqual(p.returncode,1);self.assertFalse(stamp)
        self.assertEqual(c,['getprop sys.boot_completed']*180)

if __name__=='__main__': unittest.main()
