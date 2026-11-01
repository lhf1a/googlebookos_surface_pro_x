#!/usr/bin/env python3
"""Build-only one-shot SQ2 preboot diagnostic, from the private normal payload.

Does not install, select a GRUB entry, or change the normal Android payload.
Firmware and output archives must remain outside Git.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import libfdt

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--kernel-build', type=Path, required=True)
p.add_argument('--qebspil', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
dest = a.output
base = ROOT / 'out/el2-android/native-combined-initramfs.cpio.gz'
spec = importlib.util.spec_from_file_location('cpio', ROOT / 'scripts/pack-sq2-vulkan.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
before = c.entries(gzip.decompress(base.read_bytes()))
entries = {}
bins = {'bin/' + n for n in ['busybox', 'qrtr-ns', 'pd-mapper', 'tqftpserv', 'rmtfs']}
fwroot = 'lib/firmware/qcom/msft/surface/pro-x-sq2/'
pil_firmware = ['qcadsp8180.mbn', 'qccdsp8180.mbn', 'qcmpss8180.mbn']
stage = dest / 'module-stage'
release = (a.kernel_build / 'include/config/kernel.release').read_text().strip()
assert release == '6.3.8-sq2-android1'
for n, (mode, body) in before.items():
    if n.startswith('lib/modules/') and n.endswith('.ko'):
        rel = n.split('/kernel/', 1)[1]
        source = a.kernel_build / rel
        assert source.is_file(), source
        target = stage / n
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        subprocess.run(['aarch64-linux-gnu-strip', '--strip-debug', str(target)], check=True)
        entries[n] = (mode, target.read_bytes())
    elif n in bins or ((n.startswith('lib/') or n.startswith('lib64/')) and
                      not n.startswith(('lib/modules/', 'lib/firmware/'))):
        entries[n] = (mode, body)
    elif n.startswith('lib/firmware/ath10k/') or n.startswith('lib/firmware/regulatory.db'):
        entries[n] = (mode, body)
    elif n.startswith(fwroot) and (n.endswith('.jsn') or '/modem_pr/' in n or
                                 n.endswith('/wlanmdsp.mbn')):
        entries[n] = (mode, body)
for name in bins:
    assert name in entries, name
for name in pil_firmware:
    source = fwroot + name
    target = dest / 'firmware' / source.removeprefix('lib/firmware/')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(before[source][1])
moddir = stage / 'lib/modules' / release
for name in ['modules.builtin', 'modules.builtin.modinfo', 'modules.order']:
    shutil.copyfile(a.kernel_build / name, moddir / name)
subprocess.run(['depmod', '-b', str(stage), release], check=True)
for f in moddir.glob('modules.*'):
    entries[f.relative_to(stage).as_posix()] = (0o100644, f.read_bytes())
entries['init'] = (0o100755, (ROOT / 'probe/virtualization/el2-preboot-init').read_bytes().replace(b'\r\n', b'\n'))
entries['bin/kvm-arm64-witness'] = (0o100755, (ROOT / 'out/sq2-el2-test/kvm-arm64-witness').read_bytes())
for n in list(entries):
    for parent in Path(n).parents:
        if str(parent) != '.':
            entries.setdefault(parent.as_posix(), (0o40755, b''))
raw = b''.join(c.entry(n, body, mode) for n, (mode, body) in
               sorted(entries.items(), key=lambda item: (item[0].count('/'), item[0])))
raw += c.entry('TRAILER!!!', b'', 0)
raw += b'\0' * (-len(raw) % 512)
packed = gzip.compress(raw, mtime=0)
assert c.entries(gzip.decompress(packed)) == entries
(dest / 'initramfs.cpio.gz').write_bytes(packed)
shutil.copyfile(a.kernel_build / 'arch/arm64/boot/Image', dest / 'Image')
shutil.copyfile(a.qebspil, dest / 'qebspilaa64.efi')
shutil.copyfile(ROOT / 'reference/sq1-el2/windows-22621.6199/tcblaunch.exe', dest / 'tcblaunch.exe')
dt = libfdt.Fdt((ROOT / 'out/el2-android/fullram/full-el2-context-limit.dtb').read_bytes())
dt.resize(len(dt.as_bytearray()) + 2048)
for address, compatible in [('17300000', 'adsp'), ('8300000', 'cdsp'), ('4080000', 'mpss')]:
    node = dt.path_offset('/soc@0/remoteproc@' + address)
    assert bytes(dt.getprop(node, 'compatible')).rstrip(b'\0') == ('qcom,sc8180x-' + compatible + '-pas').encode()
    dt.setprop(node, 'microsoft,el2-preboot', b'')
    dt.setprop(node, 'qcom,broken-reset', b'')
    dt.setprop_u32(node, 'microsoft,el2-preboot-complete', 0)
    dt.setprop(node, 'microsoft,el2-preboot-status', b'\0' * 24)
node = dt.path_offset('/reserved-memory/memory@85500000')
dt.setprop(node, 'microsoft,el2-preboot-rmtfs', b'')
dt.setprop_u32(node, 'microsoft,el2-preboot-complete', 0)
dt.setprop(node, 'microsoft,el2-preboot-status', b'\0' * 24)
dt.setprop(0, 'microsoft,el2-kernel-fdt', b'\0' * 8)
dt.pack()
(dest / 'preboot-el2.dtb').write_bytes(bytes(dt.as_bytearray()))
source = dest / 'efi-source'
shutil.copytree(ROOT / 'upstream/slbounce/src', source / 'src')
shutil.copyfile(ROOT / 'upstream/slbounce/Makefile', source / 'Makefile')
(source / 'external').symlink_to(ROOT / 'upstream/slbounce/external', target_is_directory=True)
main = (ROOT / 'probe/virtualization/el2-launcher-main.c').read_text()
main = main.replace('SQ2EL2KVM', 'SQ2EL2Preboot').replace('minimal-el2.dtb', 'preboot-el2.dtb')
main = main.replace('maxcpus=1 ', '').replace('loglevel=7',
    'loglevel=7 ignore_loglevel initcall_debug sq2.el2_trace=1')
main = main.replace('nohibernate ', 'nohibernate lsm=selinux enforcing=0 ')
main = main.replace('EFI_HANDLE kernel;', 'EFI_HANDLE kernel, preboot;')
anchor = '    path = FileDevicePath(loaded->DeviceHandle, L"\\\\EFI\\\\SQ2EL2Preboot\\\\Image");'
assert main.count(anchor) == 1
load = r'''
    path = FileDevicePath(loaded->DeviceHandle, L"\\EFI\\SQ2EL2Preboot\\qebspilaa64.efi");
    if (!path) return stage_error(L"preboot path", EFI_OUT_OF_RESOURCES);
    ret = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image, path, NULL, 0, &preboot);
    FreePool(path);
    if (EFI_ERROR(ret)) return stage_error(L"preboot LoadImage", ret);
    ret = uefi_call_wrapper(BS->StartImage, 3, preboot, NULL, NULL);
    if (EFI_ERROR(ret)) return stage_error(L"preboot prepare", ret);
    Print(L"Preboot firmware prepared; authentication scheduled before EL2.\n");
'''
main = main.replace(anchor, load + anchor)
main = main.replace('SQ2 isolated EL2 KVM test. No disks or Android.',
                    'SQ2 EL2 preboot diagnostic. RAM only; known ESP logs; automatic normal reboot.')
original = (source / 'src/bounce_main.c').read_text()
# No EFI console calls after ExitBootServices. A small GOP hex overlay shows
# whether the last visible PAS message precedes EBS, AUTH, LAUNCH or EL2 return.
original = original.replace('static struct sl_smc_params *smc_data;',
    '#include "el2-progress-fb.h"\n\nstatic struct sl_smc_params *smc_data;')
# The original sweep precedes EBS. qebspil then dirties loader/firmware state
# in its late EBS callback. Secure Launch destroys caches, so publish this
# state again BEFORE AUTH transfers ownership of any Secure Launch memory.
# Reuse exactly the original descriptor allowlist; never sweep reserved,
# conventional, MMIO or DSP-owned ranges indiscriminately.
loop_start = original.index('\tEFI_MEMORY_DESCRIPTOR *MemoryEntry;')
loop_end = original.index('\n\tEFI_STATUS status = uefi_call_wrapper(real_ExitBootServices', loop_start)
cache_sweep = original[loop_start:loop_end]
original = original[:loop_start] + '\tsq2_flush_loader_memory();\n' + original[loop_end:]
entry = original.index('EFI_STATUS sl_ExitBootServices(')
original = original[:entry] + 'static void sq2_flush_loader_memory(void)\n{\n' + cache_sweep + '\n}\n\n' + original[entry:]
auth = '\tsmcret = sl_smc(smc_data, SL_CMD_AUTH,'
assert original.count(auth) == 1
original = original.replace(auth,
    '\tsq2_progress_mark(0x12, 0);\n'
    '\tsq2_flush_loader_memory();\n'
    '\tsq2_progress_mark(0x13, 0);\n' + auth)
original = original.replace('\tEFI_STATUS status = uefi_call_wrapper(real_ExitBootServices, 2, ImageHandle, MapKey);',
    '\tsq2_progress_mark(0x10, 0);\n'
    '\tEFI_STATUS status = uefi_call_wrapper(real_ExitBootServices, 2, ImageHandle, MapKey);\n'
    '\tsq2_progress_mark(0x11, status);')
original = original.replace('\tsmcret = sl_smc(smc_data, SL_CMD_AUTH,',
    '\tsq2_progress_mark(0x20, 0);\n\tsmcret = sl_smc(smc_data, SL_CMD_AUTH,')
original = original.replace('SL_CMD_AUTH, pe_data, pe_size, arg_data, arg_size);',
    'SL_CMD_AUTH, pe_data, pe_size, arg_data, arg_size);\n\tsq2_progress_mark(0x21, smcret);')
original = original.replace('\t\tsmcret = sl_smc(smc_data, SL_CMD_LAUNCH,',
    '\t\tsq2_progress_mark(0x30, 0);\n\t\tsmcret = sl_smc(smc_data, SL_CMD_LAUNCH,')
original = original.replace('\n\treturn status;\n}\n\nEFI_STATUS sl_install',
    '\n\tsq2_progress_mark(0x31, read_currentel().el);\n\treturn status;\n}\n\nEFI_STATUS sl_install')
main = main.replace('    InitializeLib(image, st);',
    '    InitializeLib(image, st);\n    sq2_progress_init();')
shutil.copyfile(ROOT / 'probe/virtualization/el2-progress-fb.h', source / 'src/el2-progress-fb.h')
(source / 'src/bounce_main.c').write_text(original[:original.index('EFI_STATUS efi_main(')] + main)
with (dest / 'launcher-build.log').open('w') as log:
    subprocess.run(['make', '-s', '-C', str(source), '-j8'], check=True, stdout=log, stderr=subprocess.STDOUT)
shutil.copyfile(source / 'out/slbounce.efi', dest / 'el2launch.efi')
files = ['Image', 'initramfs.cpio.gz', 'preboot-el2.dtb', 'qebspilaa64.efi', 'tcblaunch.exe', 'el2launch.efi']
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
(dest / 'SHA256SUMS').write_text(''.join(f'{digest(dest / n)}  {n}\n' for n in files))
(dest / 'firmware/SHA256SUMS').write_text(''.join(
    f'{digest(dest / "firmware" / (fwroot + n).removeprefix("lib/firmware/"))}  {(fwroot + n).removeprefix("lib/firmware/")}\n'
    for n in pil_firmware))
(dest / 'audit.json').write_text(json.dumps({'base_sha256': digest(base),
    'initramfs_bytes': len(packed), 'modules': sum(n.endswith('.ko') for n in entries),
    'android_or_credentials_included': False, 'hardware_tested': False}, indent=2) + '\n')
print(dest, len(packed), 'initramfs bytes; BUILD ONLY')
