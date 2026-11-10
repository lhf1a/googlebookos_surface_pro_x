#!/usr/bin/env python3
"""Produce a small shared-SQ1/SQ2 EFI path repair, never write a device/image.

No kernel, DT, modules, firmware, initramfs, SMBIOS detection or LTE logic changes.
Input is the existing full-image build; output contains only the two routers and
their corresponding file lists. The original build is retained untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from disk_image import shared_model_router, module, require


def build(source, output):
    require(not output.exists(),'Output must be fresh')
    tree=source/'disk/work/esp-tree'
    output.mkdir(parents=True)
    edits=[]
    inspect=module('router_repair_pe','prepare-sq2-618-debug-entry.py')
    for directory in ('SQ2Nat','SQ2Res'):
        relative=Path('EFI')/directory
        old=(tree/relative/'start.efi').read_bytes()
        new=shared_model_router(old,directory)
        target=output/relative;target.mkdir(parents=True)
        (target/'start.efi').write_bytes(new)
        sums=[]
        for line in (tree/relative/'SHA256SUMS').read_text().splitlines():
            digest,name=line.split(None,1)
            if name.lstrip('*')=='start.efi':
                digest=hashlib.sha256(new).hexdigest()
            sums.append(digest+'  '+name)
        (target/'SHA256SUMS').write_text('\n'.join(sums)+'\n')
        edits.append({'file':str(relative/'start.efi'),
            'before_targets':[s for _,_,s in inspect.utf16_strings(old) if s.endswith('\\el2launch.efi')],
            'after_targets':[s for _,_,s in inspect.utf16_strings(new) if s.endswith('\\el2launch.efi')],
            'changed_byte_count':sum(a!=b for a,b in zip(old,new)),
            'parent_sha256':hashlib.sha256(old).hexdigest(),'sha256':hashlib.sha256(new).hexdigest(),
            'same_file_size':len(old)==len(new)})
    text=('共享载荷启动路径修复：仅将本目录 EFI 下的四个文件覆盖到启动盘 ESP 的同名路径。\n'
          '不要格式化，不用重刷整盘，不清除 userdata。保留其他文件。\n'
          '保留 SQ1/SQ2 识别与本机 LTE 数据处理，仅修复两个路由器的旧 SQ1Default 路径。\n'
          '内核、DT、模块、initramfs 和固件均不变；尚未真机复测。\n')
    (output/'README.txt').write_text(text,encoding='utf-8')
    report={'operation':'shared-model-efi-path-repair','changes':edits,'source_image_modified':False,
            'kernel_rebuilt':False,'hardware_boot_tested':False,'device_written':False,
            'model_detection_changed':False,'signed_binaries_changed':False}
    (output/'REPAIR.json').write_text(json.dumps(report,indent=2)+'\n')
    archive=output/'shared-sq1-sq2-efi-path-fix.zip'
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
        for name in ['EFI/SQ2Nat/start.efi','EFI/SQ2Nat/SHA256SUMS','EFI/SQ2Res/start.efi','EFI/SQ2Res/SHA256SUMS','README.txt','REPAIR.json']:
            z.write(output/name,name)
    with archive.open('rb') as stream:stream.read(16)
    report.update(archive=str(archive.resolve()),archive_bytes=archive.stat().st_size)
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-build',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();build(args.source_build,args.output)
