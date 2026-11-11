#!/usr/bin/env python3
"""Host-only exact early-module plan. No make, kernel insertion or device I/O."""
import argparse, fnmatch, hashlib, heapq, json, re, shutil, struct, sys
from pathlib import Path

def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def norm(n):
    if not re.fullmatch(r'[A-Za-z0-9_-]+',n): raise ValueError('invalid module name: '+n)
    return n.replace('-','_')
def relpath(s):
    p=Path(s)
    if p.is_absolute() or '..' in p.parts or not re.fullmatch(r'[A-Za-z0-9_./+-]+',s) or not s.endswith('.ko'): raise ValueError('invalid module path: '+s)
    return s
def resolve_module(n,names,builtin,aliases):
    canonical=norm(n)
    if canonical in names or canonical in builtin:return canonical
    matches={v for pattern,v in aliases if fnmatch.fnmatchcase(n,pattern)}
    if len(matches)>1:raise ValueError('ambiguous alias: '+n)
    if matches:
        value=matches.pop()
        if value not in names and value not in builtin:raise ValueError('alias target absent: '+value)
        return value
    return None
def elf_identity(raw,where):
    if raw[:6]!=b'\x7fELF\x02\x01' or len(raw)<64 or struct.unpack_from('<H',raw,18)[0]!=183:raise ValueError('not ARM64 ELF: '+where)
    table=struct.unpack_from('<Q',raw,40)[0]
    size,count,strings=struct.unpack_from('<HHH',raw,58)
    if size!=64 or not count or strings>=count or table+count*size>len(raw):raise ValueError('invalid ELF section table: '+where)
    sections=[struct.unpack_from('<IIQQQQIIQQ',raw,table+i*size) for i in range(count)]
    def data(s):
        start,length=s[4],s[5]
        if start+length>len(raw):raise ValueError('ELF section out of bounds: '+where)
        return raw[start:start+length]
    names=data(sections[strings]);found={}
    for s in sections:
        if s[0]>=len(names):raise ValueError('ELF section-name bounds: '+where)
        end=names.find(b'\0',s[0])
        if end<0:raise ValueError('ELF section-name termination: '+where)
        name=names[s[0]:end]
        if name in (b'.modinfo',b'.note.gnu.build-id'):
            if name in found:raise ValueError('duplicate ELF metadata section: '+where)
            found[name]=data(s)
    if b'.modinfo' not in found:raise ValueError('missing ELF .modinfo: '+where)
    fields={}
    for item in found[b'.modinfo'].split(b'\0'):
        if not item:continue
        if b'=' not in item:raise ValueError('malformed ELF .modinfo: '+where)
        key,value=item.split(b'=',1)
        fields.setdefault(key,[]).append(value)
    for key in (b'name',b'vermagic',b'depends'):
        if len(fields.get(key,[]))!=1:raise ValueError('missing/duplicate ELF '+key.decode()+': '+where)
    module_name=norm(fields[b'name'][0].decode('ascii'))
    depends=[norm(n) for n in fields[b'depends'][0].decode('ascii').split(',') if n]
    if len(depends)!=len(set(depends)):raise ValueError('duplicate ELF depends: '+where)
    note=found.get(b'.note.gnu.build-id')
    if note is not None:
        if len(note)<12:raise ValueError('short GNU build-id note: '+where)
        ns,ds,typ=struct.unpack_from('<III',note)
        expected=12+((ns+3)&~3)+((ds+3)&~3)
        if ns!=4 or not ds or typ!=3 or note[12:16]!=b'GNU\0' or expected!=len(note):raise ValueError('invalid GNU build-id note: '+where)
    return dict(name=module_name,vermagic=fields[b'vermagic'][0].decode('ascii'),depends=depends,note=note)
def generate(a):
    tree=a.modules.resolve(); parent=a.init.resolve(); out=a.output.absolute()
    if out.exists(): raise ValueError('fresh output required: '+str(out))
    conf={}
    for line in a.config.read_text().splitlines():
        m=re.fullmatch(r'(CONFIG_[A-Z0-9_]+)=(.*)',line) or re.fullmatch(r'# (CONFIG_[A-Z0-9_]+) is not set',line)
        if m:
            k=m[1];v=m[2] if len(m.groups())==2 else 'n'
            if k in conf:raise ValueError('duplicate config: '+k)
            conf[k]=v
    make=a.net_makefile.read_text()
    if not re.search(r'^obj-\$\(CONFIG_DUMMY\)\s*\+=\s*dummy\.o\s*$',make,re.M):raise ValueError('CONFIG_DUMMY ownership not proven by matched drivers/net/Makefile')
    text=parent.read_text()
    for s in ['modprobe af_key >> /logs/module-load.txt 2>&1 || fail af-key-module','modprobe fuse >> /logs/module-load.txt 2>&1 || fail fuse-module','if ! modprobe qcom_tsens >> /logs/module-load.txt 2>&1; then','timeout 15 modprobe "$m" >> /logs/module-load.txt 2>&1']:
        if text.count(s)!=1:raise ValueError('parent literal site not unique: '+s)
    match=re.search(r'^\s*for m in (hid_multitouch[^\n]+); do$',text,re.M)
    if not match:raise ValueError('missing original peripheral request loop')
    requested=[('af_key','base','required'),('fuse','base','required'),('qcom_tsens','tsens','required_if_opp661')]+[(n,'peripherals','optional') for n in match[1].split()]
    if len({norm(n) for n,_,_ in requested})!=len(requested):raise ValueError('duplicate normalized request')
    files={}; nonruntime_links={}
    for p in tree.rglob('*'):
        if p.is_symlink():
            rel=p.relative_to(tree).as_posix()
            if rel in ('build','source'):
                nonruntime_links[rel]=str(p.readlink());continue
            raise ValueError('module tree symlink: '+str(p))
        if p.is_file() and p.suffix=='.ko':files[p.relative_to(tree).as_posix()]=p
        elif p.is_file() and '.ko.' in p.name:raise ValueError('compressed module unsupported: '+str(p))
    dep={}; names={}
    for line in (tree/'modules.dep').read_text().splitlines():
        if ':' not in line:raise ValueError('bad modules.dep line')
        key,vals=line.split(':',1);key=relpath(key)
        if Path(key).parts[0] in ('build','source'):raise ValueError('runtime dependency under non-runtime build/source link')
        if key in dep:raise ValueError('duplicate dep entry: '+key)
        dep[key]=[relpath(v) for v in vals.split()]
        if len(set(dep[key]))!=len(dep[key]):raise ValueError('duplicate dependency: '+key)
        n=norm(Path(key).stem)
        if n in names:raise ValueError('normalization/name collision: '+n)
        names[n]=key
    if set(files)!=set(dep):raise ValueError('modules.dep does not exactly cover module files')
    builtin={}
    for line in (tree/'modules.builtin').read_text().splitlines():
        p=relpath(line);n=norm(Path(p).stem)
        if n in builtin or n in names:raise ValueError('builtin collision: '+n)
        builtin[n]=p
    for k,vs in dep.items():
        for v in vs:
            if v not in files:raise ValueError('missing dependency: '+k+' -> '+v)
    # Validate the complete hard graph, not only the selected closure.
    visiting=set(); visited=set()
    def visit(p):
        if p in visiting:raise ValueError('dependency cycle at '+p)
        if p in visited:return
        visiting.add(p)
        for q in dep[p]:visit(q)
        visiting.remove(p);visited.add(p)
    for p in dep:visit(p)
    identities={}
    for rel,p in files.items():
        ident=elf_identity(p.read_bytes(),rel)
        if ident['name']!=norm(Path(rel).stem):raise ValueError('ELF name/path mismatch: '+rel)
        identities[ident['name']]=ident
    # depmod metadata is not self-authenticating. Cross-check each ELF's actual
    # direct imports against its metadata closure, including built-in providers.
    for n,path in names.items():
        closure_paths=set()
        def collect(p):
            for q in dep[p]:
                if q not in closure_paths:closure_paths.add(q);collect(q)
        collect(path)
        providers={norm(Path(q).stem) for q in closure_paths}
        for direct in identities[n]['depends']:
            if direct not in providers and direct not in builtin:raise ValueError('ELF depends absent from modules.dep closure: '+n+' -> '+direct)
    aliases=[]
    for line in (tree/'modules.alias').read_text().splitlines():
        if not line or line.startswith('#'):continue
        parts=line.split()
        if len(parts)!=3 or parts[0]!='alias':raise ValueError('invalid alias metadata')
        aliases.append((parts[1],norm(parts[2])))
    soft={}
    for line in (tree/'modules.softdep').read_text().splitlines():
        if not line or line.startswith('#'):continue
        parts=line.split()
        if len(parts)<3 or parts[0]!='softdep':raise ValueError('invalid softdep metadata')
        n=norm(parts[1]);pre=[];post=[];where=None
        if n in soft:raise ValueError('duplicate softdep: '+n)
        for item in parts[2:]:
            if item=='pre:':where=pre
            elif item=='post:':where=post
            elif where is None:raise ValueError('softdep phase missing')
            else:where.append(norm(item))
        soft[n]=(pre,post)
    def resolve(n):
        return resolve_module(n,names,builtin,aliases)
    def closure(root):
        members=[]; edges=set(); seen=set()
        def add(n):
            if n in seen:return
            seen.add(n);members.append(n)
            if n in names:
                for path in dep[names[n]]:
                    d=norm(Path(path).stem);add(d);edges.add((d,n))
            for index,ls in enumerate(soft.get(n,([],[]))):
                for value in ls:
                    d=resolve(value)
                    if d is None:raise ValueError('missing soft dependency: '+n+' -> '+value)
                    add(d);edges.add((d,n) if index==0 else (n,d))
        add(root)
        rank={n:i for i,n in enumerate(members)}; degree={n:0 for n in members}; followers={n:[] for n in members}
        for u,v in edges:degree[v]+=1;followers[u].append(v)
        queue=[(rank[n],n) for n in members if degree[n]==0];heapq.heapify(queue);ordered=[]
        while queue:
            _,n=heapq.heappop(queue);ordered.append(n)
            for v in followers[n]:
                degree[v]-=1
                if not degree[v]:heapq.heappush(queue,(rank[v],v))
        if len(ordered)!=len(members):raise ValueError('hard/soft dependency cycle: '+root)
        return ordered
    rows=[];loads=[];classifications=[];selected=set()
    for original,group,requirement in requested:
        if norm(original)=='dummy' and conf.get('CONFIG_DUMMY')=='n':
            if 'dummy' in names or 'dummy' in builtin:raise ValueError('CONFIG_DUMMY=n conflicts with module/builtin')
            rows.append(f'{original}|{group}|{requirement}|DISABLED_BY_CONFIG|dummy|CONFIG_DUMMY=n')
            classifications.append(dict(request=original,status='DISABLED_BY_CONFIG',config='CONFIG_DUMMY=n',capability_available=False));continue
        canonical=resolve(original)
        if canonical is None:
            raise ValueError('unclassified missing request: '+original)
        kind='BUILTIN' if canonical in builtin else 'MODULE'
        rows.append(f'{original}|{group}|{requirement}|{kind}|{canonical}|-')
        order=closure(canonical)
        for n in order:
            if n in names:
                if identities[n]['note'] is None:raise ValueError('selected module lacks GNU build-id; policy=REJECT: '+n)
                selected.add(n)
            loads.append(f'{original}|{n}|'+('BUILTIN|-' if n in builtin else 'MODULE|'+names[n]))
        classifications.append(dict(request=original,canonical=canonical,status=kind,requirement=requirement,group=group,topological_closure=order))
    release=a.release.read_text().strip()
    if not re.fullmatch(r'[A-Za-z0-9._+-]+',release):raise ValueError('invalid release')
    if tree.name!=release:raise ValueError('module tree/release mismatch')
    for rel,p in files.items():
        if identities[norm(Path(rel).stem)]['vermagic'].split()[0]!=release:raise ValueError('vermagic mismatch: '+rel)
    out.mkdir(parents=True)
    (out/'init').write_text(text.replace('modprobe af_key >>','sq2-module-prelude request af_key >>').replace('modprobe fuse >>','sq2-module-prelude request fuse >>').replace('if ! modprobe qcom_tsens >>','if ! sq2-module-prelude request qcom_tsens >>').replace('timeout 15 modprobe "$m" >>','timeout 15 sq2-module-prelude request "$m" >>'))
    (out/'bin').mkdir();(out/'etc/module-prelude').mkdir(parents=True)
    config=out/'etc/module-prelude'
    shutil.copyfile(Path(__file__).with_name('execute.in.sh'),out/'bin/sq2-module-prelude')
    (out/'bin/sq2-module-prelude').chmod(0o755);(out/'init').chmod(0o755)
    (config/'requests.tsv').write_text('\n'.join(rows)+'\n');(config/'loads.tsv').write_text('\n'.join(loads)+'\n')
    (config/'module-identities.tsv').write_text(''.join(n+'|'+digest(files[names[n]])+'|'+hashlib.sha256(identities[n]['note']).hexdigest()+'\n' for n in sorted(selected)))
    (config/'non-runtime-links.tsv').write_text(''.join(f'{n}|{nonruntime_links[n]}\n' for n in sorted(nonruntime_links)))
    for src,dest in [(a.config,'matched.config'),(a.init,'parent.init'),(a.release,'kernel.release'),(a.kernel_notes,'kernel.notes'),(a.busybox,'busybox.expected'),(a.net_makefile,'drivers-net-Makefile')]:shutil.copyfile(src,config/dest)
    # Avoid copying the 2MB payload BusyBox twice into the package; pin its bytes.
    (config/'busybox.expected').unlink()
    metadata=['modules.dep','modules.builtin','modules.alias','modules.softdep']
    for n in metadata:shutil.copyfile(tree/n,config/n)
    (config/'module-files.sha256').write_text(''.join(f'{digest(files[n])}  {n}\n' for n in sorted(files)))
    payload=['requests.tsv','loads.tsv','module-identities.tsv','non-runtime-links.tsv','matched.config','parent.init','kernel.release','kernel.notes','drivers-net-Makefile','module-files.sha256']+metadata
    (config/'payload.sha256').write_text(''.join(f'{digest(config/n)}  {n}\n' for n in payload))
    manifest=dict(schema=1,status='host-generated-not-hardware-validated',kernel_release=release,parent_init=str(parent),parent_sha256=digest(parent),matched_config_sha256=digest(a.config),kernel_notes_sha256=digest(a.kernel_notes),busybox_sha256=digest(a.busybox),module_count=len(files),module_metadata={n:digest(tree/n) for n in metadata},non_runtime_links=nonruntime_links,requests=classifications,all_requested_capabilities=not any(x['status']=='DISABLED_BY_CONFIG' for x in classifications),optional_disabled=[x['request'] for x in classifications if x['status']=='DISABLED_BY_CONFIG'])
    (config/'PLAN.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # Pin plan and executor through generated literals, never eval sourced data.
    runner=out/'bin/sq2-module-prelude';s=runner.read_text()
    allowed=sorted({name for x in classifications for name in (x['request'],x.get('canonical',norm(x['request'])))})
    for k,v in {'@RELEASE@':release,'@BUSYBOX_SHA@':digest(a.busybox),'@NOTES_SHA@':digest(a.kernel_notes),'@PAYLOAD_SHA@':digest(config/'payload.sha256'),'@PLAN_SHA@':digest(config/'PLAN.json'),'@REQUESTS@':' '.join(allowed)}.items():s=s.replace(k,v)
    runner.write_text(s)
    print(json.dumps(manifest,indent=2))
    return manifest

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ['init','modules','config','release','kernel-notes','busybox','net-makefile','output']:p.add_argument('--'+name,type=Path,required=True)
    args=p.parse_args()
    try:generate(args)
    except (ValueError,OSError) as e:print('MODULE_PRELUDE_GENERATION_REJECTED: '+str(e),file=sys.stderr);sys.exit(2)
