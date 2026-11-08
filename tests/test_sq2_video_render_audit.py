from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RenderAudit(unittest.TestCase):
    def test_real_java_callback_audit(self):
        with tempfile.TemporaryDirectory() as td:
            subprocess.run(["javac", "--release", "8", "-d", td,
                str(ROOT / "probe/video/player-app/RenderedFrameAudit.java"),
                str(ROOT / "tests/fixtures/quest2_video/RenderedFrameAuditTest.java")], check=True)
            result = subprocess.run(["java", "-cp", td,
                "org.surface.sq2.videotest.RenderedFrameAuditTest"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("RENDER_AUDIT_PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
