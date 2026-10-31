#!/usr/bin/env python3
"""Cross-build pinned IPTSd and its checksum-verified Meson wrap dependencies."""
from pathlib import Path
import argparse, configparser, concurrent.futures, hashlib, json, subprocess, tempfile, time
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ghost-touch-fix', choices=('on', 'off', 'edge-only'), default='on',
                    help='on (default): continuity and heatmap repair; off: neither; edge-only: heatmap repair without continuity')
parser.add_argument('--plan-json', action='store_true', help='Print selection without building')
parser.add_argument('--touch-calibration', choices=('on', 'off'), default='on',
                    help='on (default): measured SQ2 spatial calibration; off: original coordinates')
args = parser.parse_args()
ghost_patches = {'on': ['patch-iptsd-sq2-touch.py', 'patch-iptsd-sq2-heatmap.py'],
           'off': [], 'edge-only': ['patch-iptsd-sq2-heatmap.py']}[args.ghost_touch_fix]
patches = ghost_patches + (['patch-iptsd-sq2-calibration.py'] if args.touch_calibration == 'on' else [])
if args.plan_json:
    print(json.dumps({'ghost_touch_fix': args.ghost_touch_fix, 'touch_calibration': args.touch_calibration, 'patches': patches}))
    raise SystemExit(0)
ROOT=Path(__file__).resolve().parents[1]
B=Path('/root/.cache/sq2-probe-build')
# Fresh source prevents an off build inheriting patches from a previous on build.
workspace = tempfile.TemporaryDirectory(prefix='iptsd-build-', dir=B)
stage = Path(workspace.name)
src=stage/'iptsd-3.1.1'
archive=ROOT/'downloads/iptsd-v3.1.1.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='914b2618bd1019fd3d64851aa24284d49c76d7cdf9f2db8a7ddb0ab79db95b41'
subprocess.run(['tar','-xf',str(archive),'-C',str(stage)],check=True)
touch_header = src/'src/apps/daemon/touch.hpp'
original_touch = touch_header.read_bytes()
for patch in ghost_patches:
    subprocess.run(['python3',str(ROOT/'scripts'/patch),str(src)],check=True)
if args.ghost_touch_fix != 'on':
    assert touch_header.read_bytes() == original_touch, 'Unexpected continuity changes'
if args.touch_calibration == 'on':
    subprocess.run(['python3',str(ROOT/'scripts/patch-iptsd-sq2-calibration.py'),str(src)],check=True)
cache=src/'subprojects/packagecache'; cache.mkdir(exist_ok=True)
downloads=ROOT/'downloads/test3-iptsd'; downloads.mkdir(exist_ok=True)
jobs=[]
for name in ['cli11','eigen','fmt','inih','microsoft-gsl','spdlog']:
    c=configparser.ConfigParser(); c.read(src/f'subprojects/{name}.wrap'); w=c['wrap-file']
    for kind in ['source','patch']:
        if kind+'_url' in w: jobs.append((w[kind+'_url'],w[kind+'_filename'],w[kind+'_hash']))
def fetch(item):
    url,name,digest=item; dst=downloads/name
    if not dst.exists() or hashlib.sha256(dst.read_bytes()).hexdigest()!=digest:
        win=subprocess.check_output(['wslpath','-w',str(dst)],text=True).strip()
        subprocess.run(['/mnt/c/Windows/System32/curl.exe','-fLsS','--retry','2','--max-time','90','-o',win,url],check=True)
    assert hashlib.sha256(dst.read_bytes()).hexdigest()==digest,name
    (cache/name).write_bytes(dst.read_bytes())
    return {'url':url,'file':name,'sha256':digest}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool: records=list(pool.map(fetch,jobs))
(ROOT/'out/test2/iptsd-sources.json').write_text(json.dumps(records,indent=2)+'\n')
cross=B/'iptsd-cross.ini'
cross.write_text("""[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'
pkgconfig = 'aarch64-linux-gnu-pkg-config'
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[built-in options]
cpp_link_args = ['-static']
""")
build=stage/'build'
cmd=['meson','setup',str(build),str(src),'--cross-file',str(cross),'--prefix=/usr','--wrap-mode=nodownload','-Ddebug_tools=dump','-Dservice_manager=[]','-Db_lto=false']
if (build/'build.ninja').exists():cmd.append('--reconfigure')
setup = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
print(setup.stdout, end='', flush=True)
if setup.returncode and 'Clock skew detected.' in setup.stdout:
    # Zip patch extraction can round mtimes a second ahead on this WSL host.
    # Retry only this explicit Meson condition, in the same extracted tree.
    time.sleep(3)
    retry = cmd + (['--reconfigure'] if (build/'meson-private/coredata.dat').exists() and '--reconfigure' not in cmd else [])
    subprocess.run(retry, check=True)
else:
    setup.check_returncode()
subprocess.run(['ninja','-C',str(build),'-j8'],check=True)
dest=ROOT/'out/test2'/{'on':'iptsd','off':'iptsd-no-ghost-fix','edge-only':'iptsd-edge-only'}[args.ghost_touch_fix]
dest.mkdir(parents=True, exist_ok=True)
for name in ['iptsd','iptsd-check-device','iptsd-dump']:
    subprocess.run(['aarch64-linux-gnu-strip','-o',str(dest/name),str(build/'src'/name)],check=True)
subprocess.run(['qemu-aarch64',str(dest/'iptsd'),'--help'],check=True)
config = (ROOT/'configs/iptsd-sq2.conf').read_text()
if args.ghost_touch_fix == 'off':
    config = '[DFT]\nButtonMinMag = 4000\n'
if args.touch_calibration == 'on':
    config += '\n' + (ROOT/'configs/iptsd-sq2-calibration.conf').read_text()
(dest/'iptsd.conf').write_text(config)
(dest/'build-options.json').write_text(json.dumps({
    'ghost_touch_fix': args.ghost_touch_fix, 'patches': patches,
    'touch_calibration': args.touch_calibration,
    'calibration_header_sha256': (hashlib.sha256((ROOT/'probe/test2/sq2-touch-calibration.hpp').read_bytes().replace(b'\r\n',b'\n')).hexdigest()
                                 if args.touch_calibration == 'on' else None),
    'touch_header_sha256': hashlib.sha256(touch_header.read_bytes()).hexdigest(),
    'stock_touch_header_sha256': hashlib.sha256(original_touch).hexdigest(),
    'patch_sha256': {n: hashlib.sha256((ROOT/'scripts'/n).read_bytes().replace(b'\r\n', b'\n')).hexdigest()
                     for n in patches},
    'sha256': {n: hashlib.sha256((dest/n).read_bytes()).hexdigest()
               for n in ('iptsd', 'iptsd.conf')}
}, indent=2)+'\n')
