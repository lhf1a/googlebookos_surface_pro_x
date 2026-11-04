"""Remove screen_only admission coupling, not hardware/integrity validation."""
from pathlib import PurePosixPath
import re,hashlib,posixpath

def transform(name, data):
    try: text=data.decode('utf-8')
    except UnicodeDecodeError: return data
    if '\x00' in text: return data
    base=PurePosixPath(name).name
    special={'framework-suspend-start','framework-suspend-stage','lab-keep-awake'}
    if base in special:
        anchor='export PATH=/bin:/sbin\n'
        if anchor not in text: raise ValueError('Missing helper PATH: '+name)
        tag={'framework-suspend-start':'UNRESTRICTED_SUSPEND_NO_OWNER_REGISTRATION',
             'framework-suspend-stage':'UNRESTRICTED_SUSPEND_STOCK_FRAMEWORK',
             'lab-keep-awake':'UNRESTRICTED_POWER_NO_LAB_WAKELOCK'}[base]
        block=('if [ "$(cat /sys/module/suspend/parameters/screen_only 2>/dev/null)" != Y ]; then\n'
               '    echo '+tag+'\n    exit 0\nfi\n')
        if tag not in text:text=text.replace(anchor,anchor+block,1)
        return text.encode()
    if base=='surface-pm-debug':
        if '# The legacy opt-in gate is irrelevant' in text:return data
        begin='[ "$(cat "$params/screen_only")" = Y ]\n'
        end='[ "$(cat "$params/autosuspend_tgid")" = 0 ]\n'
        if begin in text:
            a=text.index(begin);b=text.index(end,a)+len(end)
            legacy=text[a:b]
            text=text[:a]+'# The legacy opt-in gate is irrelevant when system sleep is unrestricted.\nif [ "$(cat "$params/screen_only" 2>/dev/null)" = Y ]; then\n'+legacy+'fi\n'+text[b:]
        return text.encode()
    lines=[]
    for line in text.splitlines(True):
        stripped=line.lstrip()
        direct=('/screen_only' in line and '/screen_only_' not in line and '= Y' in line and
                stripped.startswith(('test ','[ ',"'test ",'"test ')))
        if direct:continue
        if "printf 'SCREEN_ONLY='; cat /sys/module/suspend/parameters/screen_only" in line:
            line=line.rstrip('\n')+" 2>/dev/null || echo unavailable\n"
        lines.append(line)
    return ''.join(lines).encode()

def refresh_manifests(before, result):
    """Rehash changed dependencies, preserving explicitly rebuilt inner manifests."""
    result=dict(result);changed=set()
    manifests=[n for n in before if PurePosixPath(n).name=='SHA256SUMS' or n.endswith('.sha256')]
    pattern=rb'([0-9a-fA-F]{64})( [ *])([^\r\n]+)(\r?\n)?'
    def resolve(name,path):
        path=path.decode('utf-8')
        return posixpath.normpath(path.lstrip('/') if path.startswith('/') else str(PurePosixPath(name).parent/path))
    for _ in range(len(manifests)+1):
        progress=False
        for name in manifests:
            original={}
            for line in before[name][1].splitlines(True):
                m=re.fullmatch(pattern,line)
                if m:original[resolve(name,m[3])]=m[1].lower()
            mode,current=result[name];lines=[]
            for line in current.splitlines(True):
                m=re.fullmatch(pattern,line)
                if m:
                    target=resolve(name,m[3])
                    if target in before and target in result and before[target]!=result[target]:
                        expected=hashlib.sha256(before[target][1]).hexdigest().encode()
                        if original.get(target)!=expected:raise ValueError('Baseline manifest mismatch: '+name+' -> '+target)
                        updated=hashlib.sha256(result[target][1]).hexdigest().encode()
                        if m[1].lower() not in (expected,updated):raise ValueError('Modified manifest mismatch: '+name+' -> '+target)
                        line=updated+line[64:]
                lines.append(line)
            updated=(mode,b''.join(lines))
            if updated!=result[name]:
                result[name]=updated;changed.add(name);progress=True
        if not progress:return result,sorted(changed)
    raise ValueError('Cyclic integrity manifests cannot be refreshed')

def integrate_entries(entries):
    result=dict(entries);changed=[]
    for name,(mode,data) in entries.items():
        if not data.startswith(b'#!') or b'screen_only' not in data:continue
        after=transform(name,data)
        if after!=data:result[name]=(mode,after);changed.append(name)
    result,manifests=refresh_manifests(entries,result)
    return result,sorted(set(changed+manifests))


def nested_codec_runtime(entries, launcher):
    """Migrate guarded legacy C2 scripts to a separate immutable runtime version."""
    import io,tarfile,json,copy,struct
    root='etc/sq2-c2-runtime-boot/'
    path=root+'runtime.tar'
    if path not in entries:return dict(entries),None
    with tarfile.open(fileobj=io.BytesIO(entries[path][1])) as tar:
        members=tar.getmembers()
        if len({m.name for m in members})!=len(members) or any(not m.isfile() or PurePosixPath(m.name).name!=m.name for m in members):
            raise ValueError('Unsafe Codec2 runtime archive')
        blobs={m.name:tar.extractfile(m).read() for m in members}
    guarded=[n for n,b in blobs.items() if b'/parameters/screen_only' in b]
    if not guarded:return dict(entries),None
    if guarded!=['start.sh']:raise ValueError('Unknown nested Codec2 flag dependency: '+repr(guarded))
    if launcher is None:raise ValueError('Nested Codec2 flag needs explicit rebuilt --codec-launcher')
    if len(launcher)<64 or launcher[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',launcher,18)[0]!=183 or b'/data/vendor/sq2-codec2/v2u/' not in launcher:
        raise ValueError('Codec2 launcher must be rebuilt for the separate v2u runtime')
    # Verify the old immutable package rather than laundering an invalid baseline.
    for row in blobs['SHA256SUMS'].decode().splitlines():
        h,name=row.split()
        if name not in blobs or hashlib.sha256(blobs[name]).hexdigest()!=h:
            raise ValueError('Nested baseline manifest mismatch: '+name)
    if entries[root+'runtime-files.sha256'][1]!=blobs['SHA256SUMS']:
        raise ValueError('Nested runtime manifest copy differs')
    version='/data/vendor/sq2-codec2/v2u'
    start=transform('start.sh',blobs['start.sh'])
    if start.count(b'/data/vendor/sq2-codec2/v1')!=1:
        raise ValueError('Unknown Codec2 version path')
    blobs['start.sh']=start.replace(b'/data/vendor/sq2-codec2/v1',version.encode())
    blobs['launch']=launcher
    meta=json.loads(blobs['runtime.json']);meta.update(version=2,path=version)
    blobs['runtime.json']=(json.dumps(meta,sort_keys=True,indent=2)+'\n').encode()
    blobs['SHA256SUMS']=''.join(hashlib.sha256(b).hexdigest()+'  '+n+'\n'
        for n,b in sorted(blobs.items()) if n!='SHA256SUMS').encode()
    out=io.BytesIO()
    with tarfile.open(fileobj=out,mode='w') as tar:
        for old in members:
            m=copy.copy(old);m.size=len(blobs[m.name]);tar.addfile(m,io.BytesIO(blobs[m.name]))
    result=dict(entries);result[path]=(entries[path][0],out.getvalue())
    result[root+'runtime-files.sha256']=(entries[root+'runtime-files.sha256'][0],blobs['SHA256SUMS'])
    result[root+'runtime.sha256']=(entries[root+'runtime.sha256'][0],
        (hashlib.sha256(out.getvalue()).hexdigest()+'  runtime.tar\n').encode())
    for name in ['install.sh','service.sh']:
        mode,body=entries[root+name]
        if b'/v1' not in body:raise ValueError('Unknown installed Codec2 version: '+name)
        result[root+name]=(mode,body.replace(b'/v1',b'/v2u'))
    result,_=refresh_manifests(entries,result)
    return result,dict(path=version,legacy_version_untouched=True,changed_nested=[
        n for n in blobs if n in ['start.sh','launch','runtime.json','SHA256SUMS']])
