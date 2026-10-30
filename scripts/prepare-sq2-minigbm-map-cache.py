"""Prepare a separate pinned source candidate; never modify/install live graphics."""
from pathlib import Path
import argparse, hashlib, shutil, subprocess
ap=argparse.ArgumentParser()
ap.add_argument("--p010-scanout",action="store_true")
a=ap.parse_args()
root=Path(__file__).resolve().parents[1]
src=Path('/root/.cache/sq2-video-minigbm-reference')
dst=Path('/root/.cache/sq2-video-minigbm-p010' if a.p010_scanout else '/root/.cache/sq2-video-minigbm-map-cache')
revision='455b7456397dff01443584b09e2fd594079e905c'
if subprocess.check_output(['git','-C',str(src),'rev-parse','HEAD'],text=True).strip()!=revision:
    raise SystemExit('minigbm revision mismatch')
if hashlib.sha256((src/'drv.c').read_bytes()).hexdigest()!='6655bfd5e9fa2760601bf3acdae8cfef184dae383a4afb29eaf08813e6a65a60':
    raise SystemExit('drv.c mismatch')
if dst.exists(): raise SystemExit('Refusing existing candidate')
shutil.copytree(src,dst,ignore=shutil.ignore_patterns('.git'))
header=(dst/'drv.h').read_text()
old='struct mapping {\n\tstruct vma *vma;\n\tstruct rectangle rect;\n\tuint32_t refcount;\n};'
if header.count(old)!=1: raise SystemExit('mapping ABI context mismatch')
# Existing public field offsets remain unchanged; the C driver owns allocation.
(dst/'drv.h').write_text(header.replace(old,old.replace('\tuint32_t refcount;', '\tuint32_t refcount;\n\tbool sq2_cached;')))
shutil.copyfile(root/'probe/video/sq2-minigbm-map-cache.h',dst/'sq2-minigbm-map-cache.h')
code=(dst/'drv.c').read_text()
marker='int drv_bo_flush_or_unmap(struct bo *bo, struct mapping *mapping)'
assert code.count(marker)==1
code=code.replace(marker,'#include "sq2-minigbm-map-cache.h"\n\n'+marker)
needle='\tif (bo->drv->backend->bo_flush)\n\t\tret = bo->drv->backend->bo_flush(bo, mapping);\n\telse\n\t\tret = drv_bo_unmap(bo, mapping);'
assert code.count(needle)==1
code=code.replace(needle,'\tpthread_mutex_lock(&bo->drv->mappings_lock);\n\tsq2_cache_mapping(bo, mapping);\n\tpthread_mutex_unlock(&bo->drv->mappings_lock);\n\n'+needle)
(dst/'drv.c').write_text(code)
if a.p010_scanout:
    msm=(dst/'msm.c').read_text()
    if hashlib.sha256((src/'msm.c').read_bytes()).hexdigest()!='991257af464f0c52b4aa8957365b648b14bf80c76e7b1468ded09c2dd91e6882':
        raise SystemExit('msm.c mismatch')
    shutil.copyfile(root/'probe/video/sq2-minigbm-p010.h',dst/'sq2-minigbm-p010.h')
    marker='static int msm_init(struct driver *drv)'
    assert msm.count(marker)==1
    msm=msm.replace(marker,'#include "sq2-minigbm-p010.h"\n\n'+marker)
    marker='\tdrv_modify_linear_combinations(drv);'
    assert msm.count(marker)==1
    msm=msm.replace(marker,'\tif (sq2_p010_scanout_chip(drv->fd))\n'
        '\t\tdrv_modify_combination(drv, DRM_FORMAT_P010, &LINEAR_METADATA, BO_USE_SCANOUT);\n\n'+marker)
    (dst/'msm.c').write_text(msm)
print(dst)
