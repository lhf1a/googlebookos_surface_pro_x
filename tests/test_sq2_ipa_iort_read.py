"""Actual ACPI RAM range guard fixture and strict IORT parser regression tests."""
from pathlib import Path
import importlib.util
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('iort', ROOT/'scripts/inspect-sq2-ipa-iort.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

def fixture():
    header = bytearray(48); header[:4] = b'IORT'
    smmu = struct.pack('<BHBIIIQ', 3, 24, 0, 0, 0, 0, 0x15000000)
    name = b'\\_SB.IPA\0'; named_len = 29 + len(name) + 20
    named = struct.pack('<BHBIII',1,named_len,0,0,1,29+len(name)) + bytes(13) + name
    named += struct.pack('<5I',0xb030000,2,0x520,48,0)
    data = header + smmu + named
    struct.pack_into('<I',data,4,len(data)); struct.pack_into('<II',data,36,2,48)
    data[9] = -sum(data) % 256
    return data

class Tests(unittest.TestCase):
    def test_mapping_and_checksum(self):
        d=fixture(); r=m.decode(d); x=r['selected'][0]['mappings'][0]
        self.assertEqual((x['output_base'],x['output_end'],x['target_smmu_base']),('0x520','0x522','0x15000000'))
        self.assertFalse(r['ownership_inferred'])
        d[-1]^=1
        with self.assertRaises(ValueError):m.decode(d)

    def test_malformed_lengths_and_refs(self):
        for off,fmt,value in [(4,'<I',0x10000),(40,'<I',0),(72+1,'<H',16),(72+8,'<I',1025),(-8,'<I',49)]:
            d=fixture();pos=off if off>=0 else len(d)+off
            struct.pack_into(fmt,d,pos,value);d[9]=0;d[9]=-sum(d)%256
            with self.assertRaises(ValueError):m.decode(d)

    def test_actual_ram_guard(self):
        s=(ROOT/'probe/test2/sq2-ipa-iort-read.c').read_text()
        guard=s[s.index('static int acpi_ram_range'):s.index('\nstatic int copy_acpi_ram')]
        h=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
typedef uint64_t u64;
#define U64_MAX UINT64_MAX
#define SQ2_ACPI_TABLE_MAX 65536U
#define EFI_PAGE_SHIFT 12
#define EFI_ACPI_RECLAIM_MEMORY 9
#define EFI_ACPI_MEMORY_NVS 10
#define EFI_MEMORY_WB 8
#define EFI_MEMMAP 4
typedef struct { unsigned int type,pad;u64 phys_addr,virt_addr,num_pages,attribute; } efi_memory_desc_t;
struct map { void *map,*map_end;int nr_map;unsigned long desc_size; };
static struct { struct map memmap; } efi;
static int enabled=1;
static int efi_enabled(int flag) { (void)flag;return enabled; }
'''+guard+r'''
int main(void) {
 efi_memory_desc_t md={9,0,0xfff00000,0,256,8};
 efi.memmap=(struct map){&md,(&md)+1,1,sizeof(md)};
 assert(acpi_ram_range(0xffffe018,36)==0);
 assert(acpi_ram_range(0xfffffff0,36)==-ERANGE);
 md.type=10;assert(acpi_ram_range(0xffffe018,36)==0);
 md.type=7;assert(acpi_ram_range(0xffffe018,36)==-EPERM);md.type=9;
 md.attribute=0;assert(acpi_ram_range(0xffffe018,36)==-EPERM);md.attribute=8;
 assert(acpi_ram_range(0x1e40030,4)==-EINVAL);
 assert(acpi_ram_range(UINT64_MAX-3,8)==-EINVAL);
 assert(acpi_ram_range(0xffffe018,65537)==-EINVAL);
 efi.memmap.nr_map=4097;assert(acpi_ram_range(0xffffe018,36)==-EOPNOTSUPP);efi.memmap.nr_map=1;
 efi.memmap.desc_size=0;assert(acpi_ram_range(0xffffe018,36)==-EOPNOTSUPP);efi.memmap.desc_size=sizeof(md);
 enabled=0;assert(acpi_ram_range(0xffffe018,36)==-EOPNOTSUPP);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as t:
            c=Path(t)/'guard.c'; exe=Path(t)/'guard';c.write_text(h)
            # Match kernel warning policy: nr_map is signed, but the actual
            # guard rejects <=0 and >4096 before the bounded unsigned loop.
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-sign-compare',
                            '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__': unittest.main()
