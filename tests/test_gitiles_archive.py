import io
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from gitiles_archive import archive_tree


def archive(entries):
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w") as tar:
        for name, kind, content in entries:
            item = tarfile.TarInfo(name)
            if kind == "link":
                item.type = tarfile.SYMTYPE
                item.linkname = content
            elif kind == "hardlink":
                item.type = tarfile.LNKTYPE
                item.linkname = content
            elif kind == "dir":
                item.type = tarfile.DIRTYPE
            else:
                item.mode = 0o755 if kind == "exec" else 0o644
                item.size = len(content)
            tar.addfile(item, io.BytesIO(content) if item.isfile() else None)
    data.seek(0)
    return tarfile.open(fileobj=data)


class GitilesTreeTest(unittest.TestCase):
    def test_git_oracle_modes_links_order(self):
        entries = [("z", "file", b"hello"), ("a.c", "exec", b"run"),
                   ("a/inner", "file", b"nested"), ("link", "link", "a/inner")]
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            subprocess.run(["git", "init", "-q", work], check=True)
            for name, kind, content in entries:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                if kind == "link": path.symlink_to(content)
                else:
                    path.write_bytes(content)
                    path.chmod(0o755 if kind == "exec" else 0o644)
            subprocess.run(["git", "-C", work, "add", "."], check=True)
            expected = subprocess.check_output(["git", "-C", work, "write-tree"], text=True).strip()
            for order in (entries, list(reversed(entries))):
                with archive(order) as tar: self.assertEqual(archive_tree(tar), expected)

    def test_content_and_mode_changes(self):
        def digest(kind, data):
            with archive([("file", kind, data)]) as tar: return archive_tree(tar)
        self.assertNotEqual(digest("file", b"x"), digest("file", b"y"))
        self.assertNotEqual(digest("file", b"x"), digest("exec", b"x"))

    def test_unsafe_duplicate_and_conflicts(self):
        cases = [[(n, "file", b"x")] for n in ("../escape", "/abs", "a/../b", "./a", "a//b")]
        cases += [[("a", "file", b"x"), ("a", "file", b"y")],
                  [("a", "file", b"x"), ("a/b", "file", b"y")],
                  [("a/b", "file", b"x"), ("a", "file", b"y")],
                  [("a", "hardlink", "b")]]
        for entries in cases:
            with self.subTest(entries=entries), archive(entries) as tar:
                with self.assertRaises(ValueError): archive_tree(tar)

    def test_explicit_directories(self):
        with archive([("a/b", "file", b"x"), ("a/", "dir", b"")]) as tar:
            with archive([("a/b", "file", b"x")]) as other:
                self.assertEqual(archive_tree(tar), archive_tree(other))
