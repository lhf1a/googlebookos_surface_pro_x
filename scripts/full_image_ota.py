"""OTA runtime integration for Hybrid full-image initramfs builds.

Installs:
- /bin/spx-ota-tool   (static ARM64 ELF: SHA-256, GPT inspect, streaming extractor)
- /bin/spx-ota-stage  (early-boot offline OTA applier, runs before super/data mount)
- /bin/spx-ota-boot   (Android root OTA bridge daemon + Settings update handler claim)
- /etc/spx-ota-info   (build version, commit, kernel release, baseline super hash)
"""
from datetime import datetime, timezone
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from disk_image import require
from kernel_baseline import ROOT, policy

OTA_DIR = ROOT / 'probe/ota'
OTA_TOOL_SRC = OTA_DIR / 'spx-ota-tool.c'
OTA_STAGE_SRC = OTA_DIR / 'spx-ota-stage'
OTA_BOOT_SRC = OTA_DIR / 'spx-ota-boot'
INTERNAL_INSTALL_SRC = OTA_DIR / 'spx-internal-install'

EARLY_ANCHOR = b'base=/media/data/googlebook-stage\n'
LEGACY_EARLY_HOOK = (
    b'if [ "$ssd" = 1 ] && [ "$rescue" = 0 ] && [ "$smoke" = 0 ] && [ -f /media/esp/SPX-OTA-PENDING ]; then\n'
    b'    /bin/spx-ota-stage > /logs/spx-ota-stage.txt 2>&1 || echo SPX_OTA_STAGE_FAILED_NONFATAL\n'
    b'fi\n'
)
EARLY_HOOK = (
    b'if [ "$ssd" = 1 ] && [ "$rescue" = 0 ] && [ "$smoke" = 0 ] && { [ -f /media/esp/SPX-OTA-PENDING ] || [ -f /media/esp/SPX-OTA-RESULT.txt ]; }; then\n'
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
    if LEGACY_EARLY_HOOK in init:
        init = init.replace(LEGACY_EARLY_HOOK, EARLY_HOOK, 1)
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


def _sanitize_build_token(value):
    text = re.sub(r'[^A-Za-z0-9._+-]+', '-', str(value or '').strip())
    text = re.sub(r'-{2,}', '-', text)
    return text.strip('.-')


def _normalize_yymmdd(build_date):
    raw = str(build_date or '').strip()
    digits = re.sub(r'[^0-9]+', '', raw)
    if len(digits) >= 8 and digits.startswith('20'):
        return digits[2:8]
    if len(digits) == 6:
        return digits
    sanitized = _sanitize_build_token(raw)
    return sanitized or datetime.now(timezone.utc).strftime('%y%m%d')


def format_ota_build_suffix(version, build_date, source_commit='', super_sha256='', codename='poipu'):
    """Build a concise, space-free OTA suffix (<version>.<YYMMDD>[.<commit7>]) for ro.build.display.id."""
    _ = (codename, super_sha256)
    ver = _sanitize_build_token(version) or '6.18-hybrid'
    bdate = _normalize_yymmdd(build_date)
    parts = [ver]
    if bdate and bdate not in ver:
        parts.append(bdate)

    commit_clean = re.sub(r'[^0-9a-fA-F]+', '', str(source_commit or '').strip()).lower()
    commit7 = commit_clean[:7]
    if commit7 and commit7 not in ver.lower() and not re.fullmatch(r'v[0-9]+(?:\.[0-9]+)+', ver):
        parts.append(commit7)

    return '.'.join(parts)


def format_ota_info(version, source_commit, kernel_release, super_sha256='', build_date=None):
    """Render etc/spx-ota-info contents with canonical OTA build display metadata."""
    ver = (version or '').strip() or detect_version(source_commit)
    commit = (source_commit or '').strip()
    krel = (kernel_release or '').strip()
    bdate = _normalize_yymmdd(build_date)
    super_val = (super_sha256 or '').strip().lower()
    display = format_ota_build_suffix(ver, bdate, source_commit=commit, super_sha256=super_val)
    return (
        f'SPX_OTA_VERSION={ver}\n'
        f'SPX_OTA_COMMIT={commit}\n'
        f'SPX_OTA_KERNEL={krel}\n'
        f'SPX_OTA_BUILD_DATE={bdate}\n'
        f'SPX_OTA_SUPER_SHA256={super_val}\n'
        f'SPX_OTA_BUILD_DISPLAY={display}\n'
    ).encode('utf-8')


def update_ota_info_super_sha256(info_bytes, super_sha256):
    """Update SPX_OTA_SUPER_SHA256 and recompute SPX_OTA_BUILD_DISPLAY on existing spx-ota-info."""
    text = info_bytes.decode('utf-8', errors='replace').replace('\r\n', '\n')
    kv = {}
    order = []
    for line in text.splitlines():
        if '=' not in line:
            continue
        k, v = line.split('=', 1)
        k = k.strip()
        if not k:
            continue
        if k not in kv:
            order.append(k)
        kv[k] = v.strip()

    ver = kv.get('SPX_OTA_VERSION') or '6.18-hybrid'
    commit = kv.get('SPX_OTA_COMMIT', '')
    bdate = _normalize_yymmdd(kv.get('SPX_OTA_BUILD_DATE', ''))
    super_val = (super_sha256 or '').strip().lower()
    display = format_ota_build_suffix(ver, bdate, source_commit=commit, super_sha256=super_val)

    kv['SPX_OTA_BUILD_DATE'] = bdate
    kv['SPX_OTA_SUPER_SHA256'] = super_val
    kv['SPX_OTA_BUILD_DISPLAY'] = display
    for req_key in ('SPX_OTA_VERSION', 'SPX_OTA_COMMIT', 'SPX_OTA_KERNEL', 'SPX_OTA_BUILD_DATE', 'SPX_OTA_SUPER_SHA256', 'SPX_OTA_BUILD_DISPLAY'):
        if req_key not in order:
            order.append(req_key)
    return ''.join(f'{k}={kv.get(k, "")}\n' for k in order).encode('utf-8')


def install(
    records,
    bodies,
    new_record,
    compiled_ota_tool=None,
    version=None,
    source_commit=None,
    super_sha256='',
    build_date=None,
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
    install_bytes = INTERNAL_INSTALL_SRC.read_bytes().replace(b'\r\n', b'\n')
    subprocess.run(['sh', '-n'], input=stage_bytes, check=True, capture_output=True)
    subprocess.run(['sh', '-n'], input=boot_bytes, check=True, capture_output=True)
    subprocess.run(['sh', '-n'], input=install_bytes, check=True, capture_output=True)

    ver = version or detect_version(source_commit)
    commit = (source_commit or '').strip()
    krel = policy()['kernel_release']
    info_text = format_ota_info(
        ver,
        commit,
        krel,
        super_sha256=super_sha256,
        build_date=build_date,
    )

    bodies['bin/spx-ota-tool'] = compiled_ota_tool
    records['bin/spx-ota-tool'] = new_record(0o100755)
    bodies['bin/spx-ota-stage'] = stage_bytes
    records['bin/spx-ota-stage'] = new_record(0o100755)
    bodies['bin/spx-ota-boot'] = boot_bytes
    records['bin/spx-ota-boot'] = new_record(0o100755)
    bodies['bin/spx-internal-install'] = install_bytes
    records['bin/spx-internal-install'] = new_record(0o100755)
    bodies['etc/spx-ota-info'] = info_text
    records['etc/spx-ota-info'] = new_record(0o100644)

    if 'init' in bodies:
        bodies['init'] = patch_init_for_ota(bodies['init'])

    for name in ('bin/spx-ota-tool', 'bin/spx-ota-stage', 'bin/spx-ota-boot', 'bin/spx-internal-install', 'etc/spx-ota-info'):
        for parent in Path(name).parents:
            key = parent.as_posix()
            if key != '.' and key not in records:
                records[key] = new_record(0o040755)
                bodies[key] = b''
