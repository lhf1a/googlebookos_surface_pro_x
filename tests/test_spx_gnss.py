import importlib.util
import json
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILDER_PATH = ROOT / "scripts/build-spx-gnss.py"


def load_builder():
    spec = importlib.util.spec_from_file_location("build_spx_gnss", BUILDER_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class SpxGnssTests(unittest.TestCase):
    def test_host_asan_ubsan_unit_suite(self):
        builder = load_builder()
        res = builder.run_host_tests("gcc")
        self.assertEqual(res.returncode, 0)
        self.assertIn("PASS spx_gnss_test", res.stdout)

    def test_arm64_cross_compile_zero_warnings_and_elf(self):
        builder = load_builder()
        with tempfile.TemporaryDirectory(prefix="spx-gnss-arm64-") as tmp:
            out_dir = Path(tmp) / "out"
            proc = subprocess.run(
                [
                    sys.executable,
                    str(BUILDER_PATH),
                    "--target",
                    "arm64",
                    "--output",
                    str(out_dir),
                ],
                check=True,
                text=True,
                capture_output=True,
            )
            self.assertEqual(proc.stderr, "", f"Unexpected compiler output: {proc.stderr}")
            binary = out_dir / "spx-gnss-daemon"
            self.assertTrue(binary.is_file())
            elf = builder.verify_aarch64_elf(binary)
            self.assertEqual(elf["elf_class"], "ELF64")
            self.assertEqual(elf["endian"], "little")
            self.assertEqual(elf["e_machine"], "EM_AARCH64")
            meta = json.loads((out_dir / "BUILD.json").read_text())
            self.assertEqual(meta["elf"]["sha256"], elf["sha256"])

    def test_daemon_socket_idle_and_clean_sigterm(self):
        builder = load_builder()
        with tempfile.TemporaryDirectory(prefix="spx-gnss-daemon-") as tmp:
            exe = Path(tmp) / "spx-gnss-daemon-host"
            sock_path = Path(tmp) / "gnss.sock"
            subprocess.run(
                [
                    "gcc",
                    "-std=c11",
                    "-O1",
                    "-g",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-I",
                    str(ROOT / "probe/gnss"),
                    *(str(p) for p in builder.DAEMON_SOURCES),
                    "-lm",
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            help_res = subprocess.run([str(exe), "--help"], check=True,
                                      text=True, capture_output=True)
            self.assertIn("--socket PATH", help_res.stderr)

            proc = subprocess.Popen(
                [str(exe), "--socket", str(sock_path), "--socket-mode", "0600"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            try:
                deadline = time.monotonic() + 3.0
                while time.monotonic() < deadline and not sock_path.exists():
                    self.assertIsNone(proc.poll(), "Daemon exited prematurely")
                    time.sleep(0.05)
                self.assertTrue(sock_path.exists(), "Daemon did not create Unix socket")
                self.assertEqual(sock_path.stat().st_mode & 0o777, 0o600)

                # Connect and immediately disconnect a client (QRTR lookup fails on host,
                # daemon must survive cleanly and stop retrying once client disconnects).
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as cli:
                    cli.settimeout(2.0)
                    cli.connect(str(sock_path))
                time.sleep(0.1)
                self.assertIsNone(proc.poll(), "Daemon crashed after client disconnect")
            finally:
                proc.send_signal(signal.SIGTERM)
                _, err = proc.communicate(timeout=5)
            self.assertEqual(proc.returncode, 0, f"Non-zero exit on SIGTERM: {err}")
            self.assertFalse(sock_path.exists(), "Daemon left stale Unix socket behind")

    def test_gnss_hal_extensions_never_return_ok_with_null_binder(self):
        src = (ROOT / "probe/gnss/spx-gnss-hal.cpp").read_text(encoding="utf-8")
        self.assertIn("class SpxGnssPsds final : public BnGnssPsds", src)
        self.assertIn("*out = mPsds;", src)
        # Android 17 libservices.core-gnss.so crashes in GnssPsdsAidl::setCallback()
        # if getExtension*() returns ScopedAStatus::ok() with *out = nullptr.
        self.assertNotIn("*out = nullptr;\n        return ScopedAStatus::ok();", src)


if __name__ == "__main__":
    unittest.main()
