#!/usr/bin/env python3
"""Cross-build pinned IPTSd and its checksum-verified Meson wrap dependencies."""
from pathlib import Path
import configparser, concurrent.futures, hashlib, json, subprocess
ROOT=Path(__file__).resolve().parents[1]
B=Path('/root/.cache/sq2-probe-build')
src=B/'iptsd-3.1.1'
archive=ROOT/'downloads/iptsd-v3.1.1.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='914b2618bd1019fd3d64851aa24284d49c76d7cdf9f2db8a7ddb0ab79db95b41'
subprocess.run(['tar','-xf',str(archive),'-C',str(B)],check=True)
subprocess.run(['python3',str(ROOT/'scripts/patch-iptsd-sq2-touch.py'),str(src)],check=True)
subprocess.run(['python3',str(ROOT/'scripts/patch-iptsd-sq2-heatmap.py'),str(src)],check=True)
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
build=B/'iptsd-build-test3'
cmd=['meson','setup',str(build),str(src),'--cross-file',str(cross),'--prefix=/usr','--wrap-mode=nodownload','-Ddebug_tools=dump','-Dservice_manager=[]','-Db_lto=false']
if (build/'build.ninja').exists():cmd.append('--reconfigure')
subprocess.run(cmd,check=True)
subprocess.run(['ninja','-C',str(build),'-j8'],check=True)
dest=ROOT/'out/test2/iptsd'; dest.mkdir(exist_ok=True)
for name in ['iptsd','iptsd-check-device','iptsd-dump']:
    subprocess.run(['aarch64-linux-gnu-strip','-o',str(dest/name),str(build/'src'/name)],check=True)
subprocess.run(['qemu-aarch64',str(dest/'iptsd'),'--help'],check=True)
