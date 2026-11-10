"""Install the latest user0 HOME repair without overriding custom launchers."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ANCHOR = b'exec native-handoff /newroot\n'
OLD_HOOK = b'timeout 330 user0-home-boot > /logs/user0-home.txt 2>&1 &\n'
HOOK = b'timeout 3660 user0-home-boot > /logs/user0-home.txt 2>&1 &\n'


def apply(records, bodies, new_record):
    init = bodies['init'].replace(OLD_HOOK, HOOK)
    if init.count(ANCHOR) != 1 or init.count(HOOK) > 1:
        raise ValueError('Unknown native Android handoff')
    if HOOK not in init:
        init = init.replace(ANCHOR, HOOK + ANCHOR)
    bodies['init'] = init
    for name, source in (
        ('bin/user0-home-boot', ROOT / 'probe/test2/user0-home-boot'),
        ('bin/apply-sq2-user0-home', ROOT / 'scripts/apply-sq2-user0-home.sh'),
    ):
        body = source.read_bytes().replace(b'\r\n', b'\n')
        subprocess.run(['sh', '-n'], input=body, check=True, capture_output=True)
        bodies[name] = body
        records[name] = new_record(0o100755)
