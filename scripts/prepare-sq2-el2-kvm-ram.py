from pathlib import Path
import shutil,gzip,importlib.util,hashlib,libfdt,struct
r=Path(__file__).resolve().parents[1];p=r/'out/el2-kvm';u=r/'upstream/slbounce';s=p/'efi-source';s.mkdir(exist_ok=True)
shutil.copytree(u/'src',s/'src',dirs_exist_ok=True);shutil.copyfile(u/'Makefile',s/'Makefile')
x=(s/'src/bounce_main.c').read_text();x=x[:x.index('EFI_STATUS efi_main(')]+(r/'probe/virtualization/el2-launcher-main.c').read_text();(s/'src/bounce_main.c').write_text(x)
if not (s/'external').exists():(s/'external').symlink_to(u/'external',target_is_directory=True)
b=(p/'base.dtb').read_bytes();assert hashlib.sha256(b).hexdigest()=='6db562c2a28c82ca66a1e87b98d7c63c32c5a84f909917c8435fdc3fc19fef25'
f=libfdt.Fdt(b);f.resize(len(b)+8192)
def walk(off=0,path=''):
 yield off,path or '/'
 try:c=f.first_subnode(off)
 except libfdt.FdtException:return
 while c>=0:
  yield from walk(c,path+'/'+f.get_name(c))
  try:c=f.next_subnode(c)
  except libfdt.FdtException:break
paths=[]
for off,path in walk():
 try:compat=bytes(f.getprop(off,'compatible'))
 except libfdt.FdtException:compat=b''
 if any(n in path for n in ('gpu@','zap-shader','remoteproc@','video-codec@','pcie@','usb@')):paths.append(path)
for path in sorted(paths,key=len,reverse=True):f.setprop(f.path_offset(path),'status',b'disabled\0')
f.pack();(p/'minimal-el2.dtb').write_bytes(bytes(f.as_bytearray()));(p/'disabled-dt-nodes.txt').write_text('\n'.join(paths)+'\n')
spec=importlib.util.spec_from_file_location('cpio',r/'scripts/pack-sq2-vulkan.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
files={n:(0o40755,b'') for n in ('bin','dev','proc','sys')}
files['bin/busybox']=(0o100755,(p/'busybox').read_bytes());files['bin/sh']=(0o120777,b'busybox')
for n in ('mount','uname','cat','dmesg','grep','ls','sleep','reboot'):files['bin/'+n]=(0o120777,b'busybox')
files['init']=(0o100755,(r/'probe/virtualization/el2-kvm-init').read_bytes().replace(b'\r\n',b'\n'));files['kvm-arm64-witness']=(0o100755,(r/'out/sq2-el2-test/kvm-arm64-witness').read_bytes())
a=b''.join(c.entry(n,data,mode) for n,(mode,data) in files.items())+c.entry('TRAILER!!!',b'',0);a+=b'\0'*(-len(a)%512);(p/'initramfs.cpio.gz').write_bytes(gzip.compress(a,mtime=0))
shutil.copyfile(r/'reference/sq1-el2/windows-22621.6199/tcblaunch.exe',p/'tcblaunch.exe')
print('Prepared minimal DTB, RAM initramfs and isolated launcher source')
