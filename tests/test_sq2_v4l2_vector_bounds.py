"""Compile the real native runner and exercise completion coverage without device IO."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(shutil.which("gcc"),"requires Linux C compiler")
class VectorBounds(unittest.TestCase):
    def test_completion_masks_and_invalid_vector_sizes(self):
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory)
            for count in [0,1,6,30,32,33]:
                header=f"""
#define SQ2_VEC_W 320
#define SQ2_VEC_H 240
#define SQ2_VEC_FRAMES {count}
#define SQ2_VEC_SHA256 "host-self-check-no-media"
static const unsigned char sq2_vec_stream[1]={{0}};
static const unsigned sq2_vec_au[34]={{0}};
static const unsigned sq2_vec_crc_y[33]={{0}},sq2_vec_crc_uv[33]={{0}};
"""
                (temp/"sq2-h264-vector.h").write_text(header)
                command=["gcc","-std=gnu11","-O1","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined","-fno-pie","-no-pie","-I",str(temp),
                    str(ROOT/"probe/video/sq2-v4l2-decode-test.c"),"-o",str(temp/"runner")]
                result=subprocess.run(command,capture_output=True,text=True)
                if count in [0,33]:
                    self.assertNotEqual(result.returncode,0)
                    self.assertIn("vector must contain 1..32 frames",result.stderr)
                    continue
                self.assertEqual(result.returncode,0,result.stderr)
                result=subprocess.run([str(temp/"runner"),"--self-test"],capture_output=True,text=True,timeout=10)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn(f"frames={count} expected_mask={(1<<count)-1:08x} coverage=1",result.stdout)
