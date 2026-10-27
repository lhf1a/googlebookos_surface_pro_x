#!/usr/bin/env python3
"""Prepare a development-only Freedreno/minigbm/MSM vendor RAM overlay."""
from pathlib import Path
import hashlib
import json
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]
B = Path('/root/.cache/sq2-probe-build')
OUT = Path(tempfile.mkdtemp(prefix='googlebook-graphics.', dir=B))
OUT.chmod(0o755)
donor = ROOT / 'out/cuttlefish-graphics'
manifest = json.loads((donor/'manifest.json').read_text())
for rel, item in manifest['files'].items():
    assert hashlib.sha256((donor/rel).read_bytes()).hexdigest() == item['sha256']

def put(rel, content, mode=0o644):
    p=OUT/rel
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_bytes(content if isinstance(content,bytes) else content.encode())
    p.chmod(mode)

for rel in manifest['files']:
    # The generic gralloc0 donor is ashmem-based, not minigbm; use mapper5.
    if rel.endswith('gralloc.default.so') or rel.endswith('/libminigbm_gralloc.so'):
        continue
    put(rel,(donor/rel).read_bytes(),0o755 if rel.startswith('bin/') else 0o644)
for name in ('libminigbm_gralloc.so','libminigbm_gralloc_donor.so'):
    put('lib64/'+name,(ROOT/'out/sq2-graphics/minigbm'/name).read_bytes())
for p in (ROOT/'out/sq2-graphics/runtime').glob('*.so'):
    dest='lib64/egl/' if p.name.startswith(('libEGL_','libGLES')) else 'lib64/'
    put(dest+p.name,p.read_bytes())
put('bin/sq2-gles-readback',(ROOT/'out/sq2-graphics/runtime/gles-readback').read_bytes(),0o755)
put('bin/sq2-yuv-readback',(ROOT/'out/sq2-graphics/runtime/yuv-readback').read_bytes(),0o755)
put('bin/sq2-ahb-readback',(ROOT/'out/sq2-graphics/ahb-readback').read_bytes(),0o755)
put('etc/init/vendor.qti.hardware.display.allocator-service.rc','''service vendor.graphics.allocator /vendor/bin/hw/android.hardware.graphics.allocator-service.minigbm
    class hal animation
    user system
    seclabel u:r:hal_graphics_allocator_default:s0
    group graphics drmrpc
    capabilities SYS_NICE
    onrestart restart surfaceflinger
    task_profiles ServiceCapacityLow
''')
composer=(B/'googlebook/vendor/etc/init/hwc3-drm.rc').read_text()
assert '    user system\n' in composer
put('etc/init/hwc3-drm.rc',composer.replace('    user system\n','    user system\n    seclabel u:r:hal_graphics_composer_default:s0\n'))
put('etc/vintf/manifest/vendor.qti.hardware.display.allocator-service.xml','<manifest version="9.0" type="device"><hal format="aidl"><name>android.hardware.graphics.allocator</name><version>3</version><fqname>IAllocator/default</fqname></hal></manifest>\n')
put('etc/vintf/manifest/mapper.qti.xml','<manifest version="9.0" type="device"><hal format="native"><name>mapper</name><fqname>@5.0/minigbm</fqname></hal></manifest>\n')
props={'ro.hardware.egl':'freedreno','ro.hardware.vulkan':'none',
       'vendor.minigbm.debug':'nocompression',
       'ro.hwui.use_vulkan':'false','debug.renderengine.backend':'skiaglthreaded',
       'debug.renderengine.vulkan':'false','debug.renderengine.graphite_desktop_optin':'false',
       'debug.hwui.renderer':'skiagl','ro.gfx.angle.supported':'false',
       'ro.vendor.hwcomposer.mode':'client','ro.vendor.hwc.drop_drm_master':'0',
       'ro.surface_flinger.has_wide_color_display':'false','ro.surface_flinger.has_HDR_display':'false'}
text=(B/'googlebook/vendor/build.prop').read_text()
lines=[l for l in text.splitlines() if l.split('=',1)[0] not in props]
put('build.prop','\n'.join(lines+['# SQ2 graphics development: MSM DRM, Mesa GLES, minigbm.']+[k+'='+v for k,v in props.items()])+'\n')
(ROOT/'out/android-test/graphics-overlay-path.txt').write_text(str(OUT)+'\n')
result={'development_only':True,'driver':'Mesa Freedreno MSM','minigbm':'rebuilt C core with donor C++ HAL bridge',
        'mapper5':'gbos-vm helper with SQ2 mapper-local import and bounded multi-plane metadata','vulkan_enabled':False,
        'files':{str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.rglob('*')) if p.is_file()}}
(ROOT/'out/android-test/graphics-overlay-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
print('Prepared graphics RAM overlay:',OUT)
