#!/usr/bin/env python3
"""Offline additive USB module/export/dependency audit, not hardware validation."""
import argparse, gzip, hashlib, importlib.util, json, re, subprocess
from pathlib import Path

R = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', R/'scripts/pack-sq2-vulkan.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def audit(base, modules, symbols, output):
    entries = c.entries(gzip.decompress(base.read_bytes()))
    reference = next(b for n,(mode,b) in entries.items() if n.endswith('/ath10k_snoc.ko'))
    expected = re.search(rb'vermagic=([^\0]+)', reference).group(1).decode().strip()
    release = expected.split()[0]
    prefix = 'lib/modules/'+release+'/'
    stage = output/'module-stage'
    if stage.exists():
        raise ValueError('Use a fresh output directory (no stale modules)')
    for n,(mode,b) in entries.items():
        if n.startswith(prefix) and mode & 0o170000 == 0o100000:
            p = stage/n
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b)
    existing = {Path(n).name for n in entries if n.startswith(prefix) and n.endswith('.ko')}
    builtin = {Path(n).name for n in entries[prefix+'modules.builtin'][1].decode().splitlines()}
    new = {p.relative_to(modules).as_posix():p for p in modules.rglob('*.ko') if p.name not in existing|builtin}
    runtime = set(re.findall(r'__ksymtab_(\w+)', symbols.read_text()))
    if len(runtime) < 500 or not new:
        raise ValueError('Expected actual runtime exported symbols and new modules')
    exports = set(runtime)
    for p in new.values():
        nm = subprocess.check_output(['aarch64-linux-gnu-nm', str(p)], text=True)
        exports.update(re.findall(r'__ksymtab_(\w+)', nm))
    result = dict(base_sha256=hashlib.sha256(base.read_bytes()).hexdigest(),
                  runtime_exports=len(runtime), reference_vermagic=expected, new_modules={}, missing={})
    for n,p in sorted(new.items()):
        v = subprocess.check_output(['modinfo', '-F', 'vermagic', str(p)], text=True).strip()
        if v != expected:
            raise ValueError('vermagic mismatch: '+n)
        syms = {line.split()[-1] for line in subprocess.check_output(['aarch64-linux-gnu-nm','-u',str(p)],text=True).splitlines()}
        if syms-exports:
            result['missing'][n] = sorted(syms-exports)
        b = p.read_bytes()
        result['new_modules'][n] = dict(undefined_checked=len(syms), sha256=hashlib.sha256(b).hexdigest())
        dest = stage/prefix/n
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(b)
    (output/'module-audit.json').write_text(json.dumps(result,indent=2)+'\n')
    if result['missing']:
        raise ValueError('Unresolved runtime exports; see module-audit.json')
    p = subprocess.run(['depmod', '-b', str(stage), release], capture_output=True, text=True)
    (output/'depmod.stdout').write_text(p.stdout)
    (output/'depmod.stderr').write_text(p.stderr)
    p.check_returncode()
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('modules', type=Path)
    p.add_argument('runtime_kallsyms', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    r = audit(a.base, a.modules, a.runtime_kallsyms, a.output)
    print('AUDITED_NEW_MODULES='+str(len(r['new_modules'])))
