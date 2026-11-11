#!/usr/bin/env python3
"""Run exact downstream debugfs initializers with actual 6.18 void helpers.

Filesystem creation/removal and enclosing driver types are host models. No
fake dentry-returning scalar compatibility wrapper is used. A separate flag
checks the inherited controller-success cleanup defect's eventual successor.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DP_FUNCTIONS = ("dp_debug_init_status", "dp_debug_init_sink_caps", "dp_debug_init_mst",
                "dp_debug_init_link", "dp_debug_init_hdcp", "dp_debug_init_sim",
                "dp_debug_init_dsc_fec", "dp_debug_init_tpg", "dp_debug_init_reg_dump",
                "dp_debug_init_feature_toggle", "dp_debug_init_configs", "dp_debug_init")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(text, name, required=None):
    pattern = (r"^(?:static )?(?:inline )?(?:struct \w+ \*|void|int)\s*\n?" +
               re.escape(name) + r"\([^;{]*\)\n\{.*?^\}")
    matches = re.findall(pattern, text, re.M | re.S)
    if required is not None:
        matches = [body for body in matches if required in body]
    if len(matches) != 1:
        raise ValueError((name, len(matches)))
    return matches[0]


def definition(text, name):
    matches = re.findall(r"^#define\s+" + re.escape(name) + r"\s+.*$", text, re.M)
    if len(matches) != 1:
        raise ValueError((name, len(matches)))
    return matches[0]


def check(source, kernel, output, require_controller_root=False):
    source, kernel, output = source.resolve(), kernel.resolve(), output.absolute()
    msm = source if (source / "dp/dp_debug.c").exists() else source / "techpack/display/msm"
    dp = msm / "dp/dp_debug.c"
    ctrl = msm / "dsi/dsi_ctrl.c"
    display = msm / "dsi/dsi_display.c"
    wb = msm / "sde/sde_encoder_phys_wb.c"
    file_c = kernel / "fs/debugfs/file.c"
    public = kernel / "include/linux/debugfs.h"
    inode = kernel / "fs/debugfs/inode.c"
    functions = [(dp, name, None) for name in DP_FUNCTIONS] + [
        (ctrl, "dsi_ctrl_debugfs_init", "debugfs_create_dir"),
        (display, "dsi_display_debugfs_init", "debugfs_create_dir"),
        (wb, "sde_encoder_phys_wb_init_debugfs", "debugfs_create_u32")]
    bodies = [extract(path.read_text(), name, required) for path, name, required in functions]
    helpers = [extract(file_c.read_text(), name) for name in
               ("debugfs_create_mode_unsafe", "debugfs_create_u32",
                "debugfs_create_bool", "debugfs_create_ulong")]
    # Preserve actual public declarations too; incompatible return-type models
    # would fail compilation instead of silently supplying an old fake API.
    declarations = []
    for name in ("debugfs_create_u32", "debugfs_create_bool", "debugfs_create_ulong"):
        matches = re.findall(r"^void " + name + r"\([^;]+;", public.read_text(), re.M)
        if len(matches) != 1:
            raise ValueError((name, len(matches)))
        declarations += matches
    output.mkdir(parents=True, exist_ok=False)
    (output / "actual-driver-functions.h").write_text("\n\n".join(bodies) + "\n")
    (output / "actual-debugfs-helpers.h").write_text("\n".join(declarations) + "\n\n" + "\n\n".join(helpers) + "\n")
    fops = sorted(set(re.findall(r"&(\w+_fops)\b", "\n".join(bodies))))
    (output / "actual-fops-names.h").write_text("\n".join(f"static const struct file_operations {name} = {{ {i+100} }};" for i, name in enumerate(fops)) + "\n")
    defs = [definition(dp.read_text(), "DEBUG_NAME")]
    dp_h = msm / "dp/dp_debug.h"
    # A fragment needs the unchanged header supplied alongside its changed C.
    for name in ("DEFAULT_CONNECT_NOTIFICATION_DELAY_MS", "DEFAULT_DISCONNECT_DELAY_MS"):
        defs.append(definition(dp_h.read_text(), name))
    for name in ("SEC_PANEL_NAME_MAX_LEN",):
        defs.append(definition(display.read_text(), name))
    (output / "actual-definitions.h").write_text("\n".join(defs) + "\n")
    harness = ROOT / "tests/sde_debugfs_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
           "-Wno-unused-parameter", "-Wno-unused-function", "-fsanitize=address,undefined",
           "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    if require_controller_root:
        cmd.append("-DREQUIRE_CONTROLLER_ROOT=1")
    runs = []
    env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    for label, command in [("compile", cmd), ("run", [str(output / "check")])]:
        result = subprocess.run(command, capture_output=True, text=True, timeout=60, env=env)
        runs.append(dict(label=label, command=command, exit_status=result.returncode,
                         stdout=result.stdout, stderr=result.stderr))
        (output / (label + ".stdout.txt")).write_text(result.stdout)
        (output / (label + ".stderr.txt")).write_text(result.stderr)
        print(label, result.returncode, result.stdout.strip(), flush=True)
        if result.returncode:
            break
    ok = len(runs) == 2 and all(r["exit_status"] == 0 for r in runs)
    paths = {dp, ctrl, display, wb, dp_h, file_c, public, inode, harness, Path(__file__)}
    paths.update(output.glob("*.h"))
    report = dict(status="host-api-tested-not-device-validated" if ok else "failed", source=str(source),
                  kernel_source=str(kernel), functions=[name for _, name, _ in functions], runs=runs,
                  inputs={str(path): sha(path) for path in sorted(paths)}, all_pass=ok,
                  controller_cleanup_requirement=require_controller_root,
                  cleanup_limit="Default mode explicitly verifies that the API-only patch retains the known inherited controller-success removal defect; it is not a healthy cleanup claim.",
                  limits=["Actual void scalar declarations/helper bodies and fifteen downstream initializer bodies are extracted unchanged.",
                          "Dentry allocation/removal, locks, handlers and driver enclosing types remain host models; no real filesystem lifetime proof.",
                          "A successful initializer return means no observable fatal registration error, NOT that best-effort scalar nodes were created.",
                          "All real dentry API failure paths remain observable in the injected scenarios.",
                          "No complete SDE module, kernel concurrency, hardware or deployment validation."])
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--kernel-source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--require-controller-root", action="store_true")
    a = p.parse_args()
    raise SystemExit(check(a.source, a.kernel_source, a.output, a.require_controller_root))
