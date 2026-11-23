import sys, unittest, struct, tempfile, subprocess, zipfile, os, textwrap, re
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
        meta=t.install(records,bodies,files,full.new_record,lower_usb_watch=b'ELF-WATCH');first=dict(bodies)
        t.install(records,bodies,files,full.new_record,lower_usb_watch=b'ELF-WATCH');self.assertEqual(first,bodies)
        self.assertEqual(meta['lower_usb_default'],'auto')
        self.assertEqual(bodies['keep'],b'unchanged')
        self.assertEqual(bodies['bin/spx-lower-usb-watch'],b'ELF-WATCH')
        self.assertEqual(bodies['init'].count(b'/bin/spx-lower-usb-boot '),1)
        self.assertEqual(bodies['init'].count(b'/bin/spx-typec-stage '),1)
        self.assertEqual(bodies[listing].count(b'SurfaceDisplaySettings.apk'),1)
        self.assertFalse(any(n.endswith(('lower-mode','adb_keys','adbkey')) for n in bodies))
        for n in bodies:
            for parent in Path(n).parents:
                if str(parent)!='.':self.assertIn(parent.as_posix(),records)

    def test_build_lower_usb_watch_static_arm64_elf(self):
        elf=t.build_lower_usb_watch()
        self.assertEqual(elf[:6],b'\x7fELF\x02\x01')
        self.assertEqual(int.from_bytes(elf[18:20],'little'),183)

    def test_build_default_wired(self):
        s=(ROOT/'scripts/full_image.py').read_text()
        for v in ("private/'typec'",'typec=typec,','typec_dtb(dt.read_bytes())'):self.assertIn(v,s)
        helper=(ROOT/'probe/test2/spx-lower-usb').read_text()
        self.assertIn('echo auto',helper);self.assertIn('apply-role',helper)
        self.assertNotIn('ro.adb.secure 0',helper)
        self.assertIn('settings put global adb_enabled 1',helper)
        self.assertIn('test -d "$sysfs/class/udc/a600000.usb"',helper)

    def test_shell_syntax(self):
        for n in ['spx-lower-usb','spx-lower-usb-boot']:
            subprocess.run(['sh','-n',str(ROOT/'probe/test2'/n)],check=True)

    def test_spx_typec_cooperative_auto_dp_invariants(self):
        src=(ROOT/'probe/typec/spx-typec.c').read_text()
        for snippet in (
            'lower_auto_dp',
            'lower_dp_state',
            'SPX_LOWER_DP=%s',
            'wait_event_timeout(ctx->role_wait,',
            'wake_up_all(&ctx->role_wait);',
            'spx_emit_dp_uevent(ctx,"pending")',
            'spx_emit_dp_uevent(ctx,"none")',
            'spx_wait_role_settled(',
            'spx_apply_mux_only(',
            'struct mutex role_lock;',
        ):
            self.assertIn(snippet,src)

    def test_spx_lower_usb_watch_truth_table_and_uevent_stream(self):
        with tempfile.TemporaryDirectory(prefix='spx-lower-watch-test-') as td:
            td_p=Path(td)
            watch_bin=td_p/'spx-lower-usb-watch'
            subprocess.run([
                'gcc','-std=c11','-Os','-Wall','-Wextra','-Werror',
                str(ROOT/'probe/test2/spx-lower-usb-watch.c'),'-o',str(watch_bin),
            ],check=True)

            sysfs=td_p/'sys'
            typec_dev=sysfs/'bus/platform/devices/surface-typec'
            typec_dev.mkdir(parents=True)
            role_node=typec_dev/'lower_data_role'
            dp_node=typec_dev/'lower_dp_state'
            storage_node=typec_dev/'storage_ports'
            ucsi_dev=sysfs/'devices/platform/spx-ucsi-transport/0019:045E:0922.0001/typec'
            port0_dev=ucsi_dev/'port0'
            port1_dev=ucsi_dev/'port1'
            port0_dev.mkdir(parents=True)
            port1_dev.mkdir(parents=True)
            (sysfs/'class/typec').mkdir(parents=True)
            (sysfs/'class/typec/port0').symlink_to('../../devices/platform/spx-ucsi-transport/0019:045E:0922.0001/typec/port0')
            (sysfs/'class/typec/port1').symlink_to('../../devices/platform/spx-ucsi-transport/0019:045E:0922.0001/typec/port1')

            calls_log=td_p/'calls.log'
            helper=td_p/'mock-helper'
            helper.write_text(textwrap.dedent(f'''\
                #!/bin/sh
                printf '%s %s\\n' "$1" "$2" >> "{calls_log}"
                if [ "$1" = "apply-role" ]; then
                    if [ "$2" = "host" ]; then
                        printf 'host\\n' > "{role_node}"
                    elif [ "$2" = "adb" ]; then
                        printf 'device\\n' > "{role_node}"
                    fi
                fi
            '''))
            helper.chmod(0o755)

            def reset_state(role='device',dp='none',storage='0',partner=None):
                role_node.write_text(role+'\n')
                dp_node.write_text(dp+'\n')
                storage_node.write_text(storage+'\n')
                p0_partner=port0_dev/'port0-partner'
                if p0_partner.exists():
                    for c in list(p0_partner.iterdir()):
                        c.unlink()
                    p0_partner.rmdir()
                if partner is not None:
                    p0_partner.mkdir()
                    (port0_dev/'data_role').write_text(partner+'\n')
                else:
                    (port0_dev/'data_role').write_text('[host] device\n')
                calls_log.write_text('')

            def run_once():
                subprocess.run([str(watch_bin),'--helper',str(helper),'--sysfs',str(sysfs),'--once'],check=True)
                return calls_log.read_text().splitlines()

            # 1. Idle (host -> device), PC A-to-C (no partner -> device), PC C-to-C / Charger (host [device] -> device)
            reset_state(role='host',dp='none',partner=None)
            self.assertEqual(run_once(),['apply-role adb'])
            reset_state(role='device',dp='none',partner=None)
            self.assertEqual(run_once(),[])
            reset_state(role='device',dp='none',partner='host [device]')
            self.assertEqual(run_once(),[])

            # 2. USB+Ethernet dock ([host] device) -> host immediately; unplug -> adb
            reset_state(role='device',dp='none',partner='[host] device')
            self.assertEqual(run_once(),['apply-role host'])
            reset_state(role='host',dp='none',partner=None)
            self.assertEqual(run_once(),['apply-role adb'])

            # 3. DP pending/active -> host; DP unplug -> adb
            reset_state(role='device',dp='pending',partner=None)
            self.assertEqual(run_once(),['apply-role host'])
            reset_state(role='host',dp='active',partner=None)
            self.assertEqual(run_once(),[])
            reset_state(role='host',dp='none',partner=None)
            self.assertEqual(run_once(),['apply-role adb'])

            # 4. Boot storage lock forces host
            reset_state(role='device',dp='none',storage='1',partner=None)
            self.assertEqual(run_once(),['apply-role host'])

            # 5. Active lower xHCI USB device prevents yanking host mode
            xhci_dev=sysfs/'devices/platform/soc@0/a6f8800.usb/a600000.usb/xhci-hcd.1.auto/usb3/3-1'
            xhci_dev.mkdir(parents=True)
            (xhci_dev/'idVendor').write_text('0bda\n')
            reset_state(role='host',dp='none',partner=None)
            self.assertEqual(run_once(),[])
            (xhci_dev/'idVendor').unlink()

            # 6. Dynamic discovery skips non-UCSI port0 and picks lowest spx-ucsi-transport port2
            other_port0=sysfs/'devices/platform/other-usb/typec/port0'
            (other_port0/'port0-partner').mkdir(parents=True)
            (other_port0/'data_role').write_text('[host] device\n')
            (sysfs/'class/typec/port0').unlink()
            (sysfs/'class/typec/port0').symlink_to('../../devices/platform/other-usb/typec/port0')
            port2_dev=ucsi_dev/'port2'
            port2_dev.mkdir(parents=True)
            (port2_dev/'data_role').write_text('host [device]\n')
            (sysfs/'class/typec/port2').symlink_to('../../devices/platform/spx-ucsi-transport/0019:045E:0922.0001/typec/port2')
            role_node.write_text('device\n')
            dp_node.write_text('none\n')
            storage_node.write_text('0\n')
            calls_log.write_text('')
            self.assertEqual(run_once(),[])
            (sysfs/'class/typec/port2').unlink()
            (sysfs/'class/typec/port0').unlink()
            (sysfs/'class/typec/port0').symlink_to('../../devices/platform/spx-ucsi-transport/0019:045E:0922.0001/typec/port0')

            # 7. --test-stdin stream: immediate DP pending -> host, ignore port1, debounce cancellation, debounced host -> adb
            reset_state(role='device',dp='pending',partner=None)
            stream=(
                'ACTION=change\nSUBSYSTEM=platform\nDEVPATH=/devices/platform/surface-typec\nSPX_LOWER_DP=pending\n\n'
                'ACTION=remove\nSUBSYSTEM=typec\nDEVPATH=/devices/platform/spx-ucsi-transport/0019:045E:0922.0001/typec/port1/port1-partner\nTYPEC_PORT=port1\n\n'
            )
            subprocess.run([str(watch_bin),'--helper',str(helper),'--sysfs',str(sysfs),'--host-to-adb-debounce-ms','30','--test-stdin'],input=stream,text=True,check=True)
            self.assertEqual(calls_log.read_text().splitlines(),['apply-role host'])

            # Debounce cancellation: unplug event followed by pending DP event cancels host->adb return
            reset_state(role='host',dp='none',partner=None)
            calls_log.write_text('')
            proc=subprocess.Popen([str(watch_bin),'--helper',str(helper),'--sysfs',str(sysfs),'--host-to-adb-debounce-ms','10','--test-stdin'],stdin=subprocess.PIPE,text=True)
            proc.stdin.write('ACTION=change\nSUBSYSTEM=platform\nDEVPATH=/devices/platform/surface-typec\nSPX_LOWER_DP=none\n\n')
            proc.stdin.flush()
            dp_node.write_text('pending\n')
            proc.stdin.write('ACTION=change\nSUBSYSTEM=platform\nDEVPATH=/devices/platform/surface-typec\nSPX_LOWER_DP=pending\n\n')
            proc.stdin.close()
            self.assertEqual(proc.wait(timeout=5),0)
            self.assertEqual(calls_log.read_text().splitlines(),[])

            # Debounced host->adb transition when unplug stays clear
            reset_state(role='host',dp='none',partner=None)
            calls_log.write_text('')
            unplug_stream='ACTION=change\nSUBSYSTEM=platform\nDEVPATH=/devices/platform/surface-typec\nSPX_LOWER_DP=none\n\n'
            subprocess.run([str(watch_bin),'--helper',str(helper),'--sysfs',str(sysfs),'--host-to-adb-debounce-ms','10','--test-stdin'],input=unplug_stream,text=True,check=True)
            self.assertEqual(calls_log.read_text().splitlines(),['apply-role adb'])

    def test_synthetic_dtb_wakeup_source_and_idempotence(self):
        import libfdt
        dts = """/dts-v1/;
/ {
    compatible = "microsoft,surface-pro-x", "qcom,sc8180x";
    #address-cells = <2>;
    #size-cells = <2>;
    reserved-memory {
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;
        keep-prop = <1>;
    };
    soc@0 {
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;
        phy@aec2a00 {
            compatible = "qcom,sc8180x-edp-phy";
            keep-edp = <2>;
        };
        phy@88e8000 {
            compatible = "qcom,sc8180x-qmp-usb3-dp-phy";
            ports {
                #address-cells = <1>;
                #size-cells = <0>;
                port@0 { reg = <0>; endpoint {}; };
                port@2 { reg = <2>; endpoint {}; };
            };
        };
        phy@88ed000 {
            compatible = "qcom,sc8180x-qmp-usb3-dp-phy";
            ports {
                #address-cells = <1>;
                #size-cells = <0>;
                port@0 { reg = <0>; endpoint {}; };
                port@2 { reg = <2>; endpoint {}; };
            };
        };
        usb@a4f8800 {
            compatible = "qcom,sc8180x-dwc3-mp", "qcom,dwc3";
        };
        usb@a6f8800 {
            compatible = "qcom,sc8180x-dwc3", "qcom,dwc3";
            usb@a600000 {
                compatible = "snps,dwc3";
                ports {
                    #address-cells = <1>;
                    #size-cells = <0>;
                    port@0 { reg = <0>; endpoint {}; };
                };
            };
        };
        usb@a8f8800 {
            compatible = "qcom,sc8180x-dwc3", "qcom,dwc3";
            usb@a800000 {
                compatible = "snps,dwc3";
                ports {
                    #address-cells = <1>;
                    #size-cells = <0>;
                    port@0 { reg = <0>; endpoint {}; };
                };
            };
        };
        display-subsystem@ae00000 {
            displayport-controller@ae90000 {
                compatible = "qcom,sc8180x-dp";
                status = "disabled";
                ports {
                    #address-cells = <1>;
                    #size-cells = <0>;
                    port@1 { reg = <1>; endpoint {}; };
                };
            };
            displayport-controller@ae98000 {
                compatible = "qcom,sc8180x-dp";
                status = "disabled";
                ports {
                    #address-cells = <1>;
                    #size-cells = <0>;
                    port@1 { reg = <1>; endpoint {}; };
                };
            };
        };
    };
};
"""
        res = subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb'], input=dts.encode(), capture_output=True, check=True)
        base_dtb = res.stdout
        new = t.prepare_dtb(base_dtb)
        self.assertEqual(t.prepare_dtb(new), new)
        f = libfdt.Fdt(new)
        for usbwrap in ('/soc@0/usb@a4f8800', '/soc@0/usb@a6f8800', '/soc@0/usb@a8f8800'):
            off = f.path_offset(usbwrap)
            self.assertEqual(bytes(f.getprop(off, 'wakeup-source')), b'')
        self.assertEqual(bytes(f.getprop(f.path_offset('/soc@0/usb@a6f8800/usb@a600000'), 'dr_mode')), b'otg\0')
        expected_freqs = struct.pack('>4Q', 1620000000, 2700000000, 5400000000, 8100000000)
        for a in ('ae90000', 'ae98000'):
            n = f.path_offset('/soc@0/display-subsystem@ae00000/displayport-controller@' + a)
            self.assertEqual(bytes(f.getprop(n, 'status')), b'okay\0')
            ep = f.path_offset('/soc@0/display-subsystem@ae00000/displayport-controller@' + a + '/ports/port@1/endpoint')
            self.assertEqual(bytes(f.getprop(ep, 'data-lanes')), struct.pack('>4I', 0, 1, 2, 3))
            self.assertEqual(bytes(f.getprop(ep, 'link-frequencies')), expected_freqs)
        self.assertEqual(f.getprop(f.path_offset('/soc@0/phy@aec2a00'), 'keep-edp').as_uint32(), 2)
        self.assertEqual(f.getprop(f.path_offset('/reserved-memory'), 'keep-prop').as_uint32(), 1)

    def test_real_dtb_graph_if_available(self):
        import libfdt
        p=ROOT/'out/typec-dp-20261116/BOOT_BASELINE.dtb'
        if not p.exists():self.skipTest('optional private live DT input')
        old=p.read_bytes();new=t.prepare_dtb(old);self.assertEqual(t.prepare_dtb(new),new)
        f=libfdt.Fdt(new);base=libfdt.Fdt(old)
        expected_freqs=struct.pack('>4Q',1620000000,2700000000,5400000000,8100000000)
        for a in ['ae90000','ae98000']:
            n=f.path_offset('/soc@0/display-subsystem@ae00000/displayport-controller@'+a)
            self.assertEqual(bytes(f.getprop(n,'status')),b'okay\0')
            ep=f.path_offset('/soc@0/display-subsystem@ae00000/displayport-controller@'+a+'/ports/port@1/endpoint')
            self.assertEqual(bytes(f.getprop(ep,'link-frequencies')),expected_freqs)
        for usbwrap in ('/soc@0/usb@a6f8800', '/soc@0/usb@a8f8800'):
            self.assertEqual(bytes(f.getprop(f.path_offset(usbwrap), 'wakeup-source')), b'')
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

    def test_settings_apk_localized_en_and_zh(self):
        sdk=t._find_sdk()
        aapt2=sdk/'android-15/aapt2'
        platform=sdk/'android-35/android.jar'
        if not (aapt2.is_file() and platform.is_file()):
            self.skipTest('Android SDK unavailable')
        with tempfile.TemporaryDirectory(prefix='spx-display-settings-apk-') as td:
            apk=Path(td)/'SurfaceDisplaySettings.apk'
            t.settings_apk(apk)
            xml=subprocess.check_output([str(aapt2),'dump','xmltree',str(apk),'--file','AndroidManifest.xml'],text=True)
            self.assertRegex(xml,r'android:label\(0x01010001\)=@0x7f[0-9a-f]{6}')
            self.assertRegex(xml,r'com\.android\.settings\.title"\)\s*\n\s*A: http://schemas\.android\.com/apk/res/android:resource\(0x01010025\)=@0x7f[0-9a-f]{6}')
            self.assertRegex(xml,r'com\.android\.settings\.summary"\)\s*\n\s*A: http://schemas\.android\.com/apk/res/android:resource\(0x01010025\)=@0x7f[0-9a-f]{6}')
            res=subprocess.check_output([str(aapt2),'dump','resources',str(apk)],text=True)
            self.assertIn('() "Display arrangement & mirroring"',res)
            self.assertIn('(zh) "屏幕排列与镜像"',res)
            self.assertIn('() "Use native multi-display arrangement, mirror/extend & resolution settings"',res)
            self.assertIn('(zh) "使用系统原生多屏排列、镜像/扩展和分辨率设置"',res)


if __name__=='__main__':unittest.main()
