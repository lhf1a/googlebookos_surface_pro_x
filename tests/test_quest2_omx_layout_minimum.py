"""Exercise the actual allocator negotiation with injected real-size responses."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class MinimumLayout(unittest.TestCase):
    def test_actual_height_choice_and_error_cleanup(self):
        source=(ROOT/"probe/video/quest2-omx-native-layout.cpp").read_text()
        function=source[source.index('extern "C" int quest2_omx_capture_layout_for_usage_v2('):
                        source.index('extern "C" int quest2_omx_capture_layout_for_usage(')]
        prefix=r"""
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <cassert>
#include <cerrno>
#include "probe/video/quest2-omx-native-layout-check.h"
using buffer_handle_t=void*;
struct QuestGeometry { struct {int stride_bytes;} plane;unsigned scan,size; };
static unsigned live,allocations,releases,last_h;static bool fail_alloc,bad_geometry;
struct MockGralloc {
 bool device=true;
 int allocate(unsigned w,unsigned h,unsigned f,uint64_t,void**out) {
  assert(w==320);assert(f==0x7fa30c0a || f==0x7fa30c04);
  allocations++;last_h=h;if(fail_alloc)return -1;live++;*out=this;return 0;
 }
 int release(void*) {assert(live);live--;releases++;return 0;}
};
struct NativeBackend {MockGralloc gralloc;std::mutex mutex;};
static NativeBackend& backend(){static NativeBackend state;return state;}
static bool geometry(MockGralloc&,void*,unsigned,unsigned h,unsigned f,uint64_t,QuestGeometry&g) {
 if(bad_geometry)return false;
 g.plane.stride_bytes=f==0x7fa30c0a?768:512;g.scan=(h+31)&~31U;
 g.size=g.plane.stride_bytes*g.scan*3/2;return true;
}
"""
        checks=r"""
int main(void) {
 unsigned stride=9,scan=9,size=9;
 auto call=[&](unsigned minimum){return quest2_omx_capture_layout_for_usage_v2(320,240,0x30313050,0x20400903,minimum,&stride,&scan,&size);};
 assert(!call(491520));
 assert(stride==768 && scan==448 && size==516096 && last_h==448);
 assert(allocations==2 && releases==2 && live==0);
 assert(!call(0) && stride==768 && scan==256 && size==294912);
 fail_alloc=true;assert(call(491520)==-ENOMEM && !stride && !scan && !size && !live);fail_alloc=false;
 bad_geometry=true;assert(call(491520)==-EINVAL && !stride && !scan && !size && !live);bad_geometry=false;
 assert(call(UINT32_MAX)==-EINVAL && !stride && !scan && !size && !live);
 assert(quest2_omx_capture_layout_for_usage_v2(320,240,0xdead,0x20400903,1,&stride,&scan,&size)==-EINVAL);
 assert(!live);return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            d=Path(directory);(d/"check.cpp").write_text(prefix+function+checks)
            p=subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                "-fno-pie","-no-pie","-I",str(ROOT),str(d/"check.cpp"),"-o",str(d/"check")],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            p=subprocess.run([str(d/"check")],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
