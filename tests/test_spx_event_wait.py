"""Host ASan/UBSan and integration tests for spx-event-wait and event-driven watchers."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import full_image as fi_mod

SRC = ROOT / "probe/test2/spx-event-wait.c"


class SpxEventWaitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._td = tempfile.TemporaryDirectory(prefix="spx-event-wait-test-")
        cls.host_bin = Path(cls._td.name) / "spx-event-wait"
        cc = shutil.which("gcc") or shutil.which("cc")
        if not cc:
            raise unittest.SkipTest("host C compiler not available")
        subprocess.run(
            [
                cc, "-std=c11", "-O1", "-g",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-Wall", "-Wextra", "-Werror",
                str(SRC), "-o", str(cls.host_bin),
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls._td.cleanup()

    def test_inotify_match_ignores_unrelated_and_wakes_on_target(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            def writer():
                time.sleep(0.08)
                (d / "unrelated").write_text("x")
                time.sleep(0.08)
                (d / "ota-request").write_text("go\n")

            t = threading.Thread(target=writer)
            t.start()
            t0 = time.monotonic()
            res = subprocess.run(
                [
                    str(self.host_bin), "--oneshot",
                    "--inotify", str(d),
                    "--match", "ota-request,disk-request",
                    "--debounce-ms", "20",
                    "--timeout-ms", "3000",
                ],
                capture_output=True, text=True, check=True,
            )
            elapsed = time.monotonic() - t0
            t.join()
            self.assertEqual(res.stdout.strip(), "event")
            self.assertGreaterEqual(elapsed, 0.14)
            self.assertLess(elapsed, 2.0)

    def test_inotify_check_exists_is_race_free(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            req = d / "request"
            req.write_text("already-here\n")
            t0 = time.monotonic()
            res = subprocess.run(
                [
                    str(self.host_bin), "--oneshot",
                    "--inotify", str(d),
                    "--match", "request",
                    "--check-exists", str(req),
                    "--timeout-ms", "5000",
                ],
                capture_output=True, text=True, check=True,
            )
            self.assertEqual(res.stdout.strip(), "exists")
            self.assertLess(time.monotonic() - t0, 1.0)

    def test_inotify_timeout(self):
        with tempfile.TemporaryDirectory() as td:
            res = subprocess.run(
                [
                    str(self.host_bin), "--oneshot",
                    "--inotify", td,
                    "--match", "never-created",
                    "--timeout-ms", "120",
                ],
                capture_output=True, text=True, check=True,
            )
            self.assertEqual(res.stdout.strip(), "timeout")

    def test_sysfs_check_file_diff_detects_change_and_times_out_when_unchanged(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            ready = d / "ready"
            counter = d / "resume_count"
            ready.write_text("ready\n")
            counter.write_text("7\n")

            res_changed = subprocess.run(
                [
                    str(self.host_bin), "--oneshot",
                    "--sysfs", str(ready),
                    "--check-file-diff", f"{counter}:6",
                    "--timeout-ms", "3000",
                ],
                capture_output=True, text=True, check=True,
            )
            self.assertEqual(res_changed.stdout.strip(), "changed")

            res_same = subprocess.run(
                [
                    str(self.host_bin), "--oneshot",
                    "--sysfs", str(ready),
                    "--check-file-diff", f"{counter}:7",
                    "--timeout-ms", "120",
                ],
                capture_output=True, text=True, check=True,
            )
            self.assertEqual(res_same.stdout.strip(), "timeout")

    def test_uevent_stream_emits_init_and_exits_on_sigterm(self):
        with subprocess.Popen(
            [str(self.host_bin), "--stream", "--uevent", "input", "--timeout-ms", "5000"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        ) as proc:
            try:
                line = proc.stdout.readline().strip()
                self.assertEqual(line, "init")
            finally:
                proc.terminate()
                rc = proc.wait(timeout=3)
        self.assertEqual(rc, 0)

    def test_cross_compile_and_full_image_watcher_wiring(self):
        if not shutil.which("aarch64-linux-gnu-gcc"):
            self.skipTest("aarch64-linux-gnu-gcc not available")
        raw = fi_mod.build_event_wait()
        self.assertEqual(raw[:6], b"\x7fELF\x02\x01")
        self.assertEqual(struct.unpack_from("<H", raw, 18)[0], 183)

        records, bodies = {}, {}
        fi_mod.install_event_watchers(records, bodies, event_wait_bytes=raw)
        self.assertEqual(bodies["bin/spx-event-wait"], raw)
        for name in fi_mod.EVENT_WATCHER_HELPERS:
            key = f"bin/{name}"
            self.assertIn(key, bodies)
            self.assertNotIn(b"\r\n", bodies[key])
            self.assertIn(b"spx-event-wait", bodies[key])

        for extra in (
            ROOT / "probe/test2/touch-calibration-boot",
            ROOT / "probe/test2/spx-advanced-control-boot",
            ROOT / "probe/ota/spx-ota-boot",
        ):
            data = extra.read_bytes()
            self.assertNotIn(b"\r\n", data)
            self.assertIn(b"spx-event-wait", data)
            subprocess.run(["sh", "-n", str(extra)], check=True)


if __name__ == "__main__":
    unittest.main()
