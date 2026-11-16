"""Default two-port DP, native Settings tile and authenticated lower-port ADB.

Only shared platform inputs are loaded. Fresh kernels rebuild all five modules;
prebuilt modules require exact Image pairing. No identity/authorization is copied.
"""
from pathlib import Path
import hashlib, json, os, shutil, struct, subprocess, sys, tempfile, zipfile
from disk_image import module, require
ROOT=Path(__file__).resolve().parents[1]
NAMES=('spx-typec','spx-ucsi-transport','ucsi_surface_hid','phy-spx-qmp-combo','usb_f_fs')
sha=lambda b:hashlib.sha256(b).hexdigest()


def settings_apk(output):
    sdk=Path(os.environ.get('SQ2_ANDROID_SDK','/root/.cache/sq2-graphics/sdk'))
    tools=sdk/'android-15';platform=sdk/'android-35/android.jar'
    # Local signing key is build identity, not a device authorization/key input.
    key=Path(os.environ.get('SQ2_TYPEC_KEYSTORE',str(ROOT/'private/touch-calibration/signing.keystore')))
    key.parent.mkdir(parents=True,exist_ok=True)
    if not key.exists():
        subprocess.run(['keytool','-genkeypair','-keystore',str(key),'-storepass','android','-keypass','android','-alias','settings','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=Surface Display Settings'],check=True,capture_output=True)
    src=ROOT/'probe/typec/settings-tile'
    with tempfile.TemporaryDirectory() as tmp:
        w=Path(tmp);(w/'classes').mkdir();(w/'dex').mkdir()
        subprocess.run(['javac','--release','8','-encoding','UTF-8','-classpath',str(platform),'-d',str(w/'classes'),str(src/'NativeDisplayTile.java')],check=True)
        subprocess.run([str(tools/'d8'),'--min-api','26','--lib',str(platform),'--output',str(w/'dex'),str(w/'classes/org/surface/displaysettings/NativeDisplayTile.class')],check=True)
        subprocess.run([str(tools/'aapt2'),'link','--manifest',str(src/'AndroidManifest.xml'),'-I',str(platform),'-o',str(w/'unsigned.apk')],check=True)
        with zipfile.ZipFile(w/'unsigned.apk','a',zipfile.ZIP_DEFLATED) as z:z.write(w/'dex/classes.dex','classes.dex')
        subprocess.run([str(tools/'zipalign'),'-f','4',str(w/'unsigned.apk'),str(w/'aligned.apk')],check=True)
        subprocess.run([str(tools/'apksigner'),'sign','--ks',str(key),'--ks-pass','pass:android','--out',str(output),str(w/'aligned.apk')],check=True)
        subprocess.run([str(tools/'apksigner'),'verify',str(output)],check=True)


def load(inputs, image, output, compiled=None):
    inputs,image,output=Path(inputs),Path(image),Path(output)
    output.mkdir(parents=True,exist_ok=False)
    sdk=Path(os.environ.get('SQ2_ANDROID_SDK','/root/.cache/sq2-graphics/sdk'))
    env=dict(os.environ,PATH=str(sdk/'android-15')+os.pathsep+os.environ['PATH'])
    subprocess.run([sys.executable,str(ROOT/'scripts/patch-spx-unknown-lid.py'),str(inputs/'services.jar'),str(output/'services.jar')],check=True,env=env)
    if compiled:
        compiled=Path(compiled)
        audit=json.loads((compiled/'build-audit.json').read_text())
        candidates=[]
        for row in audit['commands']:
            cmd=row['command']
            if cmd and cmd[0]=='make' and '-C' in cmd and any(t.startswith('O=') for t in cmd):
                source=Path(cmd[cmd.index('-C')+1]);kout=Path(next(t[2:] for t in cmd if t.startswith('O=')))
                cross=next((t.split('=',1)[1] for t in cmd if t.startswith('CROSS_COMPILE=')),'aarch64-linux-gnu-')
                candidates.append((source,kout,cross))
        require(candidates,'Matching source/KOUT absent from completed kernel audit')
        source,kout,cross=candidates[0]
        require(all(c==candidates[0] for c in candidates),'Ambiguous kernel build trees')
        from kernel_baseline import digest
        require(digest(kout/'arch/arm64/boot/Image')==digest(image),'Type-C KOUT not paired with actual Image')
        files=module('typec_builder','build-spx-typec.py').build(source,kout,output/'modules',cross)
    else:
        info=json.loads((inputs/'prebuilt.json').read_text())
        require(info['kernel_image_sha256']==sha(image.read_bytes()),'Type-C prebuilt modules require exact Image pairing')
        files={n+'.ko':(inputs/(n+'.ko')).read_bytes() for n in NAMES}
        require(all(sha(b)==info['sha256'][n] for n,b in files.items()),'Type-C module integrity mismatch')
    from kernel_baseline import policy
    for name,data in files.items():
        require(data[:6]==b'\x7fELF\x02\x01' and int.from_bytes(data[18:20],'little')==183,
                'Type-C module is not AArch64: '+name)
        require(('vermagic='+policy()['kernel_release']+' ').encode() in data,
                'Type-C module ABI mismatch: '+name)
    settings_apk(output/'settings.apk')
    return {'modules':files,'services':(output/'services.jar').read_bytes(),'settings':(output/'settings.apk').read_bytes()}


def prepare_dtb(data):
    import libfdt
    f=libfdt.Fdt(data);f.resize(len(data)+16384)
    cell=lambda *v:struct.pack('>'+'I'*len(v),*v)
    require('microsoft,surface-pro-x' in bytes(f.getprop(0,'compatible')).decode().split('\0'),'Not a Surface Pro X DT')
    def put(path,props):
        for k,v in props.items():f.setprop(f.path_offset(path),k,v)
    def add(path,props={}):
        parent,name=path.rsplit('/',1)
        try:f.path_offset(path)
        except libfdt.FdtException:f.add_subnode(f.path_offset(parent or '/'),name)
        put(path,props)
    handles=[];off=-1;depth=0
    while True:
        off,depth=f.next_node(off,depth)
        if off<0 or depth<0:break
        try:handles.append(f.getprop(off,'phandle').as_uint32())
        except libfdt.FdtException:pass
    next_ph=max(handles,default=0)+1
    def ph(path):
        nonlocal next_ph
        try:return bytes(f.getprop(f.path_offset(path),'phandle'))
        except libfdt.FdtException:
            v=cell(next_ph);next_ph+=1;put(path,{'phandle':v});return v
    def link(a,b):
        ah,bh=ph(a),ph(b);put(a,{'remote-endpoint':bh});put(b,{'remote-endpoint':ah})
    root='/surface-typec';add(root,{'compatible':b'microsoft,surface-pro-x-typec\0','#address-cells':cell(1),'#size-cells':cell(0)})
    # These are SC8180X hardware resources, not boot-specific node/card indices.
    for port,phyaddr,dpaddr,usbwrap,usbaddr in [(0,'88e8000','ae90000','a6f8800','a600000'),(1,'88ed000','ae98000','a8f8800','a800000')]:
        phy='/soc@0/phy@'+phyaddr;dp='/soc@0/display-subsystem@ae00000/displayport-controller@'+dpaddr;usb='/soc@0/usb@'+usbwrap+'/usb@'+usbaddr
        require(bytes(f.getprop(f.path_offset(dp),'compatible'))==b'qcom,sc8180x-dp\0','Unexpected DP controller')
        con=root+f'/connector@{port}';add(con,{'compatible':b'usb-c-connector\0','reg':cell(port),'data-role':b'dual\0','power-role':b'dual\0'})
        add(con+'/ports',{'#address-cells':cell(1),'#size-cells':cell(0)})
        for n in (0,1):add(con+f'/ports/port@{n}',{'reg':cell(n)});add(con+f'/ports/port@{n}/endpoint')
        put(phy,{'compatible':b'microsoft,spx-sc8180x-usb3-dp-phy\0','orientation-switch':b'','mode-switch':b''})
        put(dp,{'status':b'okay\0'});out=dp+'/ports/port@1/endpoint';put(out,{'data-lanes':cell(0,1,2,3)})
        link(out,phy+'/ports/port@2/endpoint');link(phy+'/ports/port@0/endpoint',con+'/ports/port@1/endpoint');link(usb+'/ports/port@0/endpoint',con+'/ports/port@0/endpoint')
        if port==0:put(usb,{'dr_mode':b'otg\0','usb-role-switch':b'','role-switch-default-mode':b'host\0'})
    f.pack();return bytes(f.as_bytearray())


STAGE=b'''#!/bin/sh
set -eu
insmod /etc/spx-typec/phy-spx-qmp-combo.ko
insmod /etc/spx-typec/spx-typec.ko
insmod /etc/spx-typec/ucsi_surface_hid.ko
insmod /etc/spx-typec/spx-ucsi-transport.ko
modprobe libcomposite
insmod /etc/spx-typec/usb_f_fs.ko
'''


def install(records,bodies,files,new_record):
    service='etc/sq2-qti-system/root/bin/hw/vendor.qti.hardware.display.composer-service'
    require(service in bodies,'QTI composer input absent')
    composer=module('typec_composer','patch-spx-composer.py').patch(bodies[service])
    delta={service:(records[service][1][1],composer),'etc/spx-services-lid.jar':(0o100644,files['services']),
           'bin/spx-typec-stage':(0o100755,STAGE)}
    for n,b in files['modules'].items():delta['etc/spx-typec/'+n]=(0o100644,b)
    for n in ('spx-lower-usb','spx-lower-usb-boot'):delta['bin/'+n]=(0o100755,(ROOT/'probe/test2'/n).read_bytes().replace(b'\r\n',b'\n'))
    app='app/SurfaceDisplaySettings/SurfaceDisplaySettings.apk';prefix='etc/sq2-qti-system/'
    delta[prefix+'root/'+app]=(0o100644,files['settings'])
    listing=prefix+'INSTALL_FILES';lines=bodies[listing].splitlines()
    if app.encode() not in lines:delta[listing]=(0o100644,bodies[listing].rstrip(b'\n')+b'\n'+app.encode()+b'\n')
    init=bodies['init'];anchor=b'exec native-handoff /newroot\n'
    stage=b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n'
    pre=b'/bin/spx-typec-stage > /logs/spx-typec.txt 2>&1 || echo SPX_TYPEC_STAGE_FAILED\n'
    # Do not replay current port state until MSM attaches the HPD callback.
    post=b'echo 1 > /sys/bus/platform/devices/surface-typec/events_enabled 2>> /logs/spx-typec.txt || echo SPX_TYPEC_EVENTS_FAILED\n'
    gpu=b'for name in apex bootstrap-apex linkerconfig mnt tmp storage data_mirror; do '
    hook=(b'mount --bind /etc/spx-services-lid.jar /newroot/system/framework/services.jar || fail spx-lid-jar-bind\n'
          b'/bin/spx-lower-usb-boot > /logs/spx-lower-usb.txt 2>&1 &\n')
    require(init.count(stage)==1 and init.count(gpu)==1 and init.count(anchor)==1,'Unknown Type-C init anchors')
    if pre not in init:init=init.replace(stage,pre+stage)
    if post not in init:init=init.replace(gpu,post+gpu)
    if hook not in init:init=init.replace(anchor,hook+anchor)
    delta['init']=(records['init'][1][1],init)
    for name,(mode,body) in delta.items():
        bodies[name]=body;records[name]=new_record(mode)
        for parent in Path(name).parents:
            key=parent.as_posix()
            if key!='.' and key not in bodies:bodies[key]=b'';records[key]=new_record(0o40755)
    return {'enabled':True,'lower_usb_default':'adb','settings':'native topology/mirror/extend','module_sha256':{n:sha(b) for n,b in files['modules'].items()}}


if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description='Prepare shared Type-C build inputs; never deploys')
    p.add_argument('--services',type=Path,required=True)
    p.add_argument('--modules',type=Path,help='Optional explicit image-paired module directory')
    p.add_argument('--image',type=Path,help='Exact Image paired with --modules')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if bool(a.modules)!=bool(a.image):p.error('--modules and --image must be supplied together')
    with zipfile.ZipFile(a.services) as z:require('classes2.dex' in z.namelist(),'Missing services DEX')
    a.output.mkdir(parents=True,exist_ok=False);shutil.copy2(a.services,a.output/'services.jar')
    if a.modules:
        info={'kernel_image_sha256':sha(a.image.read_bytes()),'sha256':{}}
        for n in NAMES:
            data=(a.modules/(n+'.ko')).read_bytes()
            require(data[:6]==b'\x7fELF\x02\x01' and int.from_bytes(data[18:20],'little')==183,'Expected ARM64 module')
            shutil.copy2(a.modules/(n+'.ko'),a.output/(n+'.ko'));info['sha256'][n+'.ko']=sha(data)
        (a.output/'prebuilt.json').write_text(json.dumps(info,indent=2)+'\n')
