#!/usr/bin/env python3
"""Build the Surface SAM Type-C/PHY modules against one matching kernel tree."""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess
from kernel_baseline import ROOT, validate_kernel_source, validate_build_tree


def build(source, kout, output, cross='aarch64-linux-gnu-'):
    source, kout, output = map(lambda p: Path(p).resolve(), (source, kout, output))
    validate_kernel_source(source); validate_build_tree(kout)
    output.mkdir(parents=True, exist_ok=False)
    qmp = source/'drivers/phy/qualcomm/phy-qcom-qmp-combo.c'
    shutil.copy2(qmp, output/qmp.name)
    # Patch only the private copy, never the source or existing resident provider.
    tree=output/'patch/drivers/phy/qualcomm';tree.mkdir(parents=True)
    shutil.copy2(qmp,tree/qmp.name)
    subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(ROOT/'patches/spx-qmp-lifecycle.patch')],cwd=output/'patch',check=True)
    text=(tree/qmp.name).read_text()
    start=text.index('static const struct of_device_id qmp_combo_of_match_table[] = {')
    end=text.index('MODULE_DEVICE_TABLE(of, qmp_combo_of_match_table);',start)
    text=text[:start]+'''static const struct of_device_id qmp_combo_of_match_table[] = {
 { .compatible = "microsoft,spx-sc8180x-usb3-dp-phy", .data = &sc8180x_usb3dpphy_cfg },
 { }
};
'''+text[end:]
    text=text.replace('.name\t= "qcom-qmp-combo-phy"','.name\t= "spx-qmp-combo-phy"')
    (output/'phy-spx-qmp-combo.c').write_text(text)
    for name in ('spx-typec.c','spx-ucsi-transport.c','ucsi_surface_hid.c'):
        shutil.copy2(ROOT/'probe/typec'/name, output/name)
    shutil.copy2(source/'drivers/usb/typec/ucsi/ucsi.h',output/'ucsi.h')
    shutil.copy2(source/'drivers/hid/surface-hid/surface_hid_core.h',output/'surface_hid_core.h')
    ffs=source/'drivers/usb/gadget/function/f_fs.c'
    ffs_tree=output/'patch/drivers/usb/gadget/function';ffs_tree.mkdir(parents=True)
    shutil.copy2(ffs,ffs_tree/ffs.name)
    subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(ROOT/'patches/spx-dwc3-ffs-ep0-dequeue.patch')],cwd=output/'patch',check=True)
    shutil.copy2(ffs_tree/ffs.name,output/'f_fs.c')
    names=('spx-typec','spx-ucsi-transport','ucsi_surface_hid','phy-spx-qmp-combo','usb_f_fs')
    includes=[source/'drivers/phy/qualcomm',source/'drivers/usb/gadget',source/'drivers/usb/gadget/function']
    (output/'Makefile').write_text('obj-m += '+' '.join(n+'.o' for n in names)+'\nusb_f_fs-y := f_fs.o\nccflags-y += '+' '.join('-I'+str(p) for p in includes)+'\n')
    cmd=['make','-C',str(kout),'ARCH=arm64','CROSS_COMPILE='+cross,'M='+str(output),'modules']
    subprocess.run(cmd,check=True)
    result={n+'.ko':(output/(n+'.ko')).read_bytes() for n in names}
    (output/'BUILD.json').write_text(json.dumps({'command':cmd,'source':str(source),'kout':str(kout),
        'sha256':{n:hashlib.sha256(b).hexdigest() for n,b in result.items()}},indent=2)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','kout','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cross',default='aarch64-linux-gnu-')
    a=p.parse_args();build(a.source,a.kout,a.output,a.cross)
