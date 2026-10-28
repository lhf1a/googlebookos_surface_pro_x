#!/usr/bin/env python3
"""Restore verified original module dependency metadata after no-symvers build.

No CRCs or symbol ABI data are fabricated. The function/code sections are
unchanged. BusyBox's module loader needs .modinfo depends even with modules.dep.
"""
import hashlib, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'out/display-refresh'
original=out/'msm-original.ko'
module=out/'msm-edp-rates.ko'
assert hashlib.sha256(original.read_bytes()).hexdigest()=='f69c91e754e2a0d66cd195549d970973ed81b4c6f1bbfcbc3723f80a26414633'
def run(*args):
 return subprocess.check_output(args)
def undefined(path):
 return {line.split()[-1] for line in run('aarch64-linux-gnu-nm','-u',str(path)).decode().splitlines()}
extra=undefined(module)-undefined(original)
assert extra <= {'of_machine_is_compatible'}, extra
assert run('modinfo','-F','vermagic',str(module))==run('modinfo','-F','vermagic',str(original))
deps=run('modinfo','-F','depends',str(original)).strip()
assert deps==b'gpu-sched,llcc-qcom,mdt_loader'
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory)
 run('aarch64-linux-gnu-objcopy','--dump-section','.modinfo='+str(d/'before'),str(module))
 info=(d/'before').read_bytes()
 assert info.split(b'\0').count(b'depends=')==1
 (d/'after').write_bytes(info.replace(b'depends=\0',b'depends='+deps+b'\0'))
 run('aarch64-linux-gnu-objcopy','--dump-section','.text='+str(d/'text-before'),str(module))
 fixed=d/'fixed.ko'
 run('aarch64-linux-gnu-objcopy','--update-section','.modinfo='+str(d/'after'),str(module),str(fixed))
 run('aarch64-linux-gnu-objcopy','--dump-section','.text='+str(d/'text-after'),str(fixed))
 assert (d/'text-before').read_bytes()==(d/'text-after').read_bytes()
 assert undefined(fixed)==undefined(module)
 assert run('modinfo','-F','depends',str(fixed)).strip()==deps
 module.write_bytes(fixed.read_bytes())
print('Restored original depends; unchanged code/imports; extra built-in import:',sorted(extra))
print(hashlib.sha256(module.read_bytes()).hexdigest())
