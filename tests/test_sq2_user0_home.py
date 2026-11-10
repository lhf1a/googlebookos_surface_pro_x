"""Execute the maintenance scripts with a recorded-state command fixture.

No Android device changes: hardware resolver/rollback evidence is documented
separately. Run on Linux/WSL, which supplies sh and timeout.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LOGIN = 'com.google.android.desktop.identity.login/.LoginLauncherActivity'
NEXUS = 'com.google.android.apps.nexuslauncher/.NexusLauncherActivity'


@unittest.skipUnless(os.name == 'posix' and shutil.which('timeout'), 'Linux/WSL shell required')
class User0Home(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.state = self.root / 'state.json'
        self.state.write_text(json.dumps(dict(headless='false', user='0', home=LOGIN, component='default', commands=[])))
        stub = self.root / 'nsenter'
        stub.write_text('''#!/usr/bin/env python3
import os,sys,json,pathlib
p=pathlib.Path(os.environ['HOME_TEST_STATE']);s=json.loads(p.read_text())
assert sys.argv[1:6]==['-t','1','-m','-r','--']
args=sys.argv[6:];args[0]=pathlib.Path(args[0]).name
if args==['getprop','ro.fw.mu.headless_system_user']:print(s['headless']);sys.exit(0)
if args==['am','get-current-user']:print(s['user']);sys.exit(0)
s['commands'].append(args)
if os.environ.get('HOME_TEST_FAIL')=='1':p.write_text(json.dumps(s));sys.exit(19)
login='com.google.android.desktop.identity.login/.LoginLauncherActivity'
if args==['cmd','package','disable','--user','0',login]:s['component']='disabled'
elif args==['cmd','package','default-state','--user','0',login]:s['component']='default'
elif args[:5]==['cmd','package','set-home-activity','--user','0']:s['home']=args[5]
else:raise AssertionError(args)
p.write_text(json.dumps(s));print('fixture command applied')
''')
        stub.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ['PATH'], HOME_TEST_STATE=str(self.state))

    def run_script(self, name):
        return subprocess.run(['sh', str(ROOT / 'scripts' / name)], env=self.env, capture_output=True, text=True)

    def read(self):
        return json.loads(self.state.read_text())

    def test_baseline_apply_rollback_reapply(self):
        self.assertEqual(self.read()['home'], LOGIN)
        self.assertEqual(self.run_script('apply-sq2-user0-home.sh').returncode, 0)
        self.assertEqual((self.read()['home'], self.read()['component']), (NEXUS, 'disabled'))
        self.assertEqual(self.run_script('rollback-sq2-user0-home.sh').returncode, 0)
        self.assertEqual((self.read()['home'], self.read()['component']), (LOGIN, 'default'))
        self.assertEqual(self.run_script('apply-sq2-user0-home.sh').returncode, 0)
        self.assertEqual((self.read()['home'], self.read()['component']), (NEXUS, 'disabled'))
        self.assertEqual(len(self.read()['commands']), 6)

    def test_wrong_user_or_headless_never_changes_home(self):
        for field, value in [('user', '10'), ('headless', 'true')]:
            with self.subTest(field=field):
                before = dict(headless='false', user='0', home=LOGIN, component='default', commands=[])
                before[field] = value
                self.state.write_text(json.dumps(before))
                for script in ('apply-sq2-user0-home.sh', 'rollback-sq2-user0-home.sh'):
                    self.assertNotEqual(self.run_script(script).returncode, 0)
                    self.assertEqual(self.read(), before)

    def test_command_failure_propagates_without_setting_home(self):
        self.env['HOME_TEST_FAIL'] = '1'
        result = self.run_script('apply-sq2-user0-home.sh')
        self.assertEqual(result.returncode, 19)
        self.assertEqual(self.read()['home'], LOGIN)
        self.assertEqual(len(self.read()['commands']), 1)


if __name__ == '__main__':
    unittest.main()
