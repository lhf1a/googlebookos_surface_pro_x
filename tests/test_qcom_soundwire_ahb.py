"""Actual WCD AHB operations: forced thread interleaving, error unlock, capacity ordering."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest
from tests.test_qti_dpu_empty_flush import block
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"out/quest2-video-618-20261107/uncached-native/source/kernel/drivers/soundwire/qcom.c"
spec=importlib.util.spec_from_file_location("soundwire_patch",ROOT/"scripts/patch-qcom-soundwire-ahb.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class SoundWire(unittest.TestCase):
    def test_actual_ahb_race_and_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);target=root/"drivers/soundwire/qcom.c"
            target.parent.mkdir(parents=True);target.write_bytes(SOURCE.read_bytes())
            for original in (True,False):
                if not original:module.apply(root)
                source=target.read_text()
                text=(ROOT/"tests/qcom_soundwire_ahb_test.c.in").read_text()
                for marker,name in [("@READ@","qcom_swrm_ahb_reg_read"),("@WRITE@","qcom_swrm_ahb_reg_write")]:
                    text=text.replace(marker,block(source,"static int "+name+"("))
                cpp=root/"test.c";binary=root/"test";cpp.write_text(text)
                subprocess.run(["gcc","-std=gnu11","-Wall","-Wextra","-Werror","-Wno-unused-function","-pthread",
                                "-fsanitize=address,undefined","-fno-pie","-no-pie",str(cpp),"-o",str(binary)],check=True)
                result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
                self.assertEqual(result.returncode,3 if original else 0,result.stdout+result.stderr)
                print(result.stdout.strip())
            module.apply(root) # idempotent
            target.write_text(target.read_text()+"/* unknown source */")
            with self.assertRaises(ValueError):module.apply(root)
    def test_fifo_initialized_before_irq_and_not_overwritten_on_resume(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);target=root/"drivers/soundwire/qcom.c";target.parent.mkdir(parents=True)
            target.write_bytes(SOURCE.read_bytes());module.apply(root);source=target.read_text()
            probe=source.split("static int qcom_swrm_probe(",1)[1].split("static void qcom_swrm_remove(",1)[0]
            self.assertLess(probe.index("mutex_init(&ctrl->ahb_lock)"),probe.index("qcom_swrm_get_port_config"))
            self.assertLess(probe.index("ctrl->wr_fifo_depth ="),probe.index("devm_request_threaded_irq"))
            self.assertIn("if (!ctrl->wr_fifo_depth)",probe)
            self.assertNotIn("fifo_depth",block(source,"static int qcom_swrm_init("))
if __name__=="__main__":unittest.main()
