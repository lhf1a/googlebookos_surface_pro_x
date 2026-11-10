"""Materialize the accepted QTI DPU contract in a fresh universal-build source.

The Nov7 source archive predates caps/release-fence/UBWC/SDR integration.
Apply the existing source adaptations, not prebuilt parent modules. No content
hash gates: exact source anchors and patch context reject ambiguous inputs.
"""
from pathlib import Path
import subprocess

from disk_image import module, require

ROOT = Path(__file__).resolve().parents[1]


def prepare(external):
    tree = external / 'edp-618'
    dpu = tree / 'drivers/gpu/drm/msm/disp/dpu1'
    require(dpu.is_dir() and not dpu.is_symlink(), 'Missing explicit MSM source')
    require(not (dpu / 'dpu_caps.h').exists(), 'Display preparation requires the fresh archived source')
    caps = module('full_dpu_caps', 'patch-sq2-dpu-caps.py')
    # Validate every original site before writing any of these four files.
    changed = {name: caps.transform(name, (dpu / name).read_text())
               for name in caps.SOURCE_HASHES}
    for name, body in changed.items():
        (dpu / name).write_text(body)
    (dpu / 'dpu_caps.h').write_bytes((ROOT / 'probe/graphics/dpu-caps.h').read_bytes())
    patch = (ROOT / 'patches/sq2-dpu-output-fence.patch').read_bytes()
    subprocess.run(['patch', '--batch', '--fuzz=0', '--no-backup-if-mismatch', '-p1'],
                   cwd=tree, input=patch, check=True)
    plane = dpu / 'dpu_plane.c'
    ubwc = module('full_dpu_ubwc', 'patch-sq2-qti-display.py')
    color = module('full_dpu_color', 'patch-sq2-dpu-sdr-color.py')
    plane.write_text(color.transform(ubwc.patch(plane.read_text())))
    (dpu / 'dpu-sdr-color.h').write_bytes((ROOT / 'probe/graphics/dpu-sdr-color.h').read_bytes())
    return {'source': str(tree), 'source_only': True,
            'mechanisms': ['catalog-backed-caps', 'real-release-fence',
                           'hardware-filtered-ubwc', 'bt601-bt709-limited'],
            'prebuilt_parent_module_used': False, 'hardware_verified': False}
