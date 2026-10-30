#!/usr/bin/env python3
"""Build a temporary clock-only OF overlay module; no installation."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import libfdt

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gpu', ROOT/'scripts/build-sq2-gpu-opp.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)
raw = (ROOT/'out/video-decode/sq2-video-clock.dtb').read_bytes()
assert hashlib.sha256(raw).hexdigest() == '59966cdf5aacfddc2f9023d42285106bf8e339c67218ca9673458e5f1cdbcf41'
tree = h.snapshot(libfdt.Fdt(raw))
props = tree['/soc@0/clock-controller@ab00000']
f = libfdt.Fdt.create_empty_tree(4096)
f.add_subnode(0, 'fragment@0')
f.setprop(f.path_offset('/fragment@0'), 'target-path', b'/soc@0\0')
f.add_subnode(f.path_offset('/fragment@0'), '__overlay__')
f.add_subnode(f.path_offset('/fragment@0/__overlay__'), 'clock-controller@ab00000')
for k,v in props.items():
    f.setprop(f.path_offset('/fragment@0/__overlay__/clock-controller@ab00000'), k,v)
f.pack()
stage = Path('/root/.cache/sq2-video-clock-overlay')
stage.mkdir(exist_ok=True)
blob = bytes(f.as_bytearray())
(stage/'overlay.h').write_text('static const unsigned char overlay[] = {' + ','.join(str(b) for b in blob) + '};\n')
checks = []
for key in ('clocks','power-domains','required-opps'):
    ph = struct.unpack('>I', props[key][:4])[0]
    paths = [p for p,v in tree.items() if v.get('phandle') == struct.pack('>I', ph)]
    assert len(paths) == 1
    checks.append(f'if (!check_ref("{paths[0]}", {ph})) return -EINVAL;')
source = '''#include <linux/module.h>
#include <linux/of.h>
#include "overlay.h"
static int id = -1;
static bool allow;
module_param(allow, bool, 0400);
static bool check_ref(const char *path, u32 expected) {
 struct device_node *n = of_find_node_by_path(path);
 bool ok = n && n->phandle == expected;
 of_node_put(n); return ok;
}
static int __init start(void) {
 struct device_node *n;
 int ret;
 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x")) return -EPERM;
 n = of_find_node_by_path("/soc@0/clock-controller@ab00000");
 if (n) { of_node_put(n); return -EEXIST; }
 CHECKS
 ret = of_overlay_fdt_apply(overlay, sizeof(overlay), &id);
 pr_info("SQ2_VIDEO_CLOCK_OVERLAY ret=%d id=%d\\n", ret, id);
 return ret;
}
static void __exit stop(void) {
 if (id >= 0) pr_info("SQ2_VIDEO_CLOCK_OVERLAY remove=%d\\n", of_overlay_remove(&id));
}
module_init(start); module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SQ2 temporary VIDEOCC overlay, not a video decoder");
'''.replace('CHECKS', '\n'.join(checks))
(stage/'sq2_video_clock_overlay.c').write_text(source)
(stage/'Makefile').write_text('obj-m += sq2_video_clock_overlay.o\n')
subprocess.run(['make','-s','-C','/root/.cache/sq2-thermal/kernel',
    'O=/root/.cache/sq2-video-foundation/build','ARCH=arm64',
    'CROSS_COMPILE=aarch64-linux-gnu-',f'M={stage}','KBUILD_MODPOST_WARN=1','modules'],check=True)
dest = ROOT/'out/video-decode/sq2_video_clock_overlay.ko'
dest.write_bytes((stage/dest.name).read_bytes())
print(hashlib.sha256(dest.read_bytes()).hexdigest(), dest)
