"""Run the visible probe's actual dimension parser without Android/GPU dependencies."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('javac') and shutil.which('java'), 'JDK required')
class VisibleExtent(unittest.TestCase):
    def test_actual_dimension_parser(self):
        source = (ROOT/'probe/graphics/quest2-visible/Sq2Quest2Visible.java').read_text()
        start = source.index('    private static int extent(')
        end = source.index('\n    }', start) + len('\n    }')
        harness = 'public class ExtentCheck {\n' + source[start:end] + r"""
    private static void rejected(String requested, int available) {
        try { extent(requested, 640, available); }
        catch (IllegalArgumentException expected) { return; }
        throw new AssertionError("Accepted invalid extent: " + requested);
    }
    public static void main(String[] args) {
        assert extent(null, 640, 2752) == 640;
        assert extent(null, 640, 400) == 400;
        assert extent("2073", 640, 2752) == 2073;
        assert extent("1382", 360, 1792) == 1382;
        assert extent("2752", 640, 2752) == 2752;
        rejected("2753", 2752);
        rejected("63", 2752);
        rejected("-1", 2752);
        rejected("", 2752);
        rejected("1.5", 2752);
        rejected("2147483648", 2752);
        rejected(null, 63);
    }
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'ExtentCheck.java'
            path.write_text(harness)
            subprocess.run(['javac', str(path)], check=True, capture_output=True)
            subprocess.run(['java', '-ea', '-cp', directory, 'ExtentCheck'],
                           check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
