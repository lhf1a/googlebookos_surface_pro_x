"""Remove inherited identity and install generic first-boot setup skipping."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SETUP_HOOK = b'timeout 600 surface-first-boot > /logs/first-boot.txt 2>&1 &\n'


def apply(records, bodies, new_record):
    removed = []
    secrets = []
    for name in list(bodies):
        private = (name.startswith(('root/.ssh/', 'etc/dropbear/')) or
                   name in ('etc/sq2-android-wifi.conf', 'etc/machine-id',
                            'bin/lab-local-setup', 'bin/skip-google-setup',
                            'etc/googlebook-userdata.sparse.gz', 'etc/googlebook-bootmeta.sparse.gz'))
        if private:
            if name.endswith(('authorized_keys', '_host_key')):
                secrets.append(bodies[name])
            if name == 'etc/sq2-android-wifi.conf':
                secrets.extend(x for x in bodies[name].splitlines() if len(x) >= 4)
            removed.append(name)
            bodies.pop(name)
            records.pop(name, None)
    old_shadow = bodies.get('etc/shadow', b'')
    secrets.extend(x.split(b':')[1] for x in old_shadow.splitlines()
                   if b':' in x and len(x.split(b':')[1]) > 4)
    secrets.extend(re.findall(rb'(?:ssid|psk)\s*=\s*"([^"\n]+)"', bodies.get('etc/wpa_supplicant.conf', b'')))
    bodies['etc/shadow'] = b'root:!:0:0:99999:7:::\n'
    records['etc/shadow'] = new_record(0o100600)
    bodies['etc/wpa_supplicant.conf'] = b'ctrl_interface=/run/wpa_supplicant\nupdate_config=0\n'
    records['etc/wpa_supplicant.conf'] = new_record(0o100600)
    bodies['bin/start-ssh'] = b'#!/bin/busybox sh\necho "SSH disabled: configure credentials locally first" >&2\nexit 1\n'
    records['bin/start-ssh'] = new_record(0o100755)
    init = bodies['init']
    # Remove the complete simple startup blocks, not arbitrary Android setup code.
    for helper in (b'lab-local-setup', b'ssh-framework-boot'):
        pattern = (rb'(?m)^    if [^\n]+; then\n'
                   rb'        ' + helper + rb'[^\n]*\n    fi\n')
        init = re.sub(pattern, b'', init)
    if b'lab-local-setup >' in init or b'ssh-framework-boot >' in init:
        raise ValueError('Unknown personal startup hook')
    bodies['init'] = init
    # Do not start a diagnostic network supervisor without locally supplied config.
    for name in ('bin/wifi-framework-boot', 'bin/wifi-framework-handoff'):
        if name in bodies:
            head, tail = bodies[name].split(b'\n', 1)
            bodies[name] = head + b'\n[ -s /etc/sq2-android-wifi.conf ] || exit 0\n' + tail
    for name, body in bodies.items():
        if any(s and s in body for s in secrets):
            raise ValueError('Inherited credential remains in ' + name)
    install_first_boot(records, bodies, new_record)
    return dict(removed=sorted(removed), ssh_enabled=False, root_password_locked=True,
                first_setup_preserved=False, first_setup_skipped=True,
                credential_patterns_checked=len(secrets))


def install_first_boot(records, bodies, new_record):
    # Never restore the old LAN-specific helpers: they also changed network
    # probing policy. This generic helper only skips OOBE for the fresh user 0.
    anchor = b'exec native-handoff /newroot\n'
    if bodies['init'].count(anchor) != 1 or bodies['init'].count(SETUP_HOOK) > 1:
        raise ValueError('Unknown Android handoff for first-boot setup policy')
    if SETUP_HOOK not in bodies['init']:
        bodies['init'] = bodies['init'].replace(anchor, SETUP_HOOK+anchor)
    bodies['bin/surface-first-boot'] = (ROOT/'probe/test2/surface-first-boot').read_bytes().replace(b'\r\n',b'\n')
    records['bin/surface-first-boot'] = new_record(0o100755)
