"""Default Surface sensor integration; keep fallback and unrelated runtime intact."""
from pathlib import Path
import hashlib, io, json, struct, subprocess, sys, zipfile, zlib
from disk_image import module, require
from kernel_baseline import ROOT, policy

FIRMWARE = 'firmware/qcom/msft/surface/pro-x/qcslpi8180.mbn'
FILES = ('modules/spx-tcs3430.ko', 'modules/spx-slpi-pas.ko', 'modules/fastrpc.ko',
         'sensors.spx.so', 'spx-sensors-overlay.apk', 'hexagonrpcd',
         'spx-tcs3430-golden.bin', 'qebspilaa64.efi', FIRMWARE)

def load(bundle):
    bundle = Path(bundle)
    manifest = json.loads((bundle/'sensors-bundle.json').read_text())
    if manifest.get('schema') != 1:
        raise ValueError('Unsupported sensor bundle schema')
    names = set(manifest['sha256'])
    if not set(FILES) <= names or 'config/config_list.txt' not in names:
        raise ValueError('Incomplete sensor bundle')
    for name in names - set(FILES):
        if (not name.startswith('config/') or len(Path(name).parts) != 2 or
            not (name.endswith('.json') or name == 'config/config_list.txt')):
            raise ValueError('Unexpected sensor bundle path: '+name)
    files = {name: (bundle/name).read_bytes() for name in sorted(names)}
    for name, data in files.items():
        if not data or hashlib.sha256(data).hexdigest() != manifest['sha256'][name]:
            raise ValueError('Sensor bundle integrity: '+name)
    for name in tuple(n for n in FILES if n.endswith(('.ko', '.so'))) + ('hexagonrpcd',):
        data=files[name]
        require(data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 183,
                'Sensor input is not ARM64 ELF: '+name)
    cal=files['spx-tcs3430-golden.bin']
    require(len(cal)==1480 and (zlib.crc32(cal[:1476])^0xffffffff)==int.from_bytes(cal[1476:], 'little'),
            'Invalid model light calibration')
    with zipfile.ZipFile(io.BytesIO(files['spx-sensors-overlay.apk'])) as apk:
        require(apk.testzip() is None and {'AndroidManifest.xml', 'resources.arsc'} <= set(apk.namelist()),
                'Invalid sensor framework overlay')
    efi = files['qebspilaa64.efi']
    if len(efi) < 64 or efi[:2] != b'MZ':
        raise ValueError('Sensor EFI must be an ARM64 PE image')
    pe = struct.unpack_from('<I', efi, 60)[0]
    if efi[pe:pe+6] != b'PE\0\0\x64\xaa':
        raise ValueError('Sensor EFI must be an ARM64 PE image')
    for name in (n for n in FILES if n.endswith(".ko")):
        magic=subprocess.check_output(["modinfo", "-F", "vermagic", str(bundle/name)],text=True)
        require(magic.split()[0] == policy()["kernel_release"], "Sensor module ABI mismatch: "+name)
    return files

BOOT = b'if [ "$rescue" = 1 ]; then\n'
BOOT_HOOK = b'''if [ "$smoke" = 0 ] && [ "$ssd_smoke" = 0 ] && [ "$rescue" = 0 ]; then
    /bin/spx-sensors-boot > /logs/sensors-boot.txt 2>&1 || echo 'SPX_SENSORS_NOT_READY: see RAM log'
fi
'''
STAGE = b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n'
STAGE_HOOK = b'spx-sensors-stage > /logs/sensors-stage.txt 2>&1 || echo SPX_SENSORS_STAGE_FAILED\n'


def payload_paths(files):
    paths={"modules/"+n+".ko": "etc/spx-sensors/modules/"+n+".ko"
           for n in ("spx-tcs3430", "spx-slpi-pas", "fastrpc")}
    paths.update({n: "etc/spx-sensors/userspace/"+n for n in
                  ("sensors.spx.so", "spx-sensors-overlay.apk", "spx-tcs3430-golden.bin")})
    paths["hexagonrpcd"]="bin/spx-sensor-rpcd"
    paths.update({n: "etc/spx-sensors/rpc-root/sensors/"+n for n in files if n.startswith("config/")})
    return paths


def install(records, bodies, files, new_record):
    init=bodies["init"]
    if b"/bin/spx-sensors-boot " not in init:
        require(init.count(BOOT)==1, "Missing sensor module boot anchor")
        init=init.replace(BOOT, BOOT_HOOK+BOOT)
    if b"spx-sensors-stage >" not in init:
        require(init.count(STAGE)==1, "Missing sensor userspace stage anchor")
        init=init.replace(STAGE, STAGE+STAGE_HOOK)
    bodies["init"]=init
    for source,target in payload_paths(files).items():
        bodies[target]=files[source]
        records[target]=new_record(0o100755 if source=="hexagonrpcd" else 0o100644)
    for name in ("spx-sensors-boot", "spx-sensors-stage"):
        bodies["bin/"+name]=(ROOT/"probe/sensors"/name).read_bytes()
        records["bin/"+name]=new_record(0o100755)
    for name in ("etc/spx-sensors/rpc-root/sensors/registry",):
        bodies[name]=b"";records[name]=new_record(0o040755)


def rebuild_modules(files, source, build, output, cross, run):
    for script,folder,names in (
        ("build-spx-sensors.sh", "sensor-als", ("spx-tcs3430",)),
        ("build-spx-slpi.py", "sensor-slpi", ("spx-slpi-pas", "fastrpc"))):
        dest=output/folder
        command=["sh"] if script.endswith(".sh") else [sys.executable]
        run(["env", "CROSS_COMPILE="+cross]+command+[ROOT/"scripts"/script, source, build, dest])
        for name in names:files["modules/"+name+".ko"]=(dest/(name+".ko")).read_bytes()


def prepare_dtb(data, files):
    data=module("sensor_als_dt", "prepare-spx-als-dtb.py").prepare(data)
    return module("sensor_slpi_dt", "prepare-spx-slpi-dtb.py").prepare(
        data, files[FIRMWARE], FIRMWARE.removeprefix("firmware/"))
