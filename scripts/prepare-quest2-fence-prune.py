#!/usr/bin/env python3
"""Private v77 candidate: prune a completed pending server-wait dependency.

Poll with the existing GSL wait API and timeout zero. Errors/unsignaled objects
retain the original merge path. Never close a borrowed fence FD directly.
Requires the exact zero-area candidate; no device writes or normal installation.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'out/quest2-gpu/gles-linked-zero-area'
OUTPUT = ROOT / 'out/quest2-gpu/gles-linked-fence-prune'
ENTRY, STUB = 0x281F98, 0x3EA2C0
OUTPUT_SHA = '1d3ba33ea006962766f631362556ffa1b1e05835ceb733285bfbe37e580cce77'
ASSEMBLY = '''.text
.p2align 2
.global prune
prune:
 stp x29,x30,[sp,#-16]!
 mov x29,sp
 ldr x0,[x20,#16296]
 cbz x0,done
 ldr x8,[x20,#144]
 ldr x8,[x8,#800]
 mov w1,wzr
 blr x8
 cbnz w0,done
 ldr x0,[x20,#16296]
 ldr x8,[x20,#144]
 ldr x8,[x8,#792]
 blr x8
 str xzr,[x20,#16296]
done:
 ldr x1,[x20,#16296]
 ldp x29,x30,[sp],#16
 ret
.section .note.GNU-stack,"",%progbits
'''

def run(*args):
    return subprocess.run(args, check=True, capture_output=True, timeout=60).stdout

def main():
    spec = importlib.util.spec_from_file_location('zero_area', ROOT/'scripts/prepare-quest2-zero-area.py')
    previous = importlib.util.module_from_spec(spec); spec.loader.exec_module(previous)
    require = previous.require
    require(not OUTPUT.exists(), 'Refusing existing output')
    previous.SOURCE = SOURCE
    donors = dict(previous.EXPECTED_DONORS); donors[previous.LIB] = previous.OUTPUT_SHA
    previous.manifest('SHA256SUMS', donors)
    previous.manifest('PROBE_SHA256SUMS', previous.EXPECTED_PROBES)
    original = (SOURCE/previous.LIB).read_bytes()
    require(hashlib.sha256(original).hexdigest() == previous.OUTPUT_SHA, 'Unexpected input')
    require(struct.unpack_from('<4I', original, ENTRY-8) ==
            (0xf9404109, 0xf9400534, 0xf95fd681, 0xb4000181), 'Unexpected call-site liveness')
    with tempfile.TemporaryDirectory(prefix='sq2-fence-prune-') as tmp:
        tmp = Path(tmp); (tmp/'prune.S').write_text(ASSEMBLY)
        run('aarch64-linux-gnu-gcc', '-c', str(tmp/'prune.S'), '-o', str(tmp/'prune.o'))
        run('aarch64-linux-gnu-objcopy', '-O', 'binary', '-j', '.text', str(tmp/'prune.o'), str(tmp/'stub.bin'))
        stub = (tmp/'stub.bin').read_bytes()
        # Execute the identical instructions against null, pending, complete,
        # error cases. No GPU or proprietary code is executed.
        (tmp/'test.c').write_text('''#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern void *invoke(void*);
static unsigned waits,destroys; static int result;
static int wait_fn(void *p,unsigned timeout){if(!p||timeout)abort();waits++;return result;}
static int destroy_fn(void *p){if(!p)abort();destroys++;return 0;}
int main(void){
 unsigned char context[16304]={0}; void *table[101]={0}; int object=1;
 table[99]=(void*)destroy_fn;table[100]=(void*)wait_fn;
 memcpy(context+144,&(void*){table},8);
 const int outcomes[]={0,-10,-5,1};
 for(unsigned n=0;n<5;n++){
  void *p=n?&object:NULL;memcpy(context+16296,&p,8);
  waits=destroys=0;result=n?outcomes[n-1]:0;
  void *actual=invoke(context);void *expected=n&&result?p:NULL;
  void *stored;memcpy(&stored,context+16296,8);
  if(actual!=expected||stored!=expected||waits!=(n!=0)||destroys!=(n!=0&&result==0))return 1;
 }
 puts("FENCE_PRUNE_QEMU_OK cases=5 timeout=0 pending_and_errors_preserved=1");
}
''')
        (tmp/'invoke.S').write_text('''.text
.global invoke
invoke:
 stp x19,x20,[sp,#-32]!
 str x30,[sp,#16]
 mov x20,x0
 bl prune
 mov x0,x1
 ldr x30,[sp,#16]
 ldp x19,x20,[sp],#32
 ret
.section .note.GNU-stack,"",%progbits
''')
        run('aarch64-linux-gnu-gcc', '-static', '-O2', '-Wall', '-Wextra', '-Werror',
            str(tmp/'test.c'), str(tmp/'invoke.S'), str(tmp/'prune.o'), '-o', str(tmp/'test'))
        check = run('qemu-aarch64', str(tmp/'test')).decode().strip()
        require(check.startswith('FENCE_PRUNE_QEMU_OK'), 'QEMU verification failed')
    data = bytearray(original)
    phoff = struct.unpack_from('<Q', data, 32)[0]; phsize, count = struct.unpack_from('<HH', data, 54)
    require(phsize == 56 and count == 12, 'Unexpected program headers')
    headers = [struct.unpack_from('<IIQQQQQQ', data, phoff+i*56) for i in range(count)]
    rx = [i for i,p in enumerate(headers) if p == (1,5,0xe8000,0xe8000,0xe8000,0x3022b4,0x3022b4,0x1000)]
    require(len(rx)==1 and STUB+len(stub)<0x3eb000, 'Unexpected RX padding')
    require(not any(data[STUB:0x3eb000]), 'Nonzero padding')
    for i,p in enumerate(headers):
        require(not previous.overlap(p[2],p[5],STUB,len(stub)), 'Segment overlaps stub')
        if p[0]==1: require(not previous.overlap(p[3],p[6],STUB,len(stub)), 'LOAD overlaps stub')
    shoff = struct.unpack_from('<Q', data, 40)[0]
    shsize, shcount = struct.unpack_from('<HH', data, 58)
    require(shsize == 64 and shoff+shcount*shsize <= len(data), 'Unexpected section headers')
    for i in range(shcount):
        section = struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*64)
        if section[1] != 8:
            require(not previous.overlap(section[4],section[5],STUB,len(stub)), 'Section overlaps stub')
    branch = previous.branch(ENTRY,STUB) | 0x80000000
    struct.pack_into('<I',data,ENTRY,branch);data[STUB:STUB+len(stub)]=stub
    at=phoff+rx[0]*56+32;struct.pack_into('<QQ',data,at,STUB+len(stub)-0xe8000,STUB+len(stub)-0xe8000)
    allowed=set(range(ENTRY,ENTRY+4))|set(range(STUB,STUB+len(stub)))|set(range(at,at+16))
    require(len(data)==len(original) and all(a==b or i in allowed for i,(a,b) in enumerate(zip(original,data))), 'Unexpected changes')
    require(hashlib.sha256(data).hexdigest() == OUTPUT_SHA, 'Unexpected candidate digest')
    OUTPUT.mkdir()
    for name in donors|previous.EXPECTED_PROBES: shutil.copy2(SOURCE/name,OUTPUT/name)
    (OUTPUT/previous.LIB).write_bytes(data)
    donors[previous.LIB]=hashlib.sha256(data).hexdigest()
    (OUTPUT/'SHA256SUMS').write_text(''.join(f'{v}  {k}\n' for k,v in donors.items()))
    shutil.copy2(SOURCE/'PROBE_SHA256SUMS',OUTPUT/'PROBE_SHA256SUMS')
    (OUTPUT/'fence-prune.json').write_text(json.dumps({'input_sha256':previous.OUTPUT_SHA,
        'output_sha256':donors[previous.LIB],'entry':hex(ENTRY),'stub':hex(STUB),
        'stub_hex':stub.hex(),'qemu':check,'status':'offline_prepared_not_device_tested'},indent=2)+'\n')
    print(check);print('FENCE_PRUNE_PREPARED',donors[previous.LIB])

if __name__=='__main__': main()
