"""Exercise the actual translation helper and patched atomic completion code."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "out/qualcomm-full-stack-20261108/sm8350-reference-v1/display-source"
DRM = Path(os.environ.get("DRM_HEADERS", "/mnt/spx-workspace/toolchains/sq2-graphics/src/mesa-26.2.4/subprojects/libdrm-2.4.133"))

class PlaneProperties(unittest.TestCase):
    def test_actual_translation_and_error_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "test"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I" + str(DRM), "-I" + str(DRM / "include/drm"),
                            "-I" + str(ROOT / "probe/graphics"),
                            str(ROOT / "tests/qti_dpu_plane_properties_test.cpp"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(SOURCE.is_dir(), "requires pinned public display source")
    def test_actual_atomic_rejects_failed_plane_and_resets(self):
        config = json.loads((ROOT / "configs/sm8350-display-build.json").read_text())
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp)
            for relative in config["dpu_plane_properties"]["patched_sources"]:
                p = stage / relative
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes((SOURCE / relative).read_bytes())
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i",
                            str(ROOT / "patches/sm8350-dpu-plane-properties.patch")],
                           cwd=stage, check=True, capture_output=True)
            source = (stage / "sde-drm/drm_atomic_req.cpp").read_text()
            methods = source[source.index("int DRMAtomicReq::Validate()"):
                             source.index("}  // namespace sde_drm")]
            harness = r"""
#include <cassert>
#include <cstdint>
#include <cerrno>
#include <cstring>
#define DTRACE_SCOPED() ((void)0)
#define DRM_LOGE(...) ((void)0)
enum { DRM_MODE_ATOMIC_ALLOW_MODESET=1, DRM_MODE_ATOMIC_TEST_ONLY=2,
       DRM_MODE_ATOMIC_NONBLOCK=4 };
int ioctls=0, kernel_result=0, cursor=-1, last_flags=0;
int drmModeAtomicCommit(int, void*, int flags, void*) {
  ++ioctls; last_flags=flags; return kernel_result;
}
void drmModeAtomicSetCursor(void*, int v) { cursor=v; }
struct Manager {
  int validation=0, commits=0, retains=0; bool success=false;
  Manager* GetPlaneMgr() { return this; }
  Manager* GetCrtcMgr() { return this; }
  void UnsetUnusedResources(uint32_t, bool, void*) {}
  void RetainPlanes(uint32_t) { ++retains; }
  void PostValidate(uint32_t, bool ok) { ++validation; success=ok; }
  void PostCommit(uint32_t, bool ok) { ++commits; success=ok; }
};
struct DRMAtomicReq {
  void* drm_atomic_req_=nullptr;
  Manager* drm_mgr_;
  int fd_=-1, plane_error_=0;
  struct {uint32_t crtc_id=1;} token_;
  int Validate();
  int Commit(bool, bool);
};
"""
            harness += methods + r"""
int main() {
  Manager manager; DRMAtomicReq req; req.drm_mgr_=&manager;
  req.plane_error_=-EOPNOTSUPP;
  assert(req.Validate()==-EOPNOTSUPP && ioctls==0);
  assert(!manager.success && manager.validation==2 && cursor==0 && req.plane_error_==0);
  assert(req.Validate()==0 && ioctls==1 && manager.success);
  assert(last_flags & DRM_MODE_ATOMIC_TEST_ONLY);
  req.plane_error_=-ENOMEM;
  assert(req.Commit(false,false)==-ENOMEM && ioctls==1);
  assert(!manager.success && manager.commits==2 && req.plane_error_==0);
  assert(req.Commit(false,true)==0 && ioctls==2 && manager.success && manager.retains==1);
  assert(last_flags & DRM_MODE_ATOMIC_NONBLOCK);
  kernel_result=-1;
  assert(req.Commit(true,false)==-1 && ioctls==3 && !manager.success);
  assert(!(last_flags & DRM_MODE_ATOMIC_NONBLOCK));
}
"""
            cpp = stage / "atomic.cpp"
            cpp.write_text(harness)
            binary = stage / "atomic"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

if __name__ == "__main__":
    unittest.main()
