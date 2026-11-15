#!/usr/bin/env python3
"""Collect source-built sensor components and model firmware into private build inputs."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, shutil
p=argparse.ArgumentParser(description=__doc__)
for name in ('modules','hal','overlay','rpc','config','calibration','firmware','efi','output'):
    p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sensors',root/'scripts/full_image_sensors.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
inputs={
    **{'modules/'+n+'.ko':a.modules/(n+'.ko') for n in ('spx-tcs3430','spx-slpi-pas','fastrpc')},
    'sensors.spx.so':a.hal, 'spx-sensors-overlay.apk':a.overlay, 'hexagonrpcd':a.rpc,
    'spx-tcs3430-golden.bin':a.calibration, 'qebspilaa64.efi':a.efi, m.FIRMWARE:a.firmware,
}
for f in sorted(a.config.glob('*.json'))+[a.config/'config_list.txt']:
    inputs['config/'+f.name]=f
# Only model configuration is admitted; no registry/persist or per-unit data.
for source in inputs.values():
    if not source.is_file() or not source.stat().st_size:raise ValueError('Missing input: '+str(source))
a.output.mkdir(parents=True,exist_ok=False)
for name,source in inputs.items():
    target=a.output/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
manifest={'schema':1,'sha256':{n:hashlib.sha256((a.output/n).read_bytes()).hexdigest() for n in inputs}}
(a.output/'sensors-bundle.json').write_text(json.dumps(manifest,indent=2)+'\n')
m.load(a.output)
print(a.output.resolve())
