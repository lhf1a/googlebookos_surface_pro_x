"""Source-pinned patch and actual allocator lifetime/fence failure-path checks."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "out/qualcomm-full-stack-20261108/sm8350-reference-v1/display-source"
spec = importlib.util.spec_from_file_location("hwc_allocator_build",
                                             ROOT / "scripts/build-sm8350-hwc-allocator.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class Allocator(unittest.TestCase):
    def test_unknown_source_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "composer/hwc_buffer_allocator.h"
            path.parent.mkdir()
            path.write_text("changed")
            with self.assertRaises(ValueError):
                module.prepare(root, root / "output")
            self.assertFalse((root / "output").exists())

    @unittest.skipUnless(SOURCE.is_dir(), "requires pinned public display source")
    def test_actual_free_lock_unlock_ownership(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "patched"
            module.prepare(SOURCE, output)
            source = (output / "composer/hwc_buffer_allocator.cpp").read_text()
            def method(name):
                start = source.index("int HWCBufferAllocator::" + name + "(")
                return source[start:source.index("\n}\n", start) + 3]
            start = source.index("int MapperError(")
            errors = source[start:source.index("\n}\n", start) + 3]
            harness = r"""
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
using std::shared_ptr;
struct native_handle_t {};
struct private_handle_t : native_handle_t {};
using buffer_handle_t = const native_handle_t*;
enum AIMapper_Error { AIMAPPER_ERROR_NONE, AIMAPPER_ERROR_BAD_BUFFER,
  AIMAPPER_ERROR_BAD_VALUE, AIMAPPER_ERROR_UNSUPPORTED, AIMAPPER_ERROR_NO_RESOURCES };
struct ARect {int32_t left=0,top=0,right=0,bottom=0;};
constexpr uint64_t AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN=3;
int result=0, free_calls=0, lock_calls=0, unlock_calls=0, last_fence=-1;
bool null_address=false;
int memory=17, returned_fence=-1;
AIMapper_Error free_buffer(buffer_handle_t) { ++free_calls; return AIMapper_Error(result); }
AIMapper_Error lock_buffer(buffer_handle_t, uint64_t usage, ARect rect, int fd, void**out) {
  ++lock_calls; last_fence=fd;
  assert(usage==3 && rect.left==0 && rect.right==0);
  if(fd>=0)close(fd);
  *out=null_address?nullptr:&memory;
  return AIMapper_Error(result);
}
AIMapper_Error unlock_buffer(buffer_handle_t, int*out) {
  ++unlock_calls; *out=returned_fence; return AIMapper_Error(result);
}
struct AIMapper { struct {
  decltype(&free_buffer) freeBuffer=free_buffer;
  decltype(&lock_buffer) lock=lock_buffer;
  decltype(&unlock_buffer) unlock=unlock_buffer;
}v5;};
struct Fence {
  int fd;
  static int Dup(const shared_ptr<Fence>&f) {return f?dup(f->fd):-1;}
};
struct AllocatedBufferInfo {int fd=-1;uint64_t id=0;const void*native_handle=nullptr;};
struct BufferInfo {AllocatedBufferInfo alloc_buffer_info;void*private_data=nullptr;};
class HWCBufferAllocator {
public:
  AIMapper*mapper_=nullptr;
  int GetGrallocInstance(){return mapper_?0:-ENODEV;}
  int FreeBuffer(BufferInfo*);
  int MapBuffer(const private_handle_t*,shared_ptr<Fence>);
  int UnmapBuffer(const private_handle_t*,int*);
};
"""
            harness += errors + method("FreeBuffer") + method("MapBuffer") + method("UnmapBuffer")
            harness += r"""
int main() {
  AIMapper mapper;HWCBufferAllocator allocator;allocator.mapper_=&mapper;
  private_handle_t handle;
  BufferInfo buffer;buffer.private_data=&handle;
  buffer.alloc_buffer_info.fd=9;buffer.alloc_buffer_info.native_handle=&handle;
  result=AIMAPPER_ERROR_NO_RESOURCES;
  assert(allocator.FreeBuffer(&buffer)==-ENOMEM && buffer.private_data==&handle);
  assert(buffer.alloc_buffer_info.fd==9 && free_calls==1);
  result=0;
  assert(allocator.FreeBuffer(&buffer)==0 && !buffer.private_data);
  assert(buffer.alloc_buffer_info.fd==-1 && !buffer.alloc_buffer_info.native_handle);
  assert(allocator.FreeBuffer(&buffer)==-EINVAL && free_calls==2);
  int fds[2];assert(pipe(fds)==0);
  auto fence=std::make_shared<Fence>();fence->fd=fds[0];
  assert(allocator.MapBuffer(&handle,fence)==0);
  assert(last_fence>=0 && last_fence!=fds[0]);
  assert(fcntl(last_fence,F_GETFD)==-1 && errno==EBADF);
  assert(fcntl(fds[0],F_GETFD)>=0);
  result=AIMAPPER_ERROR_UNSUPPORTED;
  assert(allocator.MapBuffer(&handle,fence)==-ENOTSUP);
  assert(fcntl(fds[0],F_GETFD)>=0 && fcntl(last_fence,F_GETFD)==-1);
  assert(allocator.MapBuffer(nullptr,fence)==-EINVAL);
  result=0;int release=-1;returned_fence=dup(fds[0]);assert(returned_fence>=0);
  assert(allocator.UnmapBuffer(&handle,&release)==0 && release==returned_fence);
  assert(fcntl(release,F_GETFD)>=0);close(release);returned_fence=-1;
  assert(allocator.UnmapBuffer(&handle,nullptr)==-EINVAL);
  result=AIMAPPER_ERROR_BAD_BUFFER;
  assert(allocator.UnmapBuffer(&handle,&release)==-EINVAL && release==-1);
  result=0;null_address=true;int previous_unlocks=unlock_calls;
  assert(allocator.MapBuffer(&handle,nullptr)==-EINVAL && unlock_calls==previous_unlocks+1);
  allocator.mapper_=nullptr;
  assert(allocator.MapBuffer(&handle,nullptr)==-ENODEV);
  close(fds[0]);close(fds[1]);
}
"""
            cpp = output / "lifetime.cpp"
            cpp.write_text(harness)
            binary = output / "lifetime"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

if __name__ == "__main__":
    unittest.main()
