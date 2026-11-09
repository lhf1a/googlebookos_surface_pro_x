"""Exercise actual QTI initialization/plane state with injected libdrm and atomic calls."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from tests.test_sm8350_composer_command_errors import block
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get("QTI_DISPLAY_SOURCE",str(ROOT/"out/qualcomm-full-stack-20261108/sm8350-display-v18/source")))
class InheritedPlanes(unittest.TestCase):
 @unittest.skipUnless((SOURCE/"sde-drm/drm_plane.cpp").exists(),"requires pinned display source")
 def test_actual_inherited_plane_handoff(self):
  config=json.loads((ROOT/"configs/sm8350-display-build.json").read_text())["dpu_inherited_planes"]
  patch=ROOT/config["patch"]
  self.assertEqual(hashlib.sha256(patch.read_bytes()).hexdigest(),config["patch_sha256"])
  with tempfile.TemporaryDirectory() as directory:
   temp=Path(directory);(temp/"sde-drm").mkdir()
   target=temp/"sde-drm/drm_plane.cpp";shutil.copy2(SOURCE/"sde-drm/drm_plane.cpp",target)
   sha=lambda:hashlib.sha256(target.read_bytes()).hexdigest()
   if sha()==config["patched_sources"]["sde-drm/drm_plane.cpp"]:
    subprocess.run(["patch","--reverse","--batch","--fuzz=0","-p1","-i",str(patch)],cwd=temp,check=True,stdout=subprocess.PIPE)
   self.assertEqual(sha(),config["base_sources"]["sde-drm/drm_plane.cpp"])
   for version in ("original","fixed"):
    if version=="fixed":
     subprocess.run(["patch","--batch","--fuzz=0","-p1","-i",str(patch)],cwd=temp,check=True,stdout=subprocess.PIPE)
     self.assertEqual(sha(),config["patched_sources"]["sde-drm/drm_plane.cpp"])
    template=(ROOT/"tests/qti_dpu_inherited_planes_test.cpp.in").read_text()
    for marker,token in (("@INIT@","int DRMPlane::InitAndParse("),
                         ("@POSTVALIDATE@","void DRMPlane::PostValidate("),
                         ("@POSTCOMMIT@","void DRMPlane::PostCommit("),
                         ("@UNUSED@","void DRMPlaneManager::UnsetUnusedResources(")):
     template=template.replace(marker,block(target.read_text(),token))
    source=temp/(version+".cpp");binary=temp/version;source.write_text(template)
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-unused-parameter",
                    "-fsanitize=address,undefined","-fno-pie","-no-pie",str(source),"-o",str(binary)],check=True)
    result=subprocess.run([str(binary)],text=True,capture_output=True,timeout=10)
    self.assertEqual(result.returncode,3 if version=="original" else 0,result.stdout+result.stderr)
    self.assertIn("ORPHAN_NOT_TRACKED" if version=="original" else "INHERITED_PLANES_PASS",result.stdout)
    print(result.stdout.strip())
if __name__=="__main__":unittest.main()
