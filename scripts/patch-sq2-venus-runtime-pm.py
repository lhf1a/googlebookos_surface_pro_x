#!/usr/bin/env python3
"""Wire SQ2 Venus idle delayed runtime PM and lazy s2idle resume idempotently.

Only edits the supplied external/venus build copy. Validates every anchor
before writing so partial or ambiguous inputs fail loudly.
"""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[1]

SYNC_FILES = (
    'sq2-venus-system-pm.h',
    'venus-sq2-system-pm.h',
    'venus-sq2-core.h',
    'venus-sq2-avc-export.h',
    'sq2-venus-power.h',
    'sq2-venus-power.c',
    'sq2-venus-platform.h',
    'sq2-venus-platform.c',
)

CORE_H_OLD = (
    'struct venus_core {\n'
    ' struct sq2_system_pm sq2_system_pm;\n'
    ' bool sq2_no_tz_active;'
)
CORE_H_NEW = (
    'struct venus_core {\n'
    ' struct sq2_system_pm sq2_system_pm;\n'
    ' struct mutex sq2_pm_mutex;\n'
    ' bool sq2_no_tz_active;\n'
    ' bool sq2_runtime_suspended;\n'
    ' unsigned long sq2_rt_stops, sq2_rt_starts;'
)

CORE_C_SHUTDOWN_OLD = (
    '\tif (IS_SQ2_HFI(core)) {\n'
    '\t\tcancel_delayed_work_sync(&core->work);\n'
    '\t\tvenus_sq2_stop_core(core);\n'
    '\t\treturn;\n'
    '\t}'
)
CORE_C_SHUTDOWN_NEW = (
    '\tif (IS_SQ2_HFI(core)) {\n'
    '\t\tif (core->sq2_pm_enabled) {\n'
    '\t\t\tpm_runtime_disable(core->dev);\n'
    '\t\t\tcore->sq2_pm_enabled = false;\n'
    '\t\t}\n'
    '\t\tcancel_delayed_work_sync(&core->work);\n'
    '\t\tvenus_sq2_stop_core(core);\n'
    '\t\treturn;\n'
    '\t}'
)

CORE_C_RT_SUSPEND_OLD = (
    '\tif (IS_SQ2_HFI(core))\n'
    '\t\treturn -EBUSY;'
)
CORE_C_RT_SUSPEND_NEW = (
    '\tif (IS_SQ2_HFI(core)) {\n'
    '#ifdef SQ2_VENUS_SYSTEM_PM\n'
    '\t\treturn venus_sq2_runtime_suspend(dev);\n'
    '#else\n'
    '\t\treturn -EBUSY;\n'
    '#endif\n'
    '\t}'
)

CORE_C_RT_RESUME_OLD = (
    '\tif (IS_SQ2_HFI(core))\n'
    '\t\treturn test_bit(0, &core->sys_error) ? -EIO : 0;'
)
CORE_C_RT_RESUME_NEW = (
    '\tif (IS_SQ2_HFI(core)) {\n'
    '#ifdef SQ2_VENUS_SYSTEM_PM\n'
    '\t\treturn venus_sq2_runtime_resume(dev);\n'
    '#else\n'
    '\t\treturn test_bit(0, &core->sys_error) ? -EIO : 0;\n'
    '#endif\n'
    '\t}'
)

HFI_C_INC_ANCHOR = '#include <linux/mutex.h>\n'
HFI_C_INC_TARGET = '#include <linux/pm_runtime.h>\n'

HFI_C_CREATE_OLD = (
    '\tinst->ops = ops;\n'
    '\n'
    '\tmutex_lock(&core->lock);\n'
)
HFI_C_CREATE_NEW = (
    '\tinst->ops = ops;\n'
    '\n'
    '#ifdef SQ2_VENUS_SYSTEM_PM\n'
    '\tif (IS_SQ2_HFI(core)) {\n'
    '\t\tret = pm_runtime_resume_and_get(core->dev);\n'
    '\t\tif (ret < 0)\n'
    '\t\t\treturn ret;\n'
    '\t}\n'
    '#endif\n'
    '\tmutex_lock(&core->lock);\n'
)

HFI_C_CREATE_UNLOCK_OLD = (
    'unlock:\n'
    '\tmutex_unlock(&core->lock);\n'
    '\n'
    '\treturn ret;\n'
    '}\n'
    'EXPORT_SYMBOL_GPL(hfi_session_create);'
)
HFI_C_CREATE_UNLOCK_NEW = (
    'unlock:\n'
    '\tmutex_unlock(&core->lock);\n'
    '#ifdef SQ2_VENUS_SYSTEM_PM\n'
    '\tif (ret && IS_SQ2_HFI(core)) {\n'
    '\t\tpm_runtime_mark_last_busy(core->dev);\n'
    '\t\tpm_runtime_put_autosuspend(core->dev);\n'
    '\t}\n'
    '#endif\n'
    '\n'
    '\treturn ret;\n'
    '}\n'
    'EXPORT_SYMBOL_GPL(hfi_session_create);'
)

HFI_C_DESTROY_OLD = (
    '\tsq2_callback_drain(inst);\n'
    '\tif (IS_SQ2_HFI(core)) sq2_venus_scope_drop(inst);\n'
    '}\n'
    'EXPORT_SYMBOL_GPL(hfi_session_destroy);'
)
HFI_C_DESTROY_NEW = (
    '\tsq2_callback_drain(inst);\n'
    '\tif (IS_SQ2_HFI(core)) sq2_venus_scope_drop(inst);\n'
    '#ifdef SQ2_VENUS_SYSTEM_PM\n'
    '\tif (IS_SQ2_HFI(core)) {\n'
    '\t\tpm_runtime_mark_last_busy(core->dev);\n'
    '\t\tpm_runtime_put_autosuspend(core->dev);\n'
    '\t}\n'
    '#endif\n'
    '}\n'
    'EXPORT_SYMBOL_GPL(hfi_session_destroy);'
)


def _patch_idempotent(text: str, old: str, new: str, label: str):
    if new in text:
        return text, False
    count = text.count(old)
    if count != 1:
        raise ValueError(f'Unexpected Venus runtime-PM anchor ({count}) for {label}')
    return text.replace(old, new, 1), True


def apply(directory, root=ROOT):
    directory = Path(directory)
    root = Path(root)
    video_dir = root / 'probe/video'
    pending = {}

    # Prepare synchronized repo files first (validate before writing anything).
    for name in SYNC_FILES:
        src = video_dir / name
        dst = directory / name
        if not src.is_file():
            raise ValueError(f'Missing repo Venus source: {src}')
        if not dst.is_file():
            raise ValueError(f'Missing target Venus file: {dst}')
        want = src.read_text(encoding='utf-8')
        if name == 'venus-sq2-core.h':
            publish_raw = ' if (ret) goto fail;\n return 0;\nfail:'
            publish_done = ' if (ret) goto fail;\n venus_sq2_pm_publish(core);\n return 0;\nfail:'
            if publish_done not in want:
                if want.count(publish_raw) != 1:
                    raise ValueError('Unexpected venus_sq2_probe publish anchor in probe/video/venus-sq2-core.h')
                want = want.replace(publish_raw, publish_done, 1)
        if dst.read_text(encoding='utf-8') != want:
            pending[name] = want

    # Validate & transform core.h, core.c, hfi.c
    core_h = (directory / 'core.h').read_text(encoding='utf-8')
    core_h_new, ch = _patch_idempotent(core_h, CORE_H_OLD, CORE_H_NEW, 'core.h:sq2_pm_mutex')
    if ch:
        pending['core.h'] = core_h_new

    core_c = (directory / 'core.c').read_text(encoding='utf-8')
    c_mod = False
    for old, new, lbl in (
        (CORE_C_SHUTDOWN_OLD, CORE_C_SHUTDOWN_NEW, 'core.c:shutdown'),
        (CORE_C_RT_SUSPEND_OLD, CORE_C_RT_SUSPEND_NEW, 'core.c:runtime_suspend'),
        (CORE_C_RT_RESUME_OLD, CORE_C_RT_RESUME_NEW, 'core.c:runtime_resume'),
    ):
        core_c, changed = _patch_idempotent(core_c, old, new, lbl)
        c_mod = c_mod or changed
    if c_mod:
        pending['core.c'] = core_c

    hfi_c = (directory / 'hfi.c').read_text(encoding='utf-8')
    h_mod = False
    if HFI_C_INC_TARGET not in hfi_c:
        hfi_c, changed = _patch_idempotent(
            hfi_c, HFI_C_INC_ANCHOR, HFI_C_INC_ANCHOR + HFI_C_INC_TARGET, 'hfi.c:pm_runtime_inc'
        )
        h_mod = h_mod or changed
    for old, new, lbl in (
        (HFI_C_CREATE_OLD, HFI_C_CREATE_NEW, 'hfi.c:session_create'),
        (HFI_C_CREATE_UNLOCK_OLD, HFI_C_CREATE_UNLOCK_NEW, 'hfi.c:session_create_unlock'),
        (HFI_C_DESTROY_OLD, HFI_C_DESTROY_NEW, 'hfi.c:session_destroy'),
    ):
        hfi_c, changed = _patch_idempotent(hfi_c, old, new, lbl)
        h_mod = h_mod or changed
    if h_mod:
        pending['hfi.c'] = hfi_c

    for name, content in pending.items():
        (directory / name).write_text(content, encoding='utf-8')
    return sorted(pending.keys())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    print(apply(args.directory, args.root))
