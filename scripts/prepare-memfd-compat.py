#!/usr/bin/env python3
"""Backport Android's memfd ashmem ioctls to the development 6.3 kernel."""
from pathlib import Path
import base64
import hashlib
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
KERNEL = Path('/root/.cache/sq2-probe-build/kernel')
CACHE = Path('/root/.cache/sq2-graphics')
COMMIT = '0803d5f5de723dd08d1957ab4723336d3d3a704d'

def source(name, path, digest):
    cached = CACHE / name
    if not cached.exists() or hashlib.sha256(cached.read_bytes()).hexdigest() != digest:
        url = f'https://android.googlesource.com/kernel/common/+/{COMMIT}/{path}?format=TEXT'
        data = base64.b64decode(urllib.request.urlopen(url, timeout=60).read())
        assert hashlib.sha256(data).hexdigest() == digest, path
        cached.write_bytes(data)
    return cached.read_text()

shim = source('memfd-ashmem-shim.c', 'mm/memfd-ashmem-shim.c',
              'f1e3591615f05ae1f662104d999b3af71d7d2ee2babedfe1fdc4995983af99a1')
header = source('ashmem-uapi.h', 'drivers/staging/android/uapi/ashmem.h',
                '32e4a2b6f0430e6607e36a1331c1d73661a1cd5e74262928bcbb4005ba571516')
shim = shim.replace('#include "../drivers/staging/android/ashmem.h"', '''#include <linux/compat.h>
#include <linux/shmem_fs.h>
#include "sq2-ashmem-uapi.h"
#ifdef CONFIG_COMPAT
#define COMPAT_ASHMEM_SET_SIZE _IOW(__ASHMEMIOC, 3, compat_size_t)
#define COMPAT_ASHMEM_SET_PROT_MASK _IOW(__ASHMEMIOC, 5, unsigned int)
#define COMPAT_ASHMEM_GET_FILE_ID _IOR(__ASHMEMIOC, 11, compat_uptr_t)
#endif''')
# 6.3's strscpy still takes an explicit destination size.
shim = shim.replace('strscpy(file_name, snapshot.name.name + offset)',
                    'strscpy(file_name, snapshot.name.name + offset, sizeof(file_name))')
# Restrict the compatibility interface to memfd_create files, not normal tmpfs.
shim = shim.replace('unsigned long inode_nr;\n', '''unsigned long inode_nr;

\tif (!SHMEM_I(file_inode(file))->sq2_memfd_compat)
\t\treturn -ENOTTY;
''')
(KERNEL / 'mm/sq2-memfd-ashmem.c').write_text(shim)
(KERNEL / 'mm/sq2-ashmem-uapi.h').write_text(header)

def replace(path, old, new):
    p = KERNEL / path
    text = p.read_text()
    if new in text:
        return
    assert text.count(old) == 1, path
    p.write_text(text.replace(old, new))

replace('mm/Makefile', 'obj-$(CONFIG_MEMFD_CREATE) += memfd.o',
        'obj-$(CONFIG_MEMFD_CREATE) += memfd.o sq2-memfd-ashmem.o')
replace('include/linux/shmem_fs.h', 'struct shmem_inode_info {',
        'struct shmem_inode_info {\n\tbool sq2_memfd_compat; /* zeroed by shmem_get_inode */')
replace('mm/memfd.c', '\tfile->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;',
        '\tif (!(flags & MFD_HUGETLB))\n\t\tSHMEM_I(file_inode(file))->sq2_memfd_compat = true;\n'
        '\tfile->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;')
replace('mm/shmem.c', 'static const struct file_operations shmem_file_operations = {',
        '''#ifdef CONFIG_MEMFD_CREATE
extern long ashmem_memfd_ioctl(struct file *, unsigned int, unsigned long);
#endif
static const struct file_operations shmem_file_operations = {
#ifdef CONFIG_MEMFD_CREATE
\t.unlocked_ioctl = ashmem_memfd_ioctl,
#ifdef CONFIG_COMPAT
\t.compat_ioctl = ashmem_memfd_ioctl,
#endif
#endif''')
print('Prepared pinned Android memfd/ashmem compatibility backport')
