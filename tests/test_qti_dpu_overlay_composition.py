"""Run patched upstream strategy/resource and DisplayBase retry code under sanitizers.

The default run applies the candidate patch anew to display-v17. QTI_DISPLAY_SOURCE
may point at an independently built tree to verify integration. Platform/RC/DRM
boundaries are fixtures; selection, allocation, geometry and retry bodies are real.
"""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "out/qualcomm-full-stack-20261108/sm8350-display-v17/source"
PATCH = ROOT / "patches/sm8350-dpu-overlay-composition.patch"


def block(source, start):
    begin = source.index(start)
    left = source.index("{", begin)
    depth, end = 1, left + 1
    while depth:
        if source[end] == "{": depth += 1
        elif source[end] == "}": depth -= 1
        end += 1
    return source[begin:end]


class OverlayComposition(unittest.TestCase):
    def test_real_multilayer_scaling_and_all_device_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            if os.environ.get("QTI_DISPLAY_SOURCE"):
                source = Path(os.environ["QTI_DISPLAY_SOURCE"])
            else:
                source = temp / "source"
                shutil.copytree(BASE, source)
                subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(PATCH)],
                               cwd=source, check=True, capture_output=True, text=True)
            text = (ROOT / "tests/qti_dpu_overlay_composition_test.cpp.in").read_text()
            for cls, filename, names in [
                ("Strategy", "strategy.cpp", ["Start", "GetDpuOverlayIndices", "GetNextStrategy", "CanSkipValidate", "Reconfigure"]),
                ("ResourceDefault", "resource_default.cpp", ["Prepare", "PrepareLayer", "Purge",
                  "SearchPipe", "NextPipe", "GetPipe", "IsScalingNeeded", "SrcSplitConfig",
                  "DisplaySplitConfig", "Config", "CalculateCropRects", "ValidateLayerParams",
                  "ValidateDimensions", "ValidatePipeParams", "ValidateScaling", "ValidateDownScaling",
                  "ValidateUpScaling", "GetScaleFactor", "SetDecimationFactor", "SplitRect",
                  "AlignPipeConfig", "CalculateDecimation"]),
                ("CompManager", "comp_manager.cpp", ["Prepare"]),
                ("DisplayBase", "display_base.cpp", ["Prepare"]),
            ]:
                src = (source / "sdm/libs/core" / filename).read_text()
                methods = []
                for name in names:
                    match = re.search(r"(?:DisplayError|uint32_t|int|bool|void|std::vector<uint32_t>) " + cls + "::" + name + r"\(", src)
                    self.assertIsNotNone(match, name)
                    methods.append(block(src, match.group()))
                decls = "\n".join(x[:x.index("{")].replace(cls + "::", "") + ";" for x in methods)
                text = text.replace("@" + cls.upper() + "_DECLS@", decls)
                text = text.replace("@" + cls.upper() + "_METHODS@", "\n".join(methods))
                print(cls, hashlib.sha256("\n".join(methods).encode()).hexdigest())
            color = block((ROOT / "probe/graphics/qti-dpu-sdm-contract.h").read_text(), "inline bool DpuColorSupported(")
            text = text.replace("@COLOR@", color)
            cpp, binary = temp / "test.cpp", temp / "test"
            cpp.write_text(text)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-Wno-misleading-indentation", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", str(cpp), "-o", str(binary)], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20,
                                 env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                                          UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())


if __name__ == "__main__":
    unittest.main()
