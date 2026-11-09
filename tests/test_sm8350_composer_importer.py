"""The opt-in importer patch must reject unknown upstream code before writing."""
import importlib.util
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "sm8350_importer", ROOT / "scripts/build-sm8350-composer-importer.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

class ImporterSource(unittest.TestCase):
    def test_unknown_source_is_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source/composer"
            source.mkdir(parents=True)
            for name in ("QtiComposerHandleImporter.h", "QtiComposerHandleImporter.cpp"):
                (source / name).write_text("unknown source\n")
            output = base / "output"
            with self.assertRaisesRegex(ValueError, "Unrecognized"):
                builder.prepare(source.parent, output)
            self.assertFalse(output.exists())

if __name__ == "__main__":
    unittest.main()
