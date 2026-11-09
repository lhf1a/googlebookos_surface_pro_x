import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NativeVulkanMemoryTests(unittest.TestCase):
    def test_real_tls_policy_and_forwarded_arguments(self):
        with tempfile.TemporaryDirectory(prefix="sq2-native-vulkan-memory-") as directory:
            binary = pathlib.Path(directory) / "test"
            subprocess.run(["cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread",
                            "-I", str(ROOT / "probe/graphics"),
                            str(ROOT / "probe/graphics/quest2-native-vulkan-memory.c"),
                            str(ROOT / "tests/quest2_native_vulkan_memory_test.c"), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], check=True, text=True, capture_output=True, timeout=20)
            self.assertIn("matrix=512 nested=2 threaded=2000", result.stdout)


if __name__ == "__main__":
    unittest.main()
