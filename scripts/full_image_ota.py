"""OTA runtime integration for Hybrid full-image initramfs builds.

Installs:
- /bin/spx-ota-tool   (static ARM64 ELF: SHA-256, GPT inspect, streaming extractor)
- /bin/spx-ota-stage  (early-boot offline OTA applier, runs before super/data mount)
- /bin/spx-ota-boot   (Android root OTA bridge daemon + Settings update handler claim)
- /etc/spx-ota-info   (build version, commit, kernel release, baseline super hash)
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from disk_image import require
from kernel_baseline import ROOT, policy

OTA_DIR = ROOT / 'probe/ota'
OTA_TOOL_SRC = OTA_DIR / 'spx-ota-tool.c'
OTA_STAGE_SRC = OTA_DIR / 'spx-ota-stage'
OTA_BOOT_SRC = OTA_DIR / 'spx-ota-boot'

EARLY_ANCHOR = b'base=/media/data/googlebook-stage\n'
EARLY_HOOK = (
    b'if [ "$ssd" = 1 ] && [ "$rescue" = 0 ] && [ "$smoke" = 0 ] && [ -f /media/esp/SPX-OTA-PENDING ]; then\n'
    b'    /bin/spx-ota-stage > /logs/spx-ota-stage.txt 2>&1 || echo SPX_OTA_STAGE_FAILED_NONFATAL\n'
    b'fi\n'
)

HANDOFF_ANCHOR = b'exec native-handoff /newroot\n'
BOOT_HOOK = b'/bin/spx-ota-boot > /logs/spx-ota-boot.txt 2>&1 &\n'


def build_ota_tool_bytes(cross_compile='aarch64-linux-gnu-'):
    """Compile probe/ota/spx-ota-tool.c as a static ARM64 ELF binary."""
    cc = f'{cross_compile}gcc'
    strip = f'{cross_compile}strip'
    with tempfile.TemporaryDirectory(prefix='spx-ota-tool-build-') as tmp:
        out_bin = Path(tmp) / 'spx-ota-tool'
        subprocess.run(
            [cc, '-std=c11', '-static', '-Os', '-Wall', '-Wextra', '-Werror',
             str(OTA_TOOL_SRC), '-o', str(out_bin)],
            check=True,
        )
        if shutil.which(strip):
            subprocess.run([strip, str(out_bin)], check=True)
        data = out_bin.read_bytes()
    require(
        data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 183,
        'Compiled spx-ota-tool is not an ARM64 ELF binary',
    )
    return data


def patch_init_for_ota(init_bytes):
    """Wire early-boot offline OTA hook and post-boot OTA daemon into /init."""
    init = init_bytes
    if EARLY_ANCHOR in init and EARLY_HOOK not in init:
        require(init.count(EARLY_ANCHOR) == 1, 'Ambiguous pre-mount anchor in init')
        init = init.replace(EARLY_ANCHOR, EARLY_HOOK + EARLY_ANCHOR, 1)
    if HANDOFF_ANCHOR in init and BOOT_HOOK not in init:
        require(init.count(HANDOFF_ANCHOR) == 1, 'Ambiguous Android handoff anchor in init')
        init = init.replace(HANDOFF_ANCHOR, BOOT_HOOK + HANDOFF_ANCHOR, 1)
    subprocess.run(['sh', '-n'], input=init, check=True, capture_output=True)
    return init


def detect_version(source_commit=None):
    """Return a clean human-readable version string for this build."""
    try:
        desc = subprocess.check_output(
            ['git', 'describe', '--tags', '--always'],
            cwd=ROOT, text=True, stderr=subprocess.DEVNULL,
        ).strip()
        if desc:
            return desc
    except Exception:
        pass
    if source_commit:
        return '6.18-hybrid-' + str(source_commit)[:8]
    return '6.18-' + policy()['baseline'] + '-hybrid'


def install(
    records,
    bodies,
    new_record,
    compiled_ota_tool=None,
    version=None,
    source_commit=None,
    super_sha256='',
):
    """Install OTA tool, early-boot applier, Android root daemon, and metadata."""
    if compiled_ota_tool is None:
        compiled_ota_tool = build_ota_tool_bytes()
    else:
        require(
            compiled_ota_tool[:6] == b'\x7fELF\x02\x01'
            and int.from_bytes(compiled_ota_tool[18:20], 'little') == 183,
            'Provided spx-ota-tool is not an ARM64 ELF binary',
        )

    stage_bytes = OTA_STAGE_SRC.read_bytes().replace(b'\r\n', b'\n')
    boot_bytes = OTA_BOOT_SRC.read_bytes().replace(b'\r\n', b'\n')
    subprocess.run(['sh', '-n'], input=stage_bytes, check=True, capture_output=True)
    subprocess.run(['sh', '-n'], input=boot_bytes, check=True, capture_output=True)

    ver = version or detect_version(source_commit)
    commit = (source_commit or '').strip()
    krel = policy()['kernel_release']
    info_text = (
        f'SPX_OTA_VERSION={ver}\n'
        f'SPX_OTA_COMMIT={commit}\n'
        f'SPX_OTA_KERNEL={krel}\n'
        f'SPX_OTA_SUPER_SHA256={super_sha256 or ""}\n'
    ).encode('utf-8')

    bodies['bin/spx-ota-tool'] = compiled_ota_tool
    records['bin/spx-ota-tool'] = new_record(0o100755)
    bodies['bin/spx-ota-stage'] = stage_bytes
    records['bin/spx-ota-stage'] = new_record(0o100755)
    bodies['bin/spx-ota-boot'] = boot_bytes
    records['bin/spx-ota-boot'] = new_record(0o100755)
    bodies['etc/spx-ota-info'] = info_text
    records['etc/spx-ota-info'] = new_record(0o100644)

    if 'init' in bodies:
        bodies['init'] = patch_init_for_ota(bodies['init'])

    for name in ('bin/spx-ota-tool', 'bin/spx-ota-stage', 'bin/spx-ota-boot', 'etc/spx-ota-info'):
        for parent in Path(name).parents:
            key = parent.as_posix()
            if key != '.' and key not in records:
                records[key] = new_record(0o040755)
                bodies[key] = b''
