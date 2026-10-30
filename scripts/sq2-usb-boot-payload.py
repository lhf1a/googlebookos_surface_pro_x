"""Source USB + Bluetooth boot integration. No disk, network or service control."""
import hashlib

BASE = 'native-combined-initramfs.cpio.gz'
CONFIGS = {
    'storage.fstab': 'configs/sq2-usb-storage.fstab',
    'audio_policy_configuration.xml': 'configs/sq2-usb-audio-policy.xml',
    'manifest.xml': 'configs/sq2-usb-audio-manifest.xml',
}
HELPERS = ('usb-functions-stage', 'bluetooth-framework-boot', 'bluetooth-input-watch')
sha = lambda b: hashlib.sha256(b).hexdigest()


def payload(root):
    read = lambda name: (root/name).read_bytes().replace(b'\r\n', b'\n')
    files = {name: read(source) for name, source in CONFIGS.items()}
    files['SHA256SUMS'] = ''.join(sha(body)+'  '+name+'\n' for name, body in files.items()).encode()
    result = {'etc/sq2-usb/'+name: (0o100644, body) for name, body in files.items()}
    result.update({'bin/'+name: (0o100755, read('probe/test2/'+name)) for name in HELPERS})
    return result


def integrate(root, put, init):
    anchor = b'resource-monitor-start > /logs/resource-monitor.txt'
    if init.count(anchor) != 1 or b'usb-functions-stage >' in init:
        raise ValueError('USB early hook mismatch')
    for name, (mode, body) in payload(root).items():
        put(name, body, mode)
    return init.replace(anchor, b'usb-functions-stage > /logs/usb-functions-stage.txt 2>&1 || echo SQ2_USB_STAGE_FAILED_SEE_RAM_LOG\n'+anchor)


def verify(entries, root):
    expected = payload(root)
    for name, value in expected.items():
        if entries.get(name) != value:
            raise ValueError('USB/Bluetooth payload missing or differs from source: '+name)
    init = entries.get('init', (0, b''))[1]
    hook = b'usb-functions-stage > /logs/usb-functions-stage.txt'
    fstab = b'cp /etc/fstab.sq2-native /newroot/debug_ramdisk/fstab.sq2-native'
    if (init.count(hook) != 1 or fstab not in init or init.index(hook) >= init.index(fstab)
            or b'bluetooth-framework-boot >' not in init):
        raise ValueError('USB early-stage/Bluetooth post-boot wiring missing')
    bt = entries['bin/bluetooth-framework-boot'][1]
    if b'\nstart_input_watch\n' not in bt:
        raise ValueError('Bluetooth input watcher is not started')
    return {name: sha(body) for name, (_, body) in expected.items()}


def default_menu(text):
    result = []
    for line in text.splitlines():
        tokens = line.split()
        if tokens and tokens[0] == 'linux':
            flags = [t for t in tokens if t.startswith('sq2.usb=')]
            if flags and flags not in (['sq2.usb=experimental'], ['sq2.usb=0']):
                raise ValueError('Ambiguous USB flags')
            if 'sq2.rescue=1' in tokens:
                if flags == ['sq2.usb=experimental']:
                    raise ValueError('Rescue must not enable USB stage')
                flag = 'sq2.usb=0'
            else:
                flag = 'sq2.usb=0' if flags == ['sq2.usb=0'] else 'sq2.usb=experimental'
            tokens = [t for t in tokens if not t.startswith('sq2.usb=')]+[flag]
            line = ' '+ ' '.join(tokens)
        result.append(line)
    menu = ('\n'.join(result)+'\n').encode()
    validate_menu(menu)
    return menu.decode()


def validate_menu(menu):
    normal = rescue = 0
    for line in menu.decode('utf-8').splitlines():
        tokens = line.split()
        if not tokens or tokens[0] != 'linux':
            continue
        flags = [t for t in tokens if t.startswith('sq2.usb=')]
        if 'sq2.rescue=1' in tokens:
            rescue += 1
            if flags != ['sq2.usb=0']:
                raise ValueError('Rescue USB stage must be explicitly disabled')
        elif flags == ['sq2.usb=experimental']:
            normal += 1
        elif flags != ['sq2.usb=0']:
            raise ValueError('Normal entry missing USB enable/opt-out flag')
    if not normal or not rescue:
        raise ValueError('Enabled normal and safe rescue entries required')


def policy(menu, base, entries, root):
    validate_menu(menu)
    return dict(schema=1, enabled=True, menu_sha256=sha(menu),
                initramfs_sha256=sha(base), source_sha256=verify(entries, root))
