#!/usr/bin/env python3
"""Compile one calibration app + adaptive IPTSd bundle. No device operations."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,shutil,subprocess,tempfile
R=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--dependencies',type=Path,required=True,help='existing extracted Meson wrap dependencies')
    p.add_argument('--cross-file',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--native-bundle',type=Path,help='reuse unchanged native source/hash-matched IPTSd from a prior bundle')
    a=p.parse_args()
    sha=lambda b:hashlib.sha256(b).hexdigest()
    if sha(a.archive.read_bytes())!='914b2618bd1019fd3d64851aa24284d49c76d7cdf9f2db8a7ddb0ab79db95b41':raise ValueError('IPTSd source hash')
    a.output.mkdir(parents=True,exist_ok=False)
    spec=importlib.util.spec_from_file_location('wizard',R/'scripts/build-touch-calibration.py')
    app=importlib.util.module_from_spec(spec);spec.loader.exec_module(app)
    files,audit=app.build(R,a.output/'app')
    patches=['patch-iptsd-sq2-touch.py','patch-iptsd-sq2-heatmap.py','patch-iptsd-sq2-calibration.py','patch-iptsd-adaptive-noise.py']
    if a.native_bundle:
        previous=json.loads((a.native_bundle/'touch-bundle.json').read_text())
        for n in ['probe/touch-calibration/adaptive-noise.hpp','probe/touch-calibration/noise-runtime.hpp','probe/test2/sq2-touch-calibration.hpp']+['scripts/'+n for n in patches]:
            if sha((R/n).read_bytes().replace(b'\r\n',b'\n'))!=previous['source_sha256'][n]:raise ValueError('Native source changed: '+n)
        data=(a.native_bundle/'payload/bin/iptsd').read_bytes()
        if sha(data)!=previous['files']['bin/iptsd']:raise ValueError('Native cache checksum')
        files['bin/iptsd']=(0o100755,data);deps=previous['dependency_tree_sha256']
    else:
        with (a.output/'native-build.log').open('w') as log,tempfile.TemporaryDirectory(prefix='surface-touch-') as td:
            w=Path(td);s=w/'iptsd-3.1.1'
            def run(args):
                log.write('COMMAND '+json.dumps(list(map(str,args)))+'\n');log.flush()
                subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,check=True)
            run(['tar','xf',a.archive,'-C',w])
            for patch in patches:run(['python3',R/'scripts'/patch,s])
            deps={}
            for name in ['CLI11-2.7.2','GSL-4.2.2','eigen-5.0.1','fmt-12.0.0','inih-r62','spdlog-1.17.0']:
                source=a.dependencies/name
                if not source.is_dir():raise ValueError('Missing dependency '+name)
                # Capture actual dependency contents; no network, no cached source writes.
                h=hashlib.sha256()
                for f in sorted(source.rglob('*')):
                    if f.is_file():h.update(f.relative_to(source).as_posix().encode());h.update(f.read_bytes())
                deps[name]=h.hexdigest();(s/'subprojects'/name).symlink_to(source.resolve(),target_is_directory=True)
            run(['meson','setup',w/'build',s,'--cross-file',a.cross_file,'--wrap-mode=nodownload','-Ddebug_tools=dump','-Dservice_manager=[]','-Db_lto=false'])
            run(['ninja','-C',w/'build','-j6'])
            run(['aarch64-linux-gnu-strip','-o',w/'iptsd',w/'build/src/iptsd'])
            files['bin/iptsd']=(0o100755,(w/'iptsd').read_bytes())
    for name,(mode,body) in files.items():
        f=a.output/'payload'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(body);f.chmod(mode&0o777)
    sources=audit['source_sha256']
    for name in patches+['build-unified-touch.py','surface-touch-payload.py']:
        sources['scripts/'+name]=sha((R/'scripts'/name).read_bytes().replace(b'\r\n',b'\n'))
    manifest={'files':{n:sha(b) for n,(_,b) in files.items()},'source_sha256':sources,
              'dependency_tree_sha256':deps,'hardware_tested':False,'archive_sha256':sha(a.archive.read_bytes())}
    (a.output/'touch-bundle.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('BUILT TOUCH BUNDLE',a.output.resolve())
if __name__=='__main__':main()
