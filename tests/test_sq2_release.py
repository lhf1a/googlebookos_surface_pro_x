import gzip
import importlib.util
from pathlib import Path
import tempfile
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release", ROOT / "scripts/pack-sq2-release.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class Release(unittest.TestCase):
    def test_preserve_metadata_and_newc_checksum(self):
        fields = [7, 0o100750, 123, 456, 1, 99, 0, 3, 4, 5, 6, 0, 0]
        record = ("070702", fields, "")
        body = b"payload"
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "initramfs.gz"
            path.write_bytes(gzip.compress(m.encode("bin/service", body, record)))
            records, bodies = m.pack.scan(path, ("bin/service",))
            self.assertEqual(bodies["bin/service"], body)
            expected = list(fields)
            expected[6], expected[11], expected[12] = len(body), len("bin/service") + 1, sum(body)
            self.assertEqual(list(records["bin/service"][1]), expected)

    def test_remove_logger_but_keep_login_and_failure_path(self):
        before = b'''#!/bin/sh
logdir=/media/esp/logs/test
if [ "$debug" = 1 ]; then
boot-kmsg /logs/live
fi
if grep -qw "sq2.el2=1" /proc/cmdline; then
 el2-terminal-autostart 10 &
fi
# Normal-boot helper output
false || exit 7
if [ "$debug" = 1 ]; then native-monitor > /logs/monitor.txt 2>&1 & fi
'''
        after = m.clean_init(before)
        self.assertNotIn(b"boot-kmsg", after)
        self.assertNotIn(b"native-monitor", after)
        self.assertIn(b"el2-terminal-autostart 10 &", after)
        self.assertIn(b"false || exit 7", after)

    def test_release_rejects_mismatched_native_callbacks(self):
        native_spec = importlib.util.spec_from_file_location("native", ROOT / "scripts/pack-sq2-native-omx.py")
        native = importlib.util.module_from_spec(native_spec)
        native_spec.loader.exec_module(native)
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            for name, symbols in (("service", native.SERVICE_EXPORTS), ("bridge", native.BRIDGE_EXPORTS)):
                path = d / name
                source = "\n".join("int " + symbol + "(void){return 0;}" for symbol in symbols)
                subprocess.run(["cc", "-x", "c", "-shared", "-fPIC", "-o", str(path), "-"],
                               input=source, text=True, check=True)
                data = bytearray(path.read_bytes())
                data[18:20] = (183).to_bytes(2, "little")
                path.write_bytes(data)
            bridge = d / "bridge"
            bridge.write_bytes(bridge.read_bytes() + native.LAYOUT_V2.encode() + b"\0")
            with self.assertRaisesRegex(ValueError, "Missing defined native API exports"):
                m.validate_native_pair((d / "service").read_bytes(), bridge)
            bridge.write_bytes(bridge.read_bytes() + b"quest2_omx_capture_layout_for_usage\0")
            self.assertEqual(m.validate_native_pair((d / "service").read_bytes(), bridge), bridge.read_bytes())

    def test_unknown_init_rejected(self):
        with self.assertRaises(ValueError):
            m.clean_init(b"#!/bin/sh\nexit 0\n")
