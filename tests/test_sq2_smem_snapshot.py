"""Compile the actual read-only diagnostic with bounded fake SMEM regions."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
    temp = Path(d)
    for n in ['module.h', 'of.h', 'soc/qcom/smem.h']:
        f = temp / 'linux' / n
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text('')
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define __init
#define __exit
#define QCOM_SMEM_HOST_ANY 0xffffffff
#define ENODEV 19
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define IS_ERR(p) ((intptr_t)(p)<0)
#define PTR_ERR(p) ((long)(p))
#define pr_info(...) ((void)0)
#define KERN_INFO ""
#define DUMP_PREFIX_OFFSET 0
#define true 1
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define module_init(f)
#define module_exit(f)
#define MODULE_LICENSE(s)
#define MODULE_DESCRIPTION(s)
static int allowed, reads, dumps;
static unsigned char bytes[256];
static int of_machine_is_compatible(const char *name) { (void)name;return allowed; }
static const void *qcom_smem_get(unsigned h,unsigned item,size_t *size) {
 (void)h;reads++;
 if(item==421)return (void *)(intptr_t)-2;
 *size=item==435?0:item==428?32:256;return bytes;
}
static void print_hex_dump(const char *a,const char *b,int c,int d,int e,const void *p,size_t n,int ascii) {
 (void)a;(void)b;(void)c;(void)d;(void)e;(void)ascii;
 assert(p==bytes);assert(n<=128);if(dumps==0)assert(n==0);if(dumps==1)assert(n==32);dumps++;
}
'''
    harness += '#include "' + str(root / 'probe/virtualization/sq2-smem-snapshot.c') + '"\n'
    harness += 'int main(void){ assert(snapshot_init()==-19 && reads==0); allowed=1; assert(snapshot_init()==0 && reads==7 && dumps==6); snapshot_exit(); }'
    (temp / 'test.c').write_text(harness)
    subprocess.run(['gcc','-Wall','-Wextra','-Werror','-I'+d,str(temp/'test.c'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
print('PASS: platform guard, missing item, zero/short/large bounded reads')
