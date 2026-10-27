#!/usr/bin/env python3
"""Unpack verified connectivity APEX payloads for port development (no mounts)."""
from pathlib import Path
import hashlib, json, subprocess, zipfile

root=Path(__file__).resolve().parents[1]
base=root/'out/cuttlefish-connectivity'
manifest=json.loads((base/'manifest.json').read_text())
for name, record in manifest['files'].items():
    if not name.endswith('.apex'): continue
    apex=base/name
    assert hashlib.sha256(apex.read_bytes()).hexdigest()==record['sha256']
    dest=root/'out/connectivity'/apex.stem
    dest.mkdir(parents=True,exist_ok=True)
    payload=dest.with_suffix('.img')
    with zipfile.ZipFile(apex) as z: payload.write_bytes(z.read('apex_payload.img'))
    assert payload.read_bytes()[1080:1082]==b'\x53\xef'
    subprocess.run(['debugfs','-R',f'rdump / {dest}',str(payload)],check=True)
    for p in sorted(dest.rglob('*')):
        if p.is_file() and ('bin' in p.parts or p.suffix in ('.rc','.json')):
            print(p.relative_to(root))
