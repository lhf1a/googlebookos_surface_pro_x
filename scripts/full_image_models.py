"""Retain the original universal template's SQ1 OPP profile on Linux 6.18.

Only numeric GPU/GMU OPP entries are transferred, never 6.3.8 phandles, drivers,
or the whole old DT. Both models share Image/initramfs and retain two GRUB items.
"""
import json
from pathlib import Path
import subprocess

from disk_image import module, require

DEVICES = ('/soc@0/gpu@2c00000', '/soc@0/gmu@2c6a000')


def children(fdt, node):
    import libfdt
    try:
        child = fdt.first_subnode(node)
        while child >= 0:
            yield child
            child = fdt.next_subnode(child)
    except libfdt.FdtException as error:
        if error.err != -libfdt.NOTFOUND:
            raise


def profile(fdt, device):
    import libfdt
    node = fdt.path_offset(device)
    handle = int.from_bytes(fdt.getprop(node, 'operating-points-v2'), 'big')
    table = fdt.node_offset_by_phandle(handle)
    entries = {}
    for child in children(fdt, table):
        props = {}
        try:
            p = fdt.first_property_offset(child)
            while p >= 0:
                prop = fdt.get_property_by_offset(p)
                props[prop.name] = bytes(prop)
                p = fdt.next_property_offset(p)
        except libfdt.FdtException as error:
            if error.err != -libfdt.NOTFOUND:
                raise
        require(set(props) == {'opp-hz', 'opp-level'}, 'Unsupported reference OPP fields')
        require(len(props['opp-hz']) == 8 and len(props['opp-level']) == 4, 'Invalid OPP cells')
        entries[fdt.get_name(child)] = props
    require(entries, 'Empty OPP table')
    return table, entries


def describe(entries):
    return sorted([{'hz': int.from_bytes(v['opp-hz'], 'big'),
                    'level': int.from_bytes(v['opp-level'], 'big')}
                   for v in entries.values()], key=lambda v: v['hz'])


def sq1_dtb(current, reference):
    import libfdt
    new, old = libfdt.Fdt(current), libfdt.Fdt(reference)
    new.resize(len(current) + 4096)
    changes = {}
    for device in DEVICES:
        require(bytes(new.getprop(new.path_offset(device), 'compatible')) ==
                bytes(old.getprop(old.path_offset(device), 'compatible')),
                'Reference GPU/GMU identity does not match')
        _, desired = profile(old, device)
        table, previous = profile(new, device)
        changes[device] = {'before': describe(previous), 'after': describe(desired)}
        # Resolve offsets again after each structural edit; preserve the 6.18
        # table phandle, power domains, clocks, IOMMU and thermal configuration.
        for name in set(previous) - set(desired):
            table, _ = profile(new, device)
            new.del_node(new.subnode_offset(table, name))
        for name, props in desired.items():
            table, entries = profile(new, device)
            child = new.subnode_offset(table, name) if name in entries else new.add_subnode(table, name)
            for key, value in props.items():
                new.setprop(child, key, value)
        _, actual = profile(new, device)
        require(actual == desired, 'OPP transfer did not preserve the reference cells')
    new.pack()
    return bytes(new.as_bytearray()), changes


def model_router(pristine, directory):
    tool = module('model_router_pe', 'prepare-sq2-618-debug-entry.py')
    if directory != 'SQ2Nat':
        pristine, _ = tool.rewrite_efi(pristine, 'SQ2Nat', directory, 1, 'sq2.vulkan=qcom')
    tool.unsigned_arm64_pe(pristine)
    before = ('\\EFI\\SQ1Default\\el2launch.efi\0').encode('utf-16le')
    after = ('\\EFI\\' + directory + '\\el1launch.efi\0').encode('utf-16le')
    require(pristine.count(before) == 1 and len(after) <= len(before), 'Unknown SQ1 route')
    result = pristine.replace(before, after + b'\0' * (len(before) - len(after)))
    require(result.count(after) == 1, 'SQ1 route must select its own profile')
    return result


def install_profiles(tree, reference, pristine_router):
    tool = module('model_profiles_pe', 'prepare-sq2-618-debug-entry.py')
    report = {}
    for directory in ('SQ2Nat', 'SQ2Res'):
        bundle = tree / 'EFI' / directory
        dtb, changes = sq1_dtb((bundle / 'android-el2.dtb').read_bytes(), reference)
        launcher = (bundle / 'el2launch.efi').read_bytes()
        tool.unsigned_arm64_pe(launcher)
        before = ('\\EFI\\' + directory + '\\android-el2.dtb\0').encode('utf-16le')
        after = ('\\EFI\\' + directory + '\\android-sq1.dtb\0').encode('utf-16le')
        require(len(before) == len(after) and launcher.count(before) == 1, 'Unknown DTB constant')
        router = model_router(pristine_router, directory)
        tool.inspect_efi(router, launcher, (bundle / 'kernel-options.txt').read_text(),
                         directory, 'sq2.vulkan=qcom', router_target_count=1)
        (bundle / 'android-sq1.dtb').write_bytes(dtb)
        (bundle / 'el1launch.efi').write_bytes(launcher.replace(before, after))
        (bundle / 'start.efi').write_bytes(router)
        report[directory] = changes
    return {'model_selection': 'existing SMBIOS branches; SQ1 el1launch / SQ2 el2launch',
            'shared_image_and_initramfs': True, 'grub_entries_added': False,
            'profiles': report, 'hardware_verified': False}


def from_template(tree, template, esp_offset, pristine_router, work):
    reference = work / 'sq1-template-reference.dtb'
    require(not reference.exists(), 'Use a fresh model-reference output')
    subprocess.run(['mcopy', '-i', str(template) + '@@' + str(esp_offset),
                    '::/EFI/SQ1EL2/android-el2.dtb', str(reference)], check=True)
    report = install_profiles(tree, reference.read_bytes(), pristine_router)
    (work / 'MODEL-PROFILES.json').write_text(json.dumps(report, indent=2) + '\n')
    return report
