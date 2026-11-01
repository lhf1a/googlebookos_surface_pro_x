"""Check all pinned PAS wrappers retain status and expose initialized payloads."""
from pathlib import Path
import tempfile
from types import SimpleNamespace

root = Path(__file__).resolve().parents[1]
script = (root / 'scripts/prepare-sq2-el2-preboot.py').read_text()
block = script[script.index('# shortcut: PAS'):script.index("edit('src/pil.c', '#include")]
with tempfile.TemporaryDirectory() as directory:
    output = Path(directory)
    (output / 'src').mkdir()
    target = output / 'src/scm.c'
    target.write_text((root / 'upstream/qebspil/src/scm.c').read_text())
    def edit(name, old, new):
        path = output / name
        text = path.read_text()
        assert text.count(old) == 1
        path.write_text(text.replace(old, new))
    exec(block, {'a': SimpleNamespace(output=output), 'edit': edit})
    result = target.read_text()
    assert result.count('transport=%r raw=%lx/%lx/%lx') == 4
    assert result.count('= {~(UINT64)0, ~(UINT64)0, ~(UINT64)0}') == 4
    assert result.count('return status;') == 5
    assert result.count('CopyMem(sq2_pas_result, Results, sizeof(sq2_pas_result))') == 4
    assert '11, status, sq2_pas_result);' in script
    assert 'return scm->ScmSipSysCall' not in result
print('PAS logging: four wrappers checked')
