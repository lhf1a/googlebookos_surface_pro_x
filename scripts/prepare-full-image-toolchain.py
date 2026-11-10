#!/usr/bin/env python3
"""Store the compiler/binutils in a relocatable private archive, no host links."""
import argparse
import json
import os
from pathlib import Path
import tarfile


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():
        raise ValueError('Use a fresh toolchain archive')
    base=a.input.resolve(); skipped=[]
    for name in ('gcc','as','ld','ar','nm','objcopy','strip'):
        if not (base/('usr/bin/aarch64-linux-gnu-'+name)).is_file():
            raise ValueError('Missing toolchain executable: '+name)
    temp=a.output.with_name(a.output.name+'.partial')
    if temp.exists():
        raise ValueError('Partial archive already exists')
    with tarfile.open(temp,'w:gz',compresslevel=1,dereference=True) as archive:
        for directory,dirs,files in os.walk(base,followlinks=True):
            d=Path(directory)
            archive.add(d,arcname=d.relative_to(base).as_posix(),recursive=False)
            for name in files:
                f=d/name;rel=f.relative_to(base).as_posix()
                if f.exists():
                    archive.add(f,arcname=rel,recursive=False)
                elif rel.startswith(('usr/lib/gcc-cross/aarch64-linux-gnu/13/lib','usr/share/doc/')):
                    skipped.append(rel)
                else:
                    raise ValueError('Missing link target: '+rel)
    temp.rename(a.output)
    a.output.with_suffix('.json').write_text(json.dumps({'input':str(base),
        'omitted_optional_target_library_or_doc_links':skipped,'host_glibc_required':True},indent=2)+'\n')


if __name__=='__main__':
    main()
