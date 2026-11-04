"""Normal Android-owned suspend payload; retain the separate screen-only fallback."""
from pathlib import Path
import gzip, hashlib, json, re, struct, zlib, importlib.util

def digest(data):
    return hashlib.sha256(data).hexdigest()

def bundle_files(bundle):
    bundle=Path(bundle)
    manifest=json.loads((bundle/'power-bundle.json').read_text())
    if manifest.get('policy')!='android-persistent-owner':
        raise ValueError('Persistent Android owner kernel bundle required (not a single-request diagnostic)')
    names=('Image','native.efi','spi-hid.ko','system-suspend.sha256')
    files={n:(bundle/n).read_bytes() for n in names}
    for n,b in files.items():
        if not b or digest(b)!=manifest['sha256'].get(n):raise ValueError('Power bundle integrity: '+n)
    if not re.fullmatch(rb'[0-9a-f]{64}\n',files['system-suspend.sha256']):raise ValueError('Invalid native service manifest')
    return files

def integrate(raw, root, bundle, c, policy="android"):
    if policy not in ("android","unrestricted"):raise ValueError("Invalid suspend policy")
    f=bundle_files(bundle); before=c.entries(gzip.decompress(raw));e=dict(before)
    modules=[n for n in e if n.endswith('/spi-hid.ko')]
    if len(modules)!=1:raise ValueError('One SPI-HID module required')
    module=modules[0]
    def vermagic(b):
        m=re.search(rb'\x00vermagic=([^\x00]+)\x00',b)
        if not b.startswith(b'\x7fELF') or not m:raise ValueError('Invalid SPI module')
        return m[1]
    if vermagic(f['spi-hid.ko'])!=vermagic(e[module][1]):raise ValueError('SPI module vermagic mismatch')
    if b'resume_count' not in f['spi-hid.ko']:raise ValueError('SPI PM callbacks missing')
    # Refuse diagnostic frameworks/modules as the normal runtime baseline.
    if 'etc/surface-pm/desktop-services.jar' in e:raise ValueError('Use the original runtime, not a patched diagnostic JAR')
    ssam=[b for n,(mode,b) in e.items() if n.endswith('/surface_aggregator.ko')]
    if len(ssam)!=1 or b'PM_GRACE_DIAGNOSTIC' in ssam[0]:raise ValueError('Diagnostic SSAM delay must not enter normal policy')
    e[module]=(e[module][0],f['spi-hid.ko'])
    for name in ('lab-keep-awake','framework-suspend-start','lockscreen-framework-boot'):
        e['bin/'+name]=(0o100755,(Path(root)/'probe/test2'/name).read_bytes().replace(b'\r\n',b'\n'))
    e['etc/surface-pm']=(0o40755,b'')
    e['etc/surface-pm/system-suspend.sha256']=(0o100644,f['system-suspend.sha256'])
    mode,init=e['init']; anchor=b'lab-keep-awake > /logs/lab-keep-awake.txt 2>&1 &\n'
    hook=b'framework-suspend-start > /logs/framework-suspend-start.txt 2>&1 &\n'
    if init.count(anchor)!=1 or init.count(hook)>1:raise ValueError('Unexpected power boot hook')
    if policy=='unrestricted':
        # No registration/keep-awake processes are needed when the kernel gate is off.
        init=init.replace(hook,b'').replace(anchor,b'')
    else:
        if hook in init:raise ValueError('Power boot hook already present')
        init=init.replace(anchor,anchor+hook)
    e['init']=(mode,init)
    unrestricted_changes=[]
    if policy=='unrestricted':
        spec=importlib.util.spec_from_file_location('unrestricted_runtime',Path(root)/'scripts/unrestricted-power-runtime.py')
        runtime=importlib.util.module_from_spec(spec);spec.loader.exec_module(runtime)
        e,unrestricted_changes=runtime.integrate_entries(e)
        e,manifest_changes=runtime.refresh_manifests(before,e)
        unrestricted_changes=sorted(set(unrestricted_changes+manifest_changes))
    rest=raw;identity=None
    while rest:
        d=zlib.decompressobj(31);entries=c.entries(d.decompress(rest))
        if not d.eof:raise ValueError('Truncated gzip')
        member=rest[:len(rest)-len(d.unused_data)]
        if 'etc/shadow' in entries and 'bin/dropbear' in entries:identity=member
        rest=d.unused_data
    if identity is None:raise ValueError('Private identity member missing')
    body=b''.join(c.entry(n,b,m) for n,(m,b) in sorted(e.items(),key=lambda x:(x[0].count('/'),x[0])))+c.entry('TRAILER!!!',b'',0)
    body+=b'\0'*(-len(body)%512)
    result=gzip.compress(body,compresslevel=3,mtime=0)+identity
    if c.entries(gzip.decompress(result))!=e:raise ValueError('Effective payload mismatch')
    return result,dict(policy=('unrestricted' if policy=='unrestricted' else 'android-persistent-owner'),
        screen_only_enabled=(policy!='unrestricted'),owner_gate=(policy!='unrestricted'),
        request_budget=None,admission_deadline=None,unrestricted_helpers=unrestricted_changes,changed=sorted(n for n in e if e[n]!=before.get(n)),
        spi_sha256=digest(f['spi-hid.ko']),kernel_sha256=digest(f['Image']),credential_member_last=True,
        framework='stock',ssam_diagnostic_delay=False,hardware_tested=False)

def launcher(bundle, machine, directory=None, initrd='initramfs.cpio.gz', dt='android-el2.dtb', policy='android', base_initrd=None, initrd_addons=()):
    if policy not in ('android','unrestricted'):raise ValueError('Invalid suspend policy')
    if (base_initrd or initrd_addons) and policy!='unrestricted':raise ValueError('Layered initrd is an explicit unrestricted profile')
    for path in ([base_initrd] if base_initrd else [])+list(initrd_addons):
        if not re.fullmatch(r'\\EFI\\[A-Za-z0-9_.-]+\\[A-Za-z0-9_.-]+',path) or any(x in ('.','..') for x in path.split('\\')):
            raise ValueError('Invalid layered EFI path')
    raw=bundle_files(bundle)['native.efi']
    pe=struct.unpack_from('<I',raw,0x3c)[0]; opt=pe+24
    if raw[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',raw,pe+4)[0]!=0xaa64:raise ValueError('ARM64 PE required')
    if struct.unpack_from('<II',raw,opt+112+8*4)!=(0,0):raise ValueError('Refuse signed launcher')
    if struct.unpack_from('<I',raw,opt+64)[0]!=0:raise ValueError('Unexpected PE checksum')
    target=directory or ('SQ1Default' if machine=='sq1' else 'SQ2Default')
    if not all(re.fullmatch(r'[A-Za-z0-9_.-]+',s) for s in (target,initrd,dt)):raise ValueError('Invalid EFI path')
    # Replace complete UTF-16 strings in place, never grow a PE section.
    strings=[];seen=0
    for m in re.finditer(rb'(?:[\x20-\x7e]\x00){8,}\x00\x00',raw):
        text=m[0][:-2].decode('utf-16le');new=text
        if text==r'\EFI\SurfacePmAuto\Image':
            seen+=1;new='\\EFI\\'+target+'\\Image'
        elif text.startswith('console='):
            seen+=1
            if 'suspend.screen_only_autosuspend=1' not in text or 'nohibernate' not in text:raise ValueError('Native policy absent')
            new=text.replace(r'\EFI\SurfacePmAuto\initramfs.cpio.gz','\\EFI\\'+target+'\\'+initrd)
            if policy=='unrestricted':
                new=new.replace('suspend.screen_only=1','suspend.screen_only=0')
                new=new.replace('suspend.screen_only_autosuspend=1','suspend.screen_only_autosuspend=0')
                new=re.sub(r'\bpanic=\S+','panic=0',new)
                if base_initrd or initrd_addons:
                    fields=re.findall(r'\binitrd=\S+',new)
                    if len(fields)!=1:raise ValueError('One primary initrd required')
                    if base_initrd:new=new.replace(fields[0],'initrd='+base_initrd)
                    new=new.rstrip()+''.join(' initrd='+x for x in initrd_addons)
                if any(t.startswith(('suspend.screen_only_test=','suspend.screen_only_trial=')) and t.rsplit('=',1)[1] not in ('0','N') for t in new.split()):
                    raise ValueError('Diagnostic sleep option in unrestricted launcher')
        elif text.startswith('\\EFI\\SQ2Default\\'):
            seen+=1
            new=text.replace('SQ2Default',target).replace('android-ip4.dtb',dt)
        if new!=text:
            b=new.encode('utf-16le')+b'\0\0'
            if len(b)>len(m[0]):raise ValueError('EFI string cannot grow')
            strings.append((m.start(),m.end(),b+b'\0'*(len(m[0])-len(b))))
    if seen!=5:raise ValueError('Unexpected native launcher paths')
    result=bytearray(raw)
    for start,end,b in strings:result[start:end]=b
    return bytes(result)
