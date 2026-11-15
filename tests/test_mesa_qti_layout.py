"""Host ABI/branch tests complement, not replace, native DMA-BUF/EGL tests."""
from pathlib import Path
import os, subprocess, tempfile, unittest, sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import build_mesa_qti_layout as builder

class LayoutTest(unittest.TestCase):
    def test_reject_unknown_vendor(self):
        with self.assertRaises(ValueError): builder.build_payload(b'unknown')
    def test_actual_shim_branches(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td)
            (d/'vendor.c').write_text(r"""
#include <stdint.h>
#include <stdbool.h>
struct B{int w,h,f,l;uint64_t u;};
bool ub(int f,uint64_t u) __asm__("_ZN7gralloc13IsUBwcEnabledEim");
bool ub(int f,uint64_t u){(void)f;return (u&(1u<<28))!=0;}
void a(const struct B*b,unsigned*w,unsigned*h) __asm__("_ZN7gralloc24GetAlignedWidthAndHeightERKNS_10BufferInfoEPjS3_");
void a(const struct B*b,unsigned*w,unsigned*h){*w=b->w;*h=b->h;}
int s(const struct B*b,unsigned*z,unsigned*w,unsigned*h) __asm__("_ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_");
int s(const struct B*b,unsigned*z,unsigned*w,unsigned*h){*w=b->w;*h=b->h;*z=(unsigned)b->w*(unsigned)b->h*4;return b->f==999?-7:0;}
int s5(const struct B*b,unsigned*z,unsigned*w,unsigned*h,void*m) __asm__("_ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_P16GraphicsMetadata");
int s5(const struct B*b,unsigned*z,unsigned*w,unsigned*h,void*m){if(m)*(int*)m=73;return s(b,z,w,h);}
""")
            subprocess.run(['cc','-shared','-fPIC',str(d/'vendor.c'),'-o',str(d/builder.VENDOR_NAME)],check=True)
            subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',str(builder.SOURCE),'-ldl','-pthread','-o',str(d/'shim.so')],check=True)
            env=dict(os.environ,LD_LIBRARY_PATH=td)
            script=r"""
import ctypes
class B(ctypes.Structure):_fields_=[('w',ctypes.c_int),('h',ctypes.c_int),('f',ctypes.c_int),('l',ctypes.c_int),('u',ctypes.c_uint64)]
lib=ctypes.CDLL(SHIM)
s=getattr(lib,'_ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_')
s5=getattr(lib,'_ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_P16GraphicsMetadata')
a=getattr(lib,'_ZN7gralloc24GetAlignedWidthAndHeightERKNS_10BufferInfoEPjS3_')
z=ctypes.c_uint();w=ctypes.c_uint();h=ctypes.c_uint()
count=0
for f in (1,2,5,43,22):
 for ht in (1,2,3,4,65,318,320,1920):
  b=B(2880,ht,f,1,0x300);before=bytes(b)
  assert s(ctypes.byref(b),ctypes.byref(z),ctypes.byref(w),ctypes.byref(h))==0
  assert h.value==(ht+3)&~3 and w.value==2880 and z.value==w.value*h.value*4
  assert bytes(b)==before
  a(ctypes.byref(b),ctypes.byref(w),ctypes.byref(h))
  assert h.value==(ht+3)&~3 and w.value==2880
  m=ctypes.c_int()
  assert s5(ctypes.byref(b),ctypes.byref(z),ctypes.byref(w),ctypes.byref(h),ctypes.byref(m))==0 and m.value==73
  count+=1
for b in (B(2880,318,1,1,1<<28),B(2880,318,0x36,1,0x300),B(2880,318,1,2,0x300),B(2880,318,999,1,0)):
 expected=-7 if b.f==999 else 0
 assert s(ctypes.byref(b),ctypes.byref(z),ctypes.byref(w),ctypes.byref(h))==expected and h.value==318
 count+=1
b=B(2880,2147483647,1,1,0x300)
assert s(ctypes.byref(b),ctypes.byref(z),ctypes.byref(w),ctypes.byref(h))==-22
print('HOST_BRANCH_CASES',count+1)
"""
            subprocess.run([sys.executable,'-c','SHIM='+repr(str(d/'shim.so'))+'\n'+script],env=env,check=True)
if __name__=='__main__':unittest.main()
