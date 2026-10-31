"""Exercise real bridge startup against a mocked kernel socket boundary."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StartupTests(unittest.TestCase):
    def test_bridge_waits_without_closing_controller(self):
        source = (ROOT/'probe/test2/sq2-hci-bridge.c').read_text()
        # Rename only the calls; use libc declarations and compatible wrappers.
        point = source.index('int main(')
        wrappers = r'''
static int calls;
static int fake_socket(int a, int b, int c) {(void)a;(void)b;(void)c;return 3;}
static int fake_ioctl(int fd, unsigned long op, ...) {
    (void)fd; (void)op; _exit(91); /* A forced close is forbidden. */
}
static int fake_bind(int fd, const struct sockaddr *a, socklen_t n) {
    (void)fd; (void)a; (void)n;
    if (getenv("BUSY_FOREVER")) { errno=EBUSY;return -1; }
    if (getenv("OTHER_OWNER")) { errno=EUSERS;return -1; }
    if (++calls<4) { errno=EBUSY;return -1; }
    return 0;
}
static unsigned fake_sleep(unsigned n) {(void)n;return 0;}
static int fake_pty(int flags) {(void)flags;_exit(calls==4?0:92);}
#define socket fake_socket
#define ioctl fake_ioctl
#define bind fake_bind
#define sleep fake_sleep
#define posix_openpt fake_pty
'''
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p/'test.c').write_text(source[:point]+wrappers+source[point:])
            subprocess.run(['gcc','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
            args=[str(p/'test'),'0','/dev/unused']
            self.assertEqual(subprocess.run(args,capture_output=True).returncode,0)
            for env, expected in [({'BUSY_FOREVER':'1'}, 'Device or resource busy'),
                                  ({'OTHER_OWNER':'1'}, 'Too many users')]:
                result=subprocess.run(args,env=env,capture_output=True,text=True)
                self.assertEqual(result.returncode,1)
                self.assertIn(expected,result.stderr)

    def test_helper_reuses_late_bridge_and_bounds_failure(self):
        source=(ROOT/'probe/test2/bluetooth-framework-boot').read_text()
        function=source[source.index('publish_transport() {'):source.index('# Do not bring an already-owned controller')]
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)
            function=function.replace('/dev/sq2-bt-h4',str(p/'transport'))
            for scenario in ('late','dead','timeout'):
                (p/'transport').unlink(missing_ok=True)
                pre='''naps=0
bridge=123
seq() { printf '1\\n2\\n3\\n'; }
android() { echo "PUBLISH:$*"; }
'''
                pre += 'kill() { return '+('1' if scenario=='dead' else '0')+'; }\n'
                pre += 'sleep() { naps=$((naps+1)); '
                pre += (f'[ "$naps" != 2 ] || ln -s /dev/null {p}/transport; ' if scenario=='late' else '')
                pre += 'return 0; }\n'
                result=subprocess.run(['sh'],input=pre+function+'\npublish_transport\n',capture_output=True,text=True)
                self.assertEqual(result.returncode,0 if scenario=='late' else 1)
                self.assertEqual('PUBLISH:setprop vendor.sq2.bluetooth.ready 1' in result.stdout,scenario=='late')
                if scenario!='late': self.assertIn('EXITED' if scenario=='dead' else 'TIMEOUT',result.stdout)


if __name__ == '__main__':
    unittest.main()
