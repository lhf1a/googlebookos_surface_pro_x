#!/usr/bin/env python3
"""Reassemble a private lab boot image from current code and built components.

Stock firmware/userspace and proprietary Qualcomm libraries are binary inputs,
not falsely claimed to be compiled from source. Never writes physical disks.
"""
from pathlib import Path
import argparse, gzip, hashlib, importlib.util, json, subprocess, zlib, shutil

R = Path(__file__).resolve().parents[1]
O = R/'out/source-boot'
spec = importlib.util.spec_from_file_location('cpio', R/'scripts/pack-sq2-vulkan.py')
c = importlib.util.module_from_spec(spec); spec.loader.exec_module(c)
sha = lambda b: hashlib.sha256(b).hexdigest()
# Current SQ2's individually verified runtime/caps/USB/BT/audio boot chain.
DEVICE_BASE_SHA = '732f23fee7754c73152f6d1b35637072dde19f0f07835b7b721236cc3162afab'

def touch_bundle(root, mode, calibration='on'):
    if mode not in ('on', 'off', 'edge-only'):
        raise ValueError('Invalid ghost-touch-fix mode')
    if calibration not in ('on', 'off'):
        raise ValueError('Invalid touch-calibration mode')
    bundle = root/'out/test2'/{'on':'iptsd','off':'iptsd-no-ghost-fix','edge-only':'iptsd-edge-only'}[mode]
    opts = json.loads((bundle/'build-options.json').read_text())
    patches = {'on':['patch-iptsd-sq2-touch.py', 'patch-iptsd-sq2-heatmap.py'],
               'off':[], 'edge-only':['patch-iptsd-sq2-heatmap.py']}[mode]
    if calibration == 'on':
        patches += ['patch-iptsd-sq2-calibration.py']
    expected = {name: sha((root/'scripts'/name).read_bytes().replace(b'\r\n', b'\n')) for name in patches}
    header_sha = (sha((root/'probe/test2/sq2-touch-calibration.hpp').read_bytes().replace(b'\r\n',b'\n'))
                  if calibration == 'on' else None)
    if (opts.get('ghost_touch_fix') != mode or opts.get('touch_calibration') != calibration or opts.get('patches') != patches
            or opts.get('calibration_header_sha256') != header_sha
            or opts.get('patch_sha256') != expected):
        raise ValueError('Stale/mismatched IPTSd bundle; rebuild with --ghost-touch-fix '+mode+' --touch-calibration '+calibration)
    files = {name: (bundle/name).read_bytes() for name in ('iptsd', 'iptsd.conf')}
    if any(sha(body) != opts.get('sha256', {}).get(name) for name, body in files.items()):
        raise ValueError('IPTSd bundle checksum mismatch')
    config = ((root/'configs/iptsd-sq2.conf').read_bytes().replace(b'\r\n', b'\n')
              if mode != 'off' else b'[DFT]\nButtonMinMag = 4000\n')
    if calibration == 'on':
        config += b'\n' + (root/'configs/iptsd-sq2-calibration.conf').read_bytes().replace(b'\r\n',b'\n')
    if files['iptsd.conf'].replace(b'\r\n', b'\n') != config:
        raise ValueError('IPTSd configuration differs from selected source policy')
    return files, opts

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=O/'native-combined-initramfs.cpio.gz')
    parser.add_argument('--device-base',type=Path,
                        help='Use the hash-pinned current SQ2 boot chain, preserving its layout and credentials')
    parser.add_argument('--ghost-touch-fix',choices=('on','off','edge-only'),default='on',
                        help='on (default): continuity and heatmap repair; edge-only: heatmap repair; off: neither')
    parser.add_argument('--touch-calibration',choices=('on','off'),default='on',
                        help='on (default): measured SQ2 spatial calibration; off: original coordinates')
    display_spec=importlib.util.spec_from_file_location('display_payload',R/'scripts/sq2-display-boot-payload.py')
    display=importlib.util.module_from_spec(display_spec);display_spec.loader.exec_module(display)
    parser.add_argument('--display-profile',choices=display.PROFILES,default=display.DEFAULT,
                        help='GRUB default: stock panel/link 2.7-60 (default) or experimental 5.4-65')
    parser.add_argument('--edp-module',type=Path,default=R/'out/display-refresh/msm-edp-rates.ko',
                        help='Hash-pinned tested V6 module; both profiles are packaged')
    parser.add_argument('--edp-edid',type=Path,default=R/'out/display-refresh/default65-r2/sq2-native65.bin')
    parser.add_argument('--gpu-baseline',type=Path,default=O/'sc8180x-surface-pro-x.dtb',
                        help='Pinned SQ2 CPU/661 baseline; emits boot-tested700/432 default')
    parser.add_argument('--gpu-rescue',type=Path,default=O/'sc8180x-surface-pro-x-rescue.dtb')
    args=parser.parse_args()
    touch_files, touch_options = touch_bundle(R, args.ghost_touch_fix, args.touch_calibration)
    output=args.output
    output.parent.mkdir(parents=True,exist_ok=True)
    gpu_spec=importlib.util.spec_from_file_location('gpu_payload',R/'scripts/sq2-gpu-boot-payload.py')
    gpu=importlib.util.module_from_spec(gpu_spec);gpu_spec.loader.exec_module(gpu)
    if any((output.parent/n).exists() for n in (output.name,display.ADDON,'build-audit.json',gpu.NORMAL,gpu.RESCUE)):
        raise ValueError('Refusing existing output/addon/audit; use a fresh directory')
    gpu_normal=gpu.build(args.gpu_baseline.read_bytes())
    gpu_rescue=args.gpu_rescue.read_bytes()
    gpu_profile=gpu.verify(gpu_normal,gpu_rescue)
    module=args.edp_module.read_bytes();edid=args.edp_edid.read_bytes()
    display.verify_module(module)
    display.verify_edid(edid)
    base = (args.device_base or R/'out/password-ssh/combined.cpio.gz').read_bytes()
    assert sha(base) == (DEVICE_BASE_SHA if args.device_base else '651f3831a0171908d7a9b1054a4c1c2a8fd657c36e79e7e3717674ef6389ecf8')
    before = c.entries(gzip.decompress(base)); delta = {}
    def put(n, data, mode=0o100644):
        parent = Path(n).parent.as_posix()
        if parent != '.' and parent not in before and parent not in delta:
            put(parent, b'', 0o40755)
        delta[n] = (mode, data)
    def source(n): return (R/n).read_bytes().replace(b'\r\n', b'\n')
    init = source('probe/test2/init-googlebook-native')
    anchor = b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n'
    assert init.count(anchor) == 1 and b'closed-vulkan-start' not in init
    init = init.replace(anchor, b'closed-vulkan-start > /logs/closed-vulkan.txt 2>&1 || fail closed-vulkan-overlay\n'+anchor)
    video_spec=importlib.util.spec_from_file_location('video_payload',R/'scripts/sq2-video-boot-payload.py')
    video=importlib.util.module_from_spec(video_spec);video_spec.loader.exec_module(video)
    video_sha=DEVICE_BASE_SHA if args.device_base else video.PAYLOAD_SHA
    init=video.integrate(R,c,put,init,args.device_base,video_sha)
    usb_spec=importlib.util.spec_from_file_location('usb_payload',R/'scripts/sq2-usb-boot-payload.py')
    usb=importlib.util.module_from_spec(usb_spec);usb_spec.loader.exec_module(usb)
    init=usb.integrate(R,put,init)
    put('init', init, 0o100755)
    bt_spec=importlib.util.spec_from_file_location('bt_payload',R/'scripts/sq2-bluetooth-boot-payload.py')
    bt=importlib.util.module_from_spec(bt_spec);bt_spec.loader.exec_module(bt)
    bt_bridge,bt_build=bt.build_bridge(R)
    put('bin/sq2-hci-bridge',bt_bridge,0o100755)
    # Refresh existing shell helpers from repository; include new boot helpers.
    helpers = {Path(n).name for n in before if n.startswith('bin/') and (R/'probe/test2'/Path(n).name).is_file() and before[n][1].startswith(b'#!')}
    helpers |= {'cpu-frequency-start','resource-monitor-start','audio-speaker-route','audio-microphone-route','prepare-native-ssd','ssh-framework-boot','page-mode-notification-setup',
                'bluetooth-input-watch','touch-services','refresh-iptsd-input-nodes','touch-framework-ready','touch-resume-watch'}
    # These destinations were deliberately mapped to runtime helpers above.
    # Never overwrite video-codec-manifest-stage with the legacy lab implementation.
    helpers -= set(video.HELPERS)
    for name in sorted(helpers):
        body = source('probe/test2/'+name)
        subprocess.run(['sh','-n'], input=body, check=True)
        put('bin/'+name, body, 0o100755)
    subprocess.run(['sh','-n'],input=init,check=True)
    for name, dest in [('iptsd','bin/iptsd'), ('iptsd.conf','etc/iptsd.conf')]:
        body = touch_files[name]
        put(dest, body, 0o100755 if name == 'iptsd' else 0o100644)
    calibration_build = None
    if args.touch_calibration == 'on':
        cal_spec=importlib.util.spec_from_file_location('calibration_payload',R/'scripts/build-touch-calibration.py')
        cal=importlib.util.module_from_spec(cal_spec);cal_spec.loader.exec_module(cal)
        cal_files,calibration_build=cal.build(R)
        for name,(mode,body) in cal_files.items(): put(name,body,mode)
    hal = (O/'audio-hal').read_bytes()
    assert sha(hal) == '774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea'
    put('etc/sq2-audio/audio-hal',hal,0o100755)
    put('etc/sq2-audio/primary_audio_policy_configuration.xml',source('configs/primary_audio_policy_sq2.xml'))
    put('etc/sq2-monitor/thermal-info.json',source('configs/sq2-thermal-info.json'))
    put('etc/sq2-monitor/sq2_battery_compat.ko',(R/'out/resource-monitor/sq2_battery_compat.ko').read_bytes())
    put('lib/modules/6.3.8-sq2-android1/kernel/drivers/thermal/qcom/qcom_tsens.ko',(R/'out/gpu-opp/qcom_tsens.ko').read_bytes())
    for name in ('sq2-ssd-layout','sq2-ssd-partitions','sq2-ssd-super-slices'):
        put('etc/'+name,(R/'out/ssd'/name).read_bytes())
    mesa = (R/'out/sq2-turnip/runtime/vulkan.freedreno.so').read_bytes()
    egl = before['etc/googlebook-security/lib64/egl/libEGL_freedreno.so'][1]
    put('etc/googlebook-security/lib64/hw/vulkan.freedreno.so',mesa)
    n = 'etc/googlebook-security/build.prop'
    prop = before[n][1]
    assert (b'ro.hardware.vulkan=none\n' in prop or
            (args.device_base and b'ro.hardware.vulkan=freedreno\n' in prop))
    put(n, prop.replace(b'ro.hardware.vulkan=none\n',b'ro.hardware.vulkan=freedreno\n'))
    prefix='etc/googlebook-security/etc/permissions/'
    put(prefix+'android.hardware.vulkan.version-1_4.xml',b'<permissions/>\n')
    put(prefix+'android.software.vulkan.deqp.level.xml',b'<permissions/>\n')
    put(prefix+'android.hardware.vulkan.version-1_3.xml',b'<permissions><feature name="android.hardware.vulkan.version" version="4206592"/></permissions>\n')
    bundle = R/'out/quest2-gpu/vulkan-runtime-v8'
    audit=json.loads((bundle/'candidate.json').read_text())
    for name, expected in audit['sha256'].items():
        data=(bundle/name).read_bytes(); assert sha(data)==expected
        put('etc/sq2-vulkan-closed/'+name,data)
    for name, data in [('SHA256SUMS',(bundle/'SHA256SUMS').read_bytes()),
                       ('sq2-closed-vulkan.rc',source('configs/sq2-closed-vulkan.rc')),
                       ('vulkan-version.xml',b'<permissions><feature name="android.hardware.vulkan.version" version="4198400"/></permissions>\n')]:
        put('etc/sq2-vulkan-closed/'+name,data)
    # Include all runtime configuration in the helper's pre-mutation checksum gate.
    names={n.removeprefix('etc/sq2-vulkan-closed/'):b for n,(m,b) in delta.items() if n.startswith('etc/sq2-vulkan-closed/') and m==0o100644 and not n.endswith('SHA256SUMS')}
    put('etc/sq2-vulkan-closed/SHA256SUMS',''.join(f'{sha(b)}  {n}\n' for n,b in sorted(names.items())).encode())
    helper=source('probe/test2/closed-vulkan-start').replace(b'0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c',sha(mesa).encode()).replace(b'f16302e33e4f7c8bbaccbe5ab5fd5d19f3279222ee430ace236c4bad237a5434',sha(egl).encode())
    put('bin/closed-vulkan-start',helper,0o100755)
    rest=base; credential=None
    while rest:
        dec=zlib.decompressobj(31); files=c.entries(dec.decompress(rest)); assert dec.eof
        if 'etc/shadow' in files and 'bin/dropbear' in files:
            credential=rest[:len(rest)-len(dec.unused_data)]
        rest=dec.unused_data
    assert credential
    archive=b''.join(c.entry(n,b,m) for n,(m,b) in delta.items())+c.entry('TRAILER!!!',b'',0)
    archive+=b'\0'*(-len(archive)%512)
    result=base+gzip.compress(archive,mtime=0)+credential
    after=c.entries(gzip.decompress(result))
    assert all(after[n]==value for n,value in delta.items())
    assert all(after[n]==value for n,value in before.items() if n not in delta)
    assert after['etc/shadow']==before['etc/shadow'] and after['bin/dropbear']==before['bin/dropbear']
    usb_hashes=usb.verify(after,R)
    video.verify_helpers(after,R)
    audio_route=source('probe/test2/audio-speaker-route')
    assert after['bin/audio-speaker-route']==(0o100755,audio_route)
    for channel in (7,8):
        assert audio_route.count(f"mix 'RX{channel} Digital Volume' 84\n".encode())==1
    addon=display.build_addon(result,c,module,edid)
    image=(R/'out/screen-only/Image').read_bytes()
    assert b'SQ2 screen-only policy: system suspend blocked' in image
    with output.open('xb') as f: f.write(result)
    with (output.parent/display.ADDON).open('xb') as f: f.write(addon)
    shutil.copyfile(R/'out/screen-only/Image',output.parent/'Image')
    for name,data in ((gpu.NORMAL,gpu_normal),(gpu.RESCUE,gpu_rescue)):
        with (output.parent/name).open('xb') as f: f.write(data)
    report=dict(base_sha256=sha(base),initramfs_sha256=sha(result),Image_sha256=sha(image),
                ghost_touch_fix=args.ghost_touch_fix!='off',touch_mode=args.ghost_touch_fix,touch_build_options=touch_options,
                password_addon_last=True,changed=sorted(delta),gpu_profile=gpu_profile,
                hardware_boot_tested=False,hardware_decode_included=True,
                video_payload_sha256=video_sha,
                usb_functions_included=True,bluetooth_input_included=True,
                bluetooth_bridge_build=bt_build,
                touch_calibration_build=calibration_build,
                usb_bluetooth_source_sha256=usb_hashes,
                speaker_rx_raw=[84,84],speaker_rx_db=0,speaker_gain_change_db=12,
                speaker_route_sha256=sha(audio_route),
                display_profile=args.display_profile,display_profiles=list(display.PROFILES),
                display_stock_module_sha256=display.STOCK_SHA,display_edp65_module_sha256=sha(module),
                display_edp65_edid_sha256=sha(edid),display_addon_sha256=sha(addon),
                proprietary_libraries='pinned binary inputs, not source-built')
    (output.parent/'build-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='changed'}))

if __name__=='__main__': main()
