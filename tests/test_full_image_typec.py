import sys, unittest, struct, tempfile, subprocess, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import full_image_typec as t
import full_image as full
import importlib.util
s=importlib.util.spec_from_file_location('composer',ROOT/'scripts/patch-spx-composer.py');c=importlib.util.module_from_spec(s);s.loader.exec_module(c)


class TypeC(unittest.TestCase):
    def composer(self):
        b=bytearray(450000);b[:6]=b'\x7fELF\x02\x01';b[18:20]=(183).to_bytes(2,'little')
        for off,v in [(414820,bytes.fromhex('0ddc40f9bf0108eb')),(437144,bytes.fromhex('092140b94b2140b93f010b6b')),(158519,b'mdss_mdp/drm/card')]:b[off:off+len(v)]=v
        return bytes(b)

    def test_composer_copy_and_idempotence(self):
        old=self.composer();new=c.patch(old)
        self.assertNotEqual(old,new);self.assertEqual(c.patch(new),new)
        self.assertEqual(new[158519:158528],b'/drm/card')
        with self.assertRaises(ValueError):c.patch(old[:100])
        bad=bytearray(old);bad[414820]=0
        with self.assertRaises(ValueError):c.patch(bytes(bad))

    def test_install_and_no_private_state(self):
        service='etc/sq2-qti-system/root/bin/hw/vendor.qti.hardware.display.composer-service'
        listing='etc/sq2-qti-system/INSTALL_FILES'
        bodies={'init':b'#!/bin/sh\nqti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\nfor name in apex bootstrap-apex linkerconfig mnt tmp storage data_mirror; do true; done\nexec native-handoff /newroot\n',service:self.composer(),listing:b'bin/hw/composer\n','keep':b'unchanged'}
        records={n:full.new_record(0o100755 if n=='init' else 0o100644) for n in bodies}
        files={'modules':{n+'.ko':n.encode() for n in t.NAMES},'services':b'JAR','settings':b'APK'}
        t.install(records,bodies,files,full.new_record);first=dict(bodies)
        t.install(records,bodies,files,full.new_record);self.assertEqual(first,bodies)
        self.assertEqual(bodies['keep'],b'unchanged')
        self.assertEqual(bodies['init'].count(b'/bin/spx-lower-usb-boot '),1)
        self.assertEqual(bodies['init'].count(b'/bin/spx-typec-stage '),1)
        self.assertEqual(bodies[listing].count(b'SurfaceDisplaySettings.apk'),1)
        self.assertFalse(any(n.endswith(('lower-mode','adb_keys','adbkey')) for n in bodies))
        for n in bodies:
            for parent in Path(n).parents:
                if str(parent)!='.':self.assertIn(parent.as_posix(),records)

    def test_build_default_wired(self):
        s=(ROOT/'scripts/full_image.py').read_text()
        for v in ("private/'typec'",'typec=typec,','typec_dtb(dt.read_bytes())'):self.assertIn(v,s)
        helper=(ROOT/'probe/test2/spx-lower-usb').read_text()
        self.assertIn('mode=adb',helper);self.assertNotIn('ro.adb.secure 0',helper)
        self.assertIn('settings put global adb_enabled 1',helper)
        self.assertIn('test -d /sys/class/udc/a600000.usb',helper)

    def test_shell_syntax(self):
        for n in ['spx-lower-usb','spx-lower-usb-boot']:
            subprocess.run(['sh','-n',str(ROOT/'probe/test2'/n)],check=True)

    def test_real_dtb_graph_if_available(self):
        import libfdt
        p=ROOT/'out/typec-dp-20261116/BOOT_BASELINE.dtb'
        if not p.exists():self.skipTest('optional private live DT input')
        old=p.read_bytes();new=t.prepare_dtb(old);self.assertEqual(t.prepare_dtb(new),new)
        f=libfdt.Fdt(new);base=libfdt.Fdt(old)
        for a in ['ae90000','ae98000']:
            n=f.path_offset('/soc@0/display-subsystem@ae00000/displayport-controller@'+a)
            self.assertEqual(bytes(f.getprop(n,'status')),b'okay\0')
        self.assertEqual(bytes(f.getprop(f.path_offset('/soc@0/usb@a6f8800/usb@a600000'),'dr_mode')),b'otg\0')
        # Preserve memory ownership and eDP node byte properties.
        for path in ['/reserved-memory','/soc@0/phy@aec2a00']:
            def props(f):
                d={};off=f.first_property_offset(f.path_offset(path))
                while off>=0:
                    p=f.get_property_by_offset(off);d[p.name]=bytes(p)
                    try:off=f.next_property_offset(off)
                    except libfdt.FdtException:break
                return d
            self.assertEqual(props(f),props(base))


if __name__=='__main__':unittest.main()
