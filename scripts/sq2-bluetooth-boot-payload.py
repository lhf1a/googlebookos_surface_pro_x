"""Build the small HCI bridge from current source; no hardware operations."""
import hashlib
from pathlib import Path
import subprocess
import tempfile


def build_bridge(root):
    source = Path(root)/'probe/test2/sq2-hci-bridge.c'
    body = source.read_bytes().replace(b'\r\n', b'\n')
    with tempfile.TemporaryDirectory(prefix='sq2-bt-bridge-') as td:
        work = Path(td)
        (work/'sq2-hci-bridge.c').write_bytes(body)
        subprocess.run(['aarch64-linux-gnu-gcc', '-static', '-O2', '-Wall',
                        '-Wextra', '-Werror', str(work/'sq2-hci-bridge.c'), '-o',
                        str(work/'bridge')], check=True)
        binary = (work/'bridge').read_bytes()
    # ELF64, little endian, AArch64. Never silently inherit an old base binary.
    if binary[:6] != b'\x7fELF\x02\x01' or binary[18:20] != b'\xb7\x00':
        raise ValueError('Expected AArch64 ELF bridge')
    sha = lambda data: hashlib.sha256(data).hexdigest()
    return binary, dict(source_sha256=sha(body), binary_sha256=sha(binary))
