"""Fault injection executes the kernel patch's actual export and timeline functions."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
def added_file(name):
    patch=(ROOT/"patches/sq2-dpu-output-fence.patch").read_text()
    marker="+++ b/drivers/gpu/drm/msm/disp/dpu1/"+name+"\n"
    part=patch.split(marker,1)[1].split("\n--- ",1)[0]
    return "\n".join(line[1:] for line in part.splitlines() if line.startswith("+"))+"\n"
def block(source,start):
    a=source.index(start);left=source.index("{",a);depth=1;i=left+1
    while depth:
        if source[i]=="{":depth+=1
        elif source[i]=="}":depth-=1
        i+=1
    return source[a:i]
class KernelReleaseFences(unittest.TestCase):
    def test_actual_export_and_next_commit_lifecycle(self):
        source=added_file("dpu_release_fence.c")
        header=added_file("dpu_release_fence.h")
        text=(ROOT/"tests/qti_dpu_release_fence_kernel_test.c.in").read_text()
        for marker,start,origin in [
            ("@TIMELINE@","struct dpu_release_timeline {",header),
            ("@FENCE@","struct dpu_release_fence {",source),
            ("@EXPORT@","struct dpu_release_export {",source)]:
            text=text.replace(marker,block(origin,start)+";")
        functions=[
            "static void dpu_release_signal(",
            "int dpu_release_atomic_commit(",
            "void dpu_release_begin(",
            "void dpu_release_kickoff(",
            "void dpu_release_wait_result(",
            "void dpu_release_disable(",
            "void dpu_release_complete("]
        text=text.replace("@FUNCTIONS@","\n\n".join(block(source,f) for f in functions))
        with tempfile.TemporaryDirectory() as temp:
            cpp=Path(temp)/"kernel.c";binary=Path(temp)/"kernel"
            cpp.write_text(text)
            subprocess.run(["gcc","-std=gnu11","-Wall","-Wextra","-Werror",
                            "-Wno-unused-parameter","-Wno-misleading-indentation",
                            "-fsanitize=address,undefined","-fno-pie","-no-pie",
                            str(cpp),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
if __name__=="__main__":unittest.main()
