#!/usr/bin/env python3
"""Build SQ1/SQ2 profiles from one pinned, accepted EL2 runtime (offline only).

No device enumeration, installation, boot, firmware execution or test runner.
--rebuild-components retains the mainline component-rebuild backend unchanged.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import runpy
import shutil
import struct
import sys

R = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_cpio():
    spec = importlib.util.spec_from_file_location('surface_cpio', R/'scripts/pack-sq2-vulkan.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def credential_path(name):
    return (name in ('etc/shadow', 'etc/passwd', 'etc/group', 'bin/dropbear',
                     'bin/dropbearkey', 'etc/dropbear', 'root/.ssh') or
            name.startswith(('etc/dropbear/', 'root/.ssh/')))


def storage_path(name):
    return (name in ('etc/sq2-ssd-layout', 'etc/sq2-ssd-partitions',
                     'etc/sq2-ssd-super-slices', 'bin/prepare-native-ssd',
                     'bin/sq1-grow-userdata', 'etc/sq1-grow') or
            name.startswith('etc/sq1-grow/'))


def credential_member(raw, c):
    entries = c.entries(gzip.decompress(raw))
    require('etc/shadow' in entries and 'bin/dropbear' in entries, 'Missing private identity')
    # Some SQ1 builders flattened the addon. Extract only effective identity
    # entries, not its old kernel/runtime. Ancestor directories already exist in
    # the first member; do not override their original permissions here.
    identity = {n: v for n, v in entries.items() if credential_path(n)}
    archive = b''.join(c.entry(n, body, mode) for n, (mode, body) in
                       sorted(identity.items(), key=lambda item: (item[0].count('/'), item[0])))
    archive += c.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    return gzip.compress(archive, compresslevel=3, mtime=0)


def payload(raw, machine, donor, c, profile):
    before = c.entries(gzip.decompress(raw))
    if machine == 'sq2':
        return raw, {'changed': [], 'common_runtime_preserved': True,
                     'credentials': 'original SQ2 bytes unchanged'}
    other = c.entries(gzip.decompress(donor))
    e = dict(before)
    calibration = e['bin/touch-calibration-boot']
    old_sensor = b"grep -q '^HID_ID=001C:0000045E:00000C21$'"
    require(calibration[1].count(old_sensor) == 1, 'Unexpected calibration helper source')
    e['bin/touch-calibration-boot'] = (calibration[0], calibration[1].replace(
        old_sensor, b"grep -qE '^HID_ID=001C:0000045E:0000(0C21|0996)$'"))
    for name in set(e) | set(other):
        if storage_path(name) or credential_path(name):
            if name in other:
                e[name] = other[name]
            else:
                e.pop(name, None)
    layout = e['etc/sq2-ssd-layout'][1]
    require(b'SSD_ESP_UUID=' not in layout, 'Unexpected SQ1 donor layout')
    e['etc/sq2-ssd-layout'] = (e['etc/sq2-ssd-layout'][0],
                              layout + ('SSD_ESP_UUID='+profile['esp_uuid']+'\n').encode())
    # User 10 is confirmed only on the existing SQ2. Never create/clear/copy an account.
    init = e['init'][1]
    anchor = b'el2-terminal-autostart 10 >'
    require(init.count(anchor) == 1, 'Unexpected normal-login hook')
    e['init'] = (e['init'][0], init.replace(anchor, b'el2-terminal-autostart >'))
    changed = sorted(n for n in set(e) | set(before) if e.get(n) != before.get(n))
    require(all(storage_path(n) or credential_path(n) or n in ('init', 'bin/touch-calibration-boot') for n in changed),
            'A shared runtime component changed')
    for name in ('bin/usb-input-watch', 'bin/usb-network-watch', 'bin/audio-drivers-start',
                 'bin/el2-terminal-stage', 'bin/el2-terminal-autostart',
                 'bin/video-decoder-lab-start', 'etc/sq2-audio/audio-hal'):
        require(e[name] == before[name], 'Capability regression: '+name)
    # Rebuild rather than append over SQ2 credentials: no hidden old identity remains.
    archive = b''.join(c.entry(n, body, mode) for n, (mode, body) in
                       sorted(e.items(), key=lambda item: (item[0].count('/'), item[0])))
    archive += c.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    result = gzip.compress(archive, compresslevel=3, mtime=0) + credential_member(donor, c)
    require(c.entries(gzip.decompress(result)) == e, 'Last-entry-wins mismatch')
    return result, {'changed': changed, 'common_runtime_preserved': True,
                    'credentials': 'SQ1 effective identity bytes only; credential-only member last',
                    'normal_autologin': 'no account selected; AVF preparation preserved',
                    'auto_grow': 'existing pinned SQ1 image implementation retained, not executed'}


def snapshot(f):
    result = {}
    node, depth = -1, 0
    while True:
        node, depth = f.next_node(node, depth)
        if node < 0 or depth < 0:
            break
        props = {}
        p = f.first_property_offset(node, quiet=(1,))
        while p >= 0:
            prop = f.get_property_by_offset(p)
            props[prop.name] = bytes(prop)
            p = f.next_property_offset(p, quiet=(1,))
        result[f.get_path(node)] = props
    return result


def device_tree(raw, machine, profile):
    if machine == 'sq2' and (profile['gpu_mhz'], profile['rpmh_level'], profile['cpu_big_max_khz']) == (700, 432, 3148800):
        return raw, []
    import libfdt
    f = libfdt.Fdt(raw)
    before = snapshot(f)
    reserves = [f.get_mem_rsv(i) for i in range(f.num_mem_rsv())]
    f.resize(len(raw)+1024)
    model = ('Microsoft Surface Pro X ('+machine.upper()+')\0').encode()
    f.setprop(0, 'model', model)
    gpu = '/soc@0/gpu@2c00000/opp-table'
    gpu_hz = profile['gpu_mhz'] * 1000000
    cpu_hz = profile['cpu_big_max_khz'] * 1000
    selected = gpu+'/opp-'+str(gpu_hz)
    require(selected in before, 'Custom GPU choice requires an existing OPP or a custom DT input')
    removed = [path for path, props in before.items() if 'opp-hz' in props and
               ((path.startswith(gpu+'/') and int.from_bytes(props['opp-hz'], 'big') > gpu_hz) or
                (path.startswith('/opp-table-cpu4/') and int.from_bytes(props['opp-hz'], 'big') > cpu_hz))]
    for path in removed:
        require(path in before, 'Missing pinned OPP '+path)
        f.del_node(f.path_offset(path))
    # SQ1's earlier 661 profile used TURBO=416; do not inherit SQ2's 700/432 vote.
    f.setprop(f.path_offset(selected), 'opp-level', struct.pack('>I', profile['rpmh_level']))
    f.pack()
    expected = {p: dict(v) for p, v in before.items() if p not in removed}
    expected['/']['model'] = model
    expected[selected]['opp-level'] = struct.pack('>I', profile['rpmh_level'])
    require(snapshot(f) == expected, 'Unrelated DT change')
    require(reserves == [f.get_mem_rsv(i) for i in range(f.num_mem_rsv())],
            'Reserved memory changed')
    return bytes(f.as_bytearray()), ['model', 'GPU max'+str(profile['gpu_mhz']), 'CPU big max'+str(profile['cpu_big_max_khz'])]


def launcher(raw, machine, entry):
    if machine == 'sq2':
        return raw
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    require(raw[pe:pe+4] == b'PE\0\0', 'Not PE')
    require(struct.unpack_from('<H', raw, pe+4)[0] == 0xaa64, 'Not ARM64')
    opt = pe+24
    require(struct.unpack_from('<H', raw, opt)[0] == 0x20b, 'Not PE32+')
    require(struct.unpack_from('<II', raw, opt+112+8*4) == (0, 0),
            'Never edit a signed launcher')
    old, new = ('SQ2'+entry).encode('utf-16le'), ('SQ1'+entry).encode('utf-16le')
    require(raw.count(old) > 0 and len(old) == len(new), 'Missing directory tokens')
    result = raw.replace(old, new)
    require(old not in result and len(result) == len(raw), 'Launcher path edit failed')
    # Existing GNU EFI outputs carry no checksum; preserve the same policy.
    require(struct.unpack_from('<I', result, opt+64)[0] == 0, 'Unexpected PE checksum')
    return result


def menu(machine, profile):
    label = machine.upper()
    return ("insmod part_gpt\ninsmod fat\ninsmod chain\nset timeout=10\n"
            f"set default={machine}-default\n" + ''.join(
                f"menuentry '{label} {title} - {graphics} / 60Hz / GPU{profile['gpu_mhz']}' --id {machine}-{role} {{\n"
                f" search --no-floppy --fs-uuid --set=root {profile['esp_uuid']}\n"
                f" chainloader /EFI/{label}{directory}/el2launch.efi\n}}\n"
                for title, graphics, role, directory in (
                    ('Android + Linux', 'Qualcomm', 'default', 'Default'),
                    ('working fallback' if machine == 'sq2' else 'fallback (candidate)',
                     'Mesa', 'fallback', 'Fallback'))))


def main():
    if '--rebuild-components' in sys.argv:
        sys.argv.remove('--rebuild-components')
        require(not any(x.startswith(('--machine', '--sealed-input')) for x in sys.argv[1:]),
                'Component rebuild produces the SQ2 common runtime; profile it separately')
        runpy.run_path(str(R/'scripts/build-sq2-default.py'), run_name='__main__')
        return
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--machine', choices=('sq1', 'sq2', 'both'), default='sq2')
    p.add_argument('--sealed-input', type=Path, default=R/'private/unified-input')
    p.add_argument('--output', type=Path, default=R/'out/unified-boot')
    p.add_argument('--profiles', type=Path, default=R/'configs/surface-pro-x-profiles.json',
                   help='custom model settings; defaults are not mandatory capability gates')
    p.add_argument('--touch-repair', choices=('on','off'), default='on', help='generic touch repair baseline; off explicitly retains legacy runtime')
    p.add_argument('--dpi', type=int, default=300, help='default Android UI density; preserve saved user overrides')
    p.add_argument('--ksu-manager', choices=('on', 'off'), default='on', help='install pinned KernelSU Next userspace; no kernel replacement')
    p.add_argument('--ksu-bundle', type=Path, default=R/'out/ksunext')
    p.add_argument('--touch-bundle', type=Path, default=R/'out/touch-baseline', help='compiled merged calibration/noise bundle; strict source/hash checks')
    p.add_argument('--lte', choices=('on', 'off'), default='on',
                   help='baseline LTE/Radio enabled by default; off keeps the original payload')
    p.add_argument('--lte-bundle', type=Path, default=R/'private/lte-baseline',
                   help='private, exact reboot-verified Radio04 initramfs and IPA DT')
    p.add_argument('--suspend-policy', choices=('android','screen-only'), default='android')
    p.add_argument('--power-bundle', type=Path, default=R/'private/build-inputs/power')
    args = p.parse_args()
    if not 72 <= args.dpi <= 1000:
        p.error('--dpi must be between 72 and 1000')
    spec=importlib.util.spec_from_file_location('surface_defaults',R/'scripts/surface-defaults-payload.py')
    defaults=importlib.util.module_from_spec(spec);spec.loader.exec_module(defaults)
    if args.ksu_manager == 'on':
        for name in defaults.FILES:
            path=args.ksu_bundle/name
            require(path.is_file() and path.stat().st_size > 0, 'KSU input missing or empty: '+name)
    if args.touch_repair == 'on' and not (args.touch_bundle/'touch-bundle.json').is_file():
        p.error('Build the current generic touch bundle with build-unified-touch.py, pass --touch-bundle, or explicitly select --touch-repair off')
    pins = json.loads((R/'configs/surface-pro-x-unified-inputs.json').read_text())
    profiles = json.loads(args.profiles.read_text())
    machines = ('sq1', 'sq2') if args.machine == 'both' else (args.machine,)
    needed = {n: h for n, h in pins['files'].items() if not n.startswith('sq1/') or 'sq1' in machines}
    for name, expected in needed.items():
        path = args.sealed_input/name
        require(path.is_file() and path.stat().st_size > 0, 'Input hash mismatch: '+name)
    require(not args.output.exists(), 'Use a fresh output directory')
    c = load_cpio()
    raw = (args.sealed_input/'common/initramfs.cpio.gz').read_bytes()
    base_entries = c.entries(gzip.decompress(raw))
    # Required verified baseline, including all USB modules, not an older stripped SQ1 image.
    require('bin/usb-input-watch' in base_entries and 'bin/usb-network-watch' in base_entries,
            'USB baseline missing')
    lte = None
    if args.lte == 'on':
        spec = importlib.util.spec_from_file_location('lte_payload', R/'scripts/surface-lte-payload.py')
        lte = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(lte)
        lte_delta, lte_dt, lte_source = lte.prepare(raw, R, args.lte_bundle, c)
    spec=importlib.util.spec_from_file_location('surface_power',R/'scripts/surface-power-payload.py')
    power=importlib.util.module_from_spec(spec);spec.loader.exec_module(power)
    power_files=power.bundle_files(args.power_bundle) if args.suspend_policy=='android' else None
    prepared = {}
    for machine in machines:
        profile = profiles[machine]
        require(profile['esp_uuid'] != profiles['sq1']['windows_esp_must_not_be_targeted'],
                'The inspected SQ1 Windows ESP is not an Android target')
        donor = (args.sealed_input/'sq1/profile-donor.cpio.gz').read_bytes() if machine == 'sq1' else None
        packed, audit = payload(raw, machine, donor, c, profile)
        if args.touch_repair == 'on':
            spec=importlib.util.spec_from_file_location('touch_payload',R/'scripts/surface-touch-payload.py')
            touch=importlib.util.module_from_spec(spec);spec.loader.exec_module(touch)
            packed,audit['touch_bundle']=touch.integrate(packed,R,args.touch_bundle,c)
        packed,audit['defaults']=defaults.integrate(packed,R,args.ksu_bundle,c,args.dpi,args.ksu_manager == 'on')
        original_dt = (args.sealed_input/'common/android-el2.dtb').read_bytes()
        fallback_dt, changes = device_tree(original_dt, machine, profile)
        runtimes = {'Default': packed, 'Fallback': packed}
        dts = {'Default': fallback_dt, 'Fallback': fallback_dt}
        audit['lte'] = {'enabled': False, 'fallback_unchanged': True}
        if lte:
            runtimes['Default'], lte_audit = lte.integrate(packed, lte_delta, c)
            # SQ1/SQ2 share the LTE/IPA platform; retain each model's OPP caps.
            dts['Default'], _ = device_tree(lte_dt, machine, profile)
            audit['lte'] = dict(lte_source, **lte_audit, enabled=True, fallback_unchanged=True)
        launchers = {entry: launcher((args.sealed_input/'launchers'/name).read_bytes(), machine, entry)
                     for entry, name in [('Default', 'closed.efi'), ('Fallback', 'mesa.efi')]}
        if power_files:
            runtimes['Default'],audit['power']=power.integrate(runtimes['Default'],R,args.power_bundle,c)
            launchers['Default']=power.launcher(args.power_bundle,machine)
        else:
            audit['power']={'policy':'screen-only'}
        prepared[machine] = (runtimes, dts, launchers, audit, changes)
    args.output.mkdir(parents=True)
    report = {'schema': 1, 'main_commit': pins['upstream_main'], 'hardware_tested': False,
              'device_writes': False, 'kernel_recompiled': False, 'profiles': {}}
    for machine, (runtimes, dts, launchers, audit, changes) in prepared.items():
        root = args.output/machine.upper()
        root.mkdir()
        hashes = {}
        # Copy only explicitly pinned firmware, not arbitrary cache additions.
        for name in needed:
            if name.startswith('firmware/'):
                dest = root/name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(args.sealed_input/name, dest)
        for entry, code in launchers.items():
            dest = root/'EFI'/(machine.upper()+entry)
            dest.mkdir(parents=True)
            for name in ('Image', 'qebspilaa64.efi', 'tcblaunch.exe'):
                shutil.copyfile(args.sealed_input/'common'/name, dest/name)
            (dest/'android-el2.dtb').write_bytes(dts[entry])
            if entry=='Default' and power_files:
                (dest/'Image').write_bytes(power_files['Image'])
            (dest/'initramfs.cpio.gz').write_bytes(runtimes[entry])
            (dest/'el2launch.efi').write_bytes(code)
            rows = {f.name: sha(f.read_bytes()) for f in sorted(dest.iterdir())}
            (dest/'SHA256SUMS').write_text(''.join(h+'  '+n+'\n' for n, h in rows.items()))
            hashes[entry] = rows
        (root/'grub.cfg').write_text(menu(machine, profiles[machine]))
        audit.update(profile=profiles[machine], entries=hashes, dt_changes=changes,
                     common_input_sha256=sha(raw), hardware_validated=False,
                     firmware_policy='same pinned Linux firmware; not Windows raw substitutions')
        (root/'build-audit.json').write_text(json.dumps(audit, indent=2)+'\n')
        report['profiles'][machine] = audit
    (args.output/'build-audit.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Built offline profiles:', ', '.join(machines))
    print('OUTPUT:', args.output.resolve())
    print('Hardware tests: NOT RUN; deployment: NONE; kernel/modules: pinned reuse')


if __name__ == '__main__':
    main()
