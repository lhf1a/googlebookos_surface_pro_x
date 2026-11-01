"""Run the real helper with isolated Android, device and timing fixtures."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'probe/virtualization/el2-terminal-autostart').read_text()
STUBS = r'''
sleep() { :; }
seq() { command seq 1 3; }
timeout() { shift; "$@"; }
nsenter() {
    shift 5
    case "$1 $2" in
        '/system/bin/getprop sys.boot_completed')
            if [ "$MODE" = boot ]; then echo 0; else echo 1; fi ;;
        '/system/bin/cmd activity')
            if [ "$3" = get-current-user ]; then
                if [ "$MODE" = invalid ]; then echo bad-user; return; fi
                if [ "$MODE" = switch ] && [ ! -e "$FIXTURE/switched" ]; then
                    touch "$FIXTURE/switched"; echo 0
                else echo 10; fi
            elif [ "$3" = start-user ] || [ "$3" = switch-user ]; then
                printf '%s\n' "$*" >> "$FIXTURE/user_actions"
                [ "$MODE" != user_start_fail ]
            else
                printf '%s\n' "$*" >> "$FIXTURE/launches"
                [ "$MODE" != launch_fail ]
            fi ;;
        '/system/bin/cmd user')
            [ "$MODE" = missing_user ] || echo 'UserInfo{10:null:412}' ;;
        '/system/bin/cmd lock_settings')
            if [ "$MODE" = secure ]; then echo true; else echo false; fi ;;
        *) return 99 ;;
    esac
}
'''


class AutostartTest(unittest.TestCase):
    def run_case(self, mode):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            cmdline = 'sq2.el2=experimental'
            if mode == 'normal': cmdline = ''
            if mode in ('default', 'default_user'): cmdline = 'sq2.el2=1'
            if mode == 'rescue': cmdline += ' sq2.rescue=1'
            (d / 'cmdline').write_text(cmdline)
            (d / 'guard').write_text('N' if mode == 'guard' else 'Y')
            (d / 'stage').write_text('' if mode == 'stage' else 'SQ2_EL2_TERMINAL_STAGE_READY')
            if mode != 'kvm': (d / 'kvm').touch()
            image = d / 'users/10/com.android.virtualization.terminal/files/linux/completed'
            if mode not in ('image', 'default_user'):
                image.parent.mkdir(parents=True)
                image.touch()
            code = SOURCE
            for original, target in [('/proc/cmdline', d / 'cmdline'),
                                     ('/sys/module/suspend/parameters/screen_only', d / 'guard'),
                                     ('/logs/el2-terminal.txt', d / 'stage'),
                                     ('/proc/1/root/data/user', d / 'users')]:
                code = code.replace(original, str(target))
            code = code.replace('test -c /dev/kvm', 'test -e ' + str(d / 'kvm'))
            explicit = mode in ('passwordless', 'default_user', 'secure', 'missing_user', 'user_start_fail', 'bad_target')
            arguments = ['bad'] if mode == 'bad_target' else ['10'] if explicit else []
            result = subprocess.run(['sh', '-c', STUBS + code, 'helper', *arguments],
                                    env=dict(os.environ, FIXTURE=tmp, MODE=mode),
                                    capture_output=True, text=True, timeout=5)
            calls = (d / 'launches').read_text().splitlines() if (d / 'launches').exists() else []
            if mode in ('ready', 'switch', 'launch_fail', 'passwordless'):
                self.assertEqual(calls, ['/system/bin/cmd activity start-activity -W --user 10 -n '
                                        'com.android.virtualization.terminal/.new2.ui.MainActivity'])
            else:
                self.assertEqual(calls, [])
            actions = (d / 'user_actions').read_text().splitlines() if (d / 'user_actions').exists() else []
            expected = ['/system/bin/cmd activity start-user -w 10', '/system/bin/cmd activity switch-user 10']
            self.assertEqual(actions, expected if mode in ('passwordless', 'default_user') else expected[:1] if mode == 'user_start_fail' else [])
            success = mode in ('ready', 'default', 'default_user', 'switch', 'normal', 'rescue', 'passwordless')
            self.assertEqual(result.returncode == 0, success, result.stderr)
            self.assertEqual('LAUNCH_REQUESTED_NOT_SHELL_PROOF' in result.stdout,
                             mode in ('ready', 'switch', 'passwordless'))
            if mode in ('default', 'default_user'):
                self.assertNotIn('WAIT_FOR_ACTIVE_USER_IMAGE', result.stdout)
            self.assertFalse((d / 'users/0').exists())

    def test_guards_waits_and_launch(self):
        for mode in ('normal', 'rescue', 'guard', 'kvm', 'boot', 'stage',
                     'image', 'invalid', 'ready', 'default', 'default_user', 'switch', 'launch_fail',
                     'passwordless', 'secure', 'missing_user', 'user_start_fail', 'bad_target'):
            with self.subTest(mode=mode): self.run_case(mode)


if __name__ == '__main__':
    unittest.main()
