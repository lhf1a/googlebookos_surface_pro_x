"""Rescue-mode payload integration for Hybrid full-image initramfs builds.

Installs:
- Rescue-only SSH setup (pubkey auth + default root password 000000; normal boot stays locked)
- Rescue Wi-Fi & USB Ethernet loader/watcher + optional lower-port USB NCM gadget
- Interactive static ARM64 Wi-Fi selection & login TUI (/bin/spx-wifi-tui, /bin/wifi-tui)
- start-android / continue-android command and PID 1 Wi-Fi/USB ownership handoff
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from disk_image import require
from kernel_baseline import ROOT

RESCUE_DIR = ROOT / 'probe/rescue'
WIFI_TUI_SRC = RESCUE_DIR / 'spx-wifi-tui.c'

RESCUE_SCRIPTS = (
    'spx-rescue-ssh',
    'spx-rescue-net',
    'spx-rescue-usb-gadget',
    'spx-rescue-session',
    'spx-rescue-shell',
    'start-android',
    'spx-rescue-handoff',
)

RESCUE_ALIASES = {
    'bin/wifi-tui': b'#!/bin/sh\nexec /bin/spx-wifi-tui "$@"\n',
    'bin/continue-android': b'#!/bin/sh\nexec /bin/start-android "$@"\n',
}

LEGACY_DEBUG_WIFI_ANCHOR = (
    b'    if [ "$debug" = 1 ]; then\n'
    b"        echo 'Preparing Wi-Fi and diagnostic SSH (maximum 120 seconds)...'\n"
)
NEW_DEBUG_WIFI_ANCHOR = (
    b'    if [ "$debug" = 1 ] && [ "$rescue" = 0 ]; then\n'
    b"        echo 'Preparing Wi-Fi and diagnostic SSH (maximum 120 seconds)...'\n"
)

RESCUE_BANNER_ANCHOR = (
    b'if [ "$rescue" = 1 ]; then\n'
    b"    echo 'SQ2_SSD_RESCUE_READY: SSH and USB logs; Android/data not mounted.' | tee -a /logs/status.txt\n"
)
LEGACY_RESCUE_BLOCK = (
    RESCUE_BANNER_ANCHOR
    + b'    while true; do setsid cttyhack /bin/sh; sleep 1; done\n'
    + b'fi\n'
)
NEW_RESCUE_BLOCK = (
    RESCUE_BANNER_ANCHOR
    + b'    /bin/spx-rescue-session || true\n'
    + b'    if [ -e /run/sq2-rescue-continue ]; then\n'
    + b'        . /bin/spx-rescue-handoff\n'
    + b'    else\n'
    + b'        while true; do setsid cttyhack /bin/sh; sleep 1; done\n'
    + b'    fi\n'
    + b'fi\n'
)


def build_wifi_tui_bytes(cross_compile='aarch64-linux-gnu-'):
    """Compile probe/rescue/spx-wifi-tui.c as a static ARM64 ELF binary."""
    cc = f'{cross_compile}gcc'
    strip = f'{cross_compile}strip'
    with tempfile.TemporaryDirectory(prefix='spx-wifi-tui-build-') as tmp:
        out_bin = Path(tmp) / 'spx-wifi-tui'
        subprocess.run(
            [cc, '-static', '-Os', '-Wall', '-Wextra', '-Werror',
             str(WIFI_TUI_SRC), '-o', str(out_bin)],
            check=True,
        )
        if shutil.which(strip):
            subprocess.run([strip, str(out_bin)], check=True)
        data = out_bin.read_bytes()
    require(
        data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 183,
        'Compiled spx-wifi-tui is not an ARM64 ELF binary',
    )
    return data


def patch_init_for_rescue(init_bytes):
    """Wire rescue session and Android continuation handoff into initramfs /init.

    Preserves the exact `if [ "$rescue" = 1 ]; then\\n    echo ` anchor required by
    full_image_camera, full_image_sensors and disk_image.typec_early_handoff.
    Minimal synthetic unit-test init fixtures without a rescue block are returned unchanged.
    """
    if b'if [ "$rescue" = 1 ]; then\n' not in init_bytes:
        return init_bytes

    init = init_bytes
    if NEW_DEBUG_WIFI_ANCHOR not in init and LEGACY_DEBUG_WIFI_ANCHOR in init:
        require(init.count(LEGACY_DEBUG_WIFI_ANCHOR) == 1, 'Ambiguous debug Wi-Fi init block')
        init = init.replace(LEGACY_DEBUG_WIFI_ANCHOR, NEW_DEBUG_WIFI_ANCHOR, 1)

    if NEW_RESCUE_BLOCK not in init:
        require(init.count(LEGACY_RESCUE_BLOCK) == 1, 'Unknown rescue block in init')
        init = init.replace(LEGACY_RESCUE_BLOCK, NEW_RESCUE_BLOCK, 1)

    subprocess.run(['sh', '-n'], input=init, check=True, capture_output=True)
    return init


def install(records, bodies, new_record, compiled_wifi_tui=None):
    """Install rescue SSH, Wi-Fi TUI, USB NIC loader and Android handoff helpers."""
    if compiled_wifi_tui is None:
        compiled_wifi_tui = build_wifi_tui_bytes()
    else:
        require(
            compiled_wifi_tui[:6] == b'\x7fELF\x02\x01'
            and int.from_bytes(compiled_wifi_tui[18:20], 'little') == 183,
            'Provided spx-wifi-tui is not an ARM64 ELF binary',
        )

    bodies['bin/spx-wifi-tui'] = compiled_wifi_tui
    records['bin/spx-wifi-tui'] = new_record(0o100755)

    for name in RESCUE_SCRIPTS:
        script = (RESCUE_DIR / name).read_bytes().replace(b'\r\n', b'\n')
        subprocess.run(['sh', '-n'], input=script, check=True, capture_output=True)
        rel = 'bin/' + name
        bodies[rel] = script
        records[rel] = new_record(0o100755)

    for rel, wrapper in RESCUE_ALIASES.items():
        subprocess.run(['sh', '-n'], input=wrapper, check=True, capture_output=True)
        bodies[rel] = wrapper
        records[rel] = new_record(0o100755)

    if 'init' in bodies:
        bodies['init'] = patch_init_for_rescue(bodies['init'])

    return {
        'rescue_ssh_default_password': '000000',
        'rescue_ssh_pubkey_enabled': True,
        'rescue_stops_before_android': True,
        'rescue_wifi_tui': 'bin/spx-wifi-tui',
        'rescue_continue_command': 'bin/start-android',
        'installed_scripts': sorted(['bin/' + n for n in RESCUE_SCRIPTS] + list(RESCUE_ALIASES)),
    }
