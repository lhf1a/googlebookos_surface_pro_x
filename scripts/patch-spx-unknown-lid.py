from pathlib import Path
import os,re,subprocess,json,zipfile,hashlib,difflib,shutil
import argparse, tempfile
parser=argparse.ArgumentParser(description='Preserve unknown initial SW_LID state; real lid callbacks unchanged')
parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
args=parser.parse_args()
jobs=str(max(1,(os.cpu_count() or 2)-1))
workspace=tempfile.TemporaryDirectory(prefix='spx-lid-')
D=Path(workspace.name);P=D
root=D/'smali';rebuilt=D/'reopened'
with zipfile.ZipFile(args.source) as archive:(D/'classes2.dex').write_bytes(archive.read('classes2.dex'))
subprocess.run(['baksmali','disassemble','-a','35','-j',jobs,str(D/'classes2.dex'),'-o',str(root)],check=True)
target=root/'com/android/server/input/InputManagerService.smali'
original=target.read_text()
modified=original
for method in ['registerLidSwitchCallbackInternal','systemRunning']:
 pat=re.compile(r'^\.method[^\n]*\b'+method+r'\([^\n]*\n.*?^\.end method',re.M|re.S)
 found=list(pat.finditer(modified));assert len(found)==1
 match=found[0];before=match.group();after=before
 query=re.search(r'invoke-virtual \{[^}]+\}, Lcom/android/server/input/InputManagerService;->getSwitchState\(III\)I\s+move-result (v\d+)',after)
 assert query
 reg=query[1];pos=query.end()
 if re.match(r'\s*if-ltz '+reg+r',',after[pos:]): continue
 after=after[:pos]+'\n\n    # No SW_LID device: keep each listener\'s default, do not synthesize CLOSED.\n    if-ltz '+reg+', :spx_skip_lid_init\n'+after[pos:]
 marker='    monitor-exit v0';assert marker in after
 after=after.replace(marker,'    :spx_skip_lid_init\n'+marker,1)
 modified=modified[:match.start()]+after+modified[match.end():]
(D/'InputManagerService.baseline.smali').write_text(original)
(D/'InputManagerService.modified.smali').write_text(modified)
(D/'input-lid.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True),modified.splitlines(True),fromfile='a/InputManagerService.smali',tofile='b/InputManagerService.smali')))
target.write_text(modified)
cmd=['smali','assemble','-a','35','-j',jobs,str(root),'-o',str(D/'classes2.modified.dex')]
r=subprocess.run(cmd,capture_output=True,text=True);assert r.returncode==0,r.stderr
r=subprocess.run(['baksmali','disassemble','-a','35','-j',jobs,str(D/'classes2.modified.dex'),'-o',str(rebuilt)],capture_output=True,text=True);assert r.returncode==0,r.stderr
changed=[];encoding_only=[];count=0
def normalize_default_booleans(text):
 return re.sub(r'(?m)^(\.field [^\n]*\bstatic\b[^\n]*:Z) = false$',r'\1',text)
for f in root.rglob('*.smali'):
 rel=f.relative_to(root);count+=1
 if f.read_bytes()!=(rebuilt/rel).read_bytes():
  if normalize_default_booleans(f.read_text())==normalize_default_booleans((rebuilt/rel).read_text()):encoding_only.append(str(rel))
  else:changed.append(str(rel))
# Labels are regenerated in the two changed methods; no other class may change semantically.
assert changed in ([], ['com/android/server/input/InputManagerService.smali']),changed
source=args.source;out=D/'services.modified.jar'
with zipfile.ZipFile(source) as a,zipfile.ZipFile(out,'w') as b:
 b.comment=a.comment
 for info in a.infolist():
  assert not (info.filename.upper().startswith('META-INF/') and info.filename.upper().endswith(('.RSA','.DSA','.EC'))),'signed JAR'
  data=(D/'classes2.modified.dex').read_bytes() if info.filename=='classes2.dex' else a.read(info.filename)
  b.writestr(info,data)
with zipfile.ZipFile(source) as a,zipfile.ZipFile(out) as b:
 assert a.namelist()==b.namelist()
 assert [n for n in a.namelist() if a.read(n)!=b.read(n)] in ([], ['classes2.dex'])
report={'methods':['InputManagerService.registerLidSwitchCallbackInternal','InputManagerService.systemRunning'],'mechanism':'Skip initial callback only for unknown negative SW_LID state; preserve registered listeners and real lid event delivery','disassembled_classes':count,'other_classes_semantically_unchanged':True,'default_false_encoding_elision_only':encoding_only,'changed_member':'classes2.dex','baseline_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'modified_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
(D/'PATCH.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
subprocess.run(['zipalign','-f','4',str(out),str(args.output)],check=True)
workspace.cleanup()
