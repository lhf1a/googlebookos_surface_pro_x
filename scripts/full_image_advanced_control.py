"""Build/install Surface Advanced Settings APK and its root/init bridge daemon."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
APK = 'app/SurfaceAdvancedControl/SurfaceAdvancedControl.apk'
BRIDGE_SRC = ROOT / 'probe/test2/spx-advanced-control-boot'
BRIDGE_DST = 'bin/spx-advanced-control-boot'
AVF_VPN_WATCH_SRC = ROOT / 'probe/test2/spx-avf-vpn-watch.c'
AVF_VPN_WATCH_DST = 'bin/spx-avf-vpn-watch'
AVF_VPN_RECONCILE_SRC = ROOT / 'probe/test2/spx-avf-vpn-reconcile'
AVF_VPN_RECONCILE_DST = 'bin/spx-avf-vpn-reconcile'
INIT_ANCHOR = b'exec native-handoff /newroot\n'
INIT_HOOK = b'/bin/spx-advanced-control-boot > /logs/spx-advanced-control.txt 2>&1 &\n'
QTI_STAGE_DST = 'bin/qti-system-display-stage'
QTI_STAGE_OLD_COPY = b'    cp -p "$source/root/$name" "$target"\n'
QTI_STAGE_APK_MARKER = b'        *.apk)\n            cp "$source/root/$name" "$target"\n'
QTI_STAGE_NEW_COPY = (
    b'    case "$name" in\n'
    b'        *.apk)\n'
    b'            cp "$source/root/$name" "$target"\n'
    b'            chmod 644 "$target"\n'
    b'            now_epoch=$(date +%s 2>/dev/null || echo 0)\n'
    b"            case \"$now_epoch\" in ''|*[!0-9]*) now_epoch=0 ;; esac\n"
    b'            if [ "$now_epoch" -lt 1577836800 ]; then\n'
    b'                apk_stamp=""\n'
    b'                if [ -r /etc/spx-ota-info ]; then\n'
    b'                    apk_bdate=$(awk -F \'=\' \'$1 == "SPX_OTA_BUILD_DATE" { gsub(/[^0-9]/, "", $2); print $2; exit }\' /etc/spx-ota-info 2>/dev/null || true)\n'
    b'                    case "$apk_bdate" in\n'
    b'                        [0-9][0-9][0-9][0-9][0-9][0-9]) apk_stamp="20${apk_bdate}0000" ;;\n'
    b'                        20[0-9][0-9][0-9][0-9][0-9][0-9]*) apk_stamp="$(printf \'%s\' "$apk_bdate" | cut -c1-8)0000" ;;\n'
    b'                    esac\n'
    b'                fi\n'
    b'                touch -t "${apk_stamp:-202601010000}" "$target" "${target%/*}" 2>/dev/null || true\n'
    b'            fi\n'
    b'            ;;\n'
    b'        *)\n'
    b'            cp -p "$source/root/$name" "$target"\n'
    b'            ;;\n'
    b'    esac\n'
)


def patch_qti_stage_for_apk_mtime(stage_bytes):
    """Ensure staged *.apk payloads never retain initramfs 1970-01-01 mtime."""
    normalized = stage_bytes.replace(b'\r\n', b'\n')
    if QTI_STAGE_APK_MARKER in normalized:
        subprocess.run(['sh', '-n'], input=normalized, check=True, capture_output=True)
        return normalized
    if normalized.count(QTI_STAGE_OLD_COPY) != 1:
        raise ValueError('Unknown qti-system-display-stage INSTALL_FILES copy loop')
    patched = normalized.replace(QTI_STAGE_OLD_COPY, QTI_STAGE_NEW_COPY, 1)
    subprocess.run(['sh', '-n'], input=patched, check=True, capture_output=True)
    return patched


def _find_sdk():
    env_sdk = os.environ.get('SQ2_ANDROID_SDK')
    if env_sdk:
        return Path(env_sdk)
    for cand in (
        Path('/mnt/spx-workspace/toolchains/sq2-graphics/sdk'),
        Path('/root/.cache/sq2-graphics/sdk'),
    ):
        if (cand / 'android-35/android.jar').is_file() and (cand / 'android-15/aapt2').is_file():
            return cand
    return Path('/root/.cache/sq2-graphics/sdk')


def build_apk(output):
    sdk = _find_sdk()
    platform, tools = sdk / 'android-35/android.jar', sdk / 'android-15'
    key = Path(os.environ.get('SPX_ADVANCED_CONTROL_KEYSTORE',
                              str(ROOT / 'private/advanced-control/signing.keystore')))
    key.parent.mkdir(parents=True, exist_ok=True)
    if not key.exists():
        subprocess.run(['keytool', '-genkeypair', '-keystore', str(key),
                        '-storepass', 'android', '-keypass', 'android', '-alias', 'advanced-control',
                        '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
                        '-dname', 'CN=Surface Advanced Control'], check=True, capture_output=True)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    src = ROOT / 'probe/advanced-control'
    res_dir = src / 'res'
    with tempfile.TemporaryDirectory(prefix='spx-advanced-control-') as tmp:
        work = Path(tmp)
        (work / 'classes').mkdir()
        (work / 'dex').mkdir()
        subprocess.run(['javac', '--release', '8', '-encoding', 'UTF-8', '-classpath', str(platform),
                        '-d', str(work / 'classes'), *map(str, sorted(src.glob('*.java')))], check=True)
        subprocess.run([str(tools / 'd8'), '--min-api', '26', '--lib', str(platform),
                        '--output', str(work / 'dex'),
                        *map(str, sorted((work / 'classes').rglob('*.class')))], check=True)
        link_cmd = [str(tools / 'aapt2'), 'link', '--manifest', str(src / 'AndroidManifest.xml'),
                    '-I', str(platform), '--auto-add-overlay', '-o', str(work / 'unsigned.apk')]
        if res_dir.is_dir():
            res_zip = work / 'res.zip'
            subprocess.run([str(tools / 'aapt2'), 'compile', '--dir', str(res_dir),
                            '-o', str(res_zip)], check=True)
            link_cmd.append(str(res_zip))
        subprocess.run(link_cmd, check=True)
        with zipfile.ZipFile(work / 'unsigned.apk', 'a', zipfile.ZIP_DEFLATED) as archive:
            archive.write(work / 'dex/classes.dex', 'classes.dex')
        subprocess.run([str(tools / 'zipalign'), '-f', '-P', '16', '4',
                        str(work / 'unsigned.apk'), str(work / 'aligned.apk')], check=True)
        subprocess.run([str(tools / 'apksigner'), 'sign', '--ks', str(key), '--ks-pass', 'pass:android',
                        '--out', str(output), str(work / 'aligned.apk')], check=True)
        subprocess.run([str(tools / 'apksigner'), 'verify', str(output)], check=True)
    return output.read_bytes()


def build_avf_vpn_watch(output=None):
    def _compile(out_path):
        out_path = Path(out_path)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        cc = os.environ.get('SPX_AVF_VPN_CROSS_CC', 'aarch64-linux-gnu-gcc')
        env = dict(os.environ, SOURCE_DATE_EPOCH='0')
        subprocess.run([
            cc, '-std=c11', '-static', '-Os', '-Wall', '-Wextra', '-Werror',
            '-Wl,--build-id=none', '-frandom-seed=spx-avf-vpn-watch',
            f'-ffile-prefix-map={ROOT}=/src',
            str(AVF_VPN_WATCH_SRC), '-o', str(out_path),
        ], env=env, check=True)
        return out_path.read_bytes()

    if output is not None:
        return _compile(output)
    with tempfile.TemporaryDirectory(prefix='spx-avf-vpn-watch-') as tmp:
        return _compile(Path(tmp) / 'spx-avf-vpn-watch')


def install(records, bodies, new_record, apk=None, bridge=None,
            avf_vpn_watch=None, avf_vpn_reconcile=None):
    prefix = 'etc/sq2-qti-system/'
    listing = prefix + 'INSTALL_FILES'
    if listing not in bodies:
        raise ValueError('Advanced Control requires Hybrid QTI runtime')
    if apk is None:
        with tempfile.TemporaryDirectory(prefix='spx-control-payload-') as tmp:
            apk = build_apk(Path(tmp) / 'SurfaceAdvancedControl.apk')
    if bridge is None:
        bridge = BRIDGE_SRC.read_bytes().replace(b'\r\n', b'\n')
    subprocess.run(['sh', '-n'], input=bridge, check=True, capture_output=True)
    if avf_vpn_watch is None:
        avf_vpn_watch = build_avf_vpn_watch()
    if avf_vpn_reconcile is None:
        avf_vpn_reconcile = AVF_VPN_RECONCILE_SRC.read_bytes().replace(b'\r\n', b'\n')
    subprocess.run(['sh', '-n'], input=avf_vpn_reconcile, check=True, capture_output=True)

    init_updated = None
    if 'init' in bodies and INIT_ANCHOR in bodies['init']:
        init_bytes = bodies['init']
        if init_bytes.count(INIT_ANCHOR) != 1:
            raise ValueError('Ambiguous Android handoff anchor in init')
        if INIT_HOOK not in init_bytes:
            init_updated = init_bytes.replace(INIT_ANCHOR, INIT_HOOK + INIT_ANCHOR)
    qti_stage_updated = (
        patch_qti_stage_for_apk_mtime(bodies[QTI_STAGE_DST])
        if QTI_STAGE_DST in bodies
        else None
    )

    path = prefix + 'root/' + APK
    bodies[path] = apk
    records[path] = new_record(0o100644)
    lines = bodies[listing].splitlines()
    if APK.encode() not in lines:
        bodies[listing] = bodies[listing].rstrip(b'\n') + b'\n' + APK.encode() + b'\n'
    marker = 'etc/googlebook-security/etc/surface-vulkan-app-routing.version'
    bodies[marker] = b'1\n'
    records[marker] = new_record(0o100644)
    bodies[BRIDGE_DST] = bridge
    records[BRIDGE_DST] = new_record(0o100755)
    bodies[AVF_VPN_WATCH_DST] = avf_vpn_watch
    records[AVF_VPN_WATCH_DST] = new_record(0o100755)
    bodies[AVF_VPN_RECONCILE_DST] = avf_vpn_reconcile
    records[AVF_VPN_RECONCILE_DST] = new_record(0o100755)
    if init_updated is not None:
        bodies['init'] = init_updated
    if qti_stage_updated is not None:
        bodies[QTI_STAGE_DST] = qti_stage_updated
    for name in (path, marker, BRIDGE_DST, AVF_VPN_WATCH_DST, AVF_VPN_RECONCILE_DST):
        for parent in Path(name).parents:
            key = parent.as_posix()
            if key != '.' and key not in records:
                records[key] = new_record(0o040755)
                bodies[key] = b''


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = build_apk(args.output)
    print(f'ADVANCED_CONTROL_APK_OK {args.output.resolve()} ({len(data)} bytes)')
