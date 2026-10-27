#!/usr/bin/env python3
"""Fetch ARM64 Ubuntu packages from official HTTPS indexes; verify package SHA256."""
from pathlib import Path
import hashlib,json,lzma,urllib.request,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def fetch(url):
    # WSL's outbound TLS is unreliable on this host; Windows curl uses the working route.
    path=ROOT/'downloads/test2-package-transfer.tmp'
    winpath=subprocess.check_output(['wslpath','-w',str(path)],text=True).strip()
    subprocess.run(['/mnt/c/Windows/System32/curl.exe','-fsSL','--retry','3','--max-time','90','-o',winpath,url],check=True)
    return path.read_bytes()
base='https://ports.ubuntu.com/ubuntu-ports/'
wanted={'libudev1','libudev-dev','liblzma5','liblzma-dev','libcap2','libc6'}
packages={}
for suite in ['noble','noble-updates']:
    url=base+f'dists/{suite}/main/binary-arm64/Packages.xz'
    data=lzma.decompress(fetch(url)).decode()
    for entry in data.split('\n\n'):
        fields={line.split(': ',1)[0]:line.split(': ',1)[1] for line in entry.splitlines() if ': ' in line and not line.startswith(' ')}
        if fields.get('Package') in wanted: packages[fields['Package']]=fields
assert set(packages)==wanted
dest=ROOT/'downloads/test2-arm64-libs';dest.mkdir(exist_ok=True)
stage=Path('/root/.cache/sq2-probe-build/tools-test2/arm64-libs')
if sys.platform!='win32':stage.mkdir(exist_ok=True)
manifest=[]
for name,p in packages.items():
    path=dest/Path(p['Filename']).name
    if not path.exists():path.write_bytes(fetch(base+p['Filename']))
    assert hashlib.sha256(path.read_bytes()).hexdigest()==p['SHA256']
    if sys.platform!='win32':subprocess.run(['dpkg-deb','-x',str(path),str(stage)],check=True)
    manifest.append({'name':name,'version':p['Version'],'url':base+p['Filename'],'file':path.name,'sha256':p['SHA256']})
(ROOT/'out/test2/arm64-libs-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Verified and extracted',len(manifest),'ARM64 packages')
