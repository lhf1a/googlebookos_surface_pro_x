#!/usr/bin/env python3
"""Run production SDE link transactions with a modeled AUX transport.

The helper bodies, private state and core rate conversion/macros are extracted
from the supplied sources. AUX, sleeps and physical sink behavior are models;
this is not link training, eDP rate-table negotiation or hardware validation.
Optional pinned --reference executes the removed donor functions as a regression
baseline, never supplies old core headers to a kernel build.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("extractor", ROOT / "scripts/check-kgsl-dmabuf.py")
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_wiring(source):
    """Check real production callsites, not the extracted helper harness."""
    files = sorted(p for p in source.rglob("*") if p.suffix in (".c", ".h"))
    text = "\n".join(p.read_text() for p in files)
    ctrl = (source / "dp/dp_ctrl.c").read_text()
    link = (source / "dp/dp_link.c").read_text()
    panel = (source / "dp/dp_panel.c").read_text()
    checks = {
        "no_removed_core_link_type": not re.search(r"struct\s+drm_dp_link\b", text),
        "no_removed_core_transactions": not re.search(r"\bdrm_dp_link_(?:configure|power_up|power_down)\s*\(", text),
        "no_removed_core_capability": "DP_LINK_CAP_ENHANCED_FRAMING" not in text,
        "no_old_public_header_paths": not re.search(r"drm/(?:drmP|drm_dp_helper|drm_dp_mst_helper|drm_dsc)\.h", text),
        "three_delay_calls_use_aux": len(re.findall(r"drm_dp_link_train_(?:clock_recovery|channel_eq)_delay\(ctrl->aux->drm_aux, ctrl->panel->dpcd\)", ctrl)) == 3
            and len(re.findall(r"drm_dp_link_train_(?:clock_recovery|channel_eq)_delay\(", ctrl)) == 3,
        "configure_error_stops_training": bool(re.search(r"ret = sde_dp_link_configure\(ctrl->aux->drm_aux, &link_info\);\s*if \(ret\)\s*goto end;", extractor.extract(ctrl, "dp_ctrl_link_train"))),
        "power_wrapper_propagates_errno": bool(re.search(r"if \(enable\)\s*ret = sde_dp_link_power_down\(link->aux->drm_aux, link_info\);\s*else\s*ret = sde_dp_link_power_up\(link->aux->drm_aux, link_info\);", extractor.extract(link, "dp_link_psm_config")))
            and extractor.extract(link, "dp_link_psm_config").rstrip().endswith("return ret;\n}"),
        "panel_resets_capability_not_stale_or": bool(re.search(r"link_info->capabilities = drm_dp_enhanced_frame_cap\(dpcd\) \?\s*DP_LINK_INFO_ENHANCED_FRAMING : 0;", panel))
            and not re.search(r"link_info->capabilities\s*\|=", panel),
        "private_header_wired": '#include "dp_link_helpers.h"' in (source / "dp/dp_link.h").read_text(),
    }
    for name in ("Makefile", "Kbuild"):
        checks[name + "_links_helper_once"] = (source / name).read_text().count("dp/dp_link_helpers.o") == 1
        files.append(source / name)
    if not all(checks.values()):
        raise ValueError("production wiring failed: " + repr(checks))
    return checks, files


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", required=True, type=Path, help="prepared techpack/display/msm")
    p.add_argument("--kernel-source", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path, help="fresh evidence directory")
    p.add_argument("--reference", type=Path, help="fixed donor drm_dp_helper.c (expected to fail strict tests)")
    p.add_argument("--check-wiring", action="store_true", help="also check the complete candidate production tree")
    a = p.parse_args()
    a.output = a.output.absolute()
    a.output.mkdir(parents=True, exist_ok=False)
    c = a.source / "dp/dp_link_helpers.c"
    h = a.source / "dp/dp_link_helpers.h"
    core = a.kernel_source / "drivers/gpu/drm/display/drm_dp_helper.c"
    defs = a.kernel_source / "include/drm/display/drm_dp.h"
    files = [core, defs, Path(__file__), ROOT / "tests/sde_dp_link_test.c"]
    wiring = {}
    if a.check_wiring:
        wiring, wiring_files = check_wiring(a.source)
        files.extend(wiring_files)
    if a.reference:
        if sha(a.reference) != "7344d8f75f3c9aba10b985fa0c969facd17dd92805fb5961c4bc66b3b06a4dcf":
            raise ValueError("reference is not the pinned donor core")
        reference_header = a.reference.parents[3] / "include/drm/drm_dp_helper.h"
        if sha(reference_header) != "f739339626682a580b1e874695715c76bcf7e2534964ca4eee0bde01bb16f664":
            raise ValueError("reference type is not the pinned donor core")
        files.extend([a.reference, reference_header])
        types = re.search(r"struct drm_dp_link \{.*?\n};", reference_header.read_text(), re.S).group().replace("struct drm_dp_link", "struct dp_link_info")
        cap = re.search(r"^#define DP_LINK_CAP_ENHANCED_FRAMING .*$", reference_header.read_text(), re.M).group().replace("DP_LINK_CAP_ENHANCED_FRAMING", "DP_LINK_INFO_ENHANCED_FRAMING")
        functions = {}
        for n in ("configure", "power_up", "power_down"):
            body = extractor.extract(a.reference.read_text(), "drm_dp_link_" + n)
            body = body.replace("struct drm_dp_link", "const struct dp_link_info")
            body = body.replace("DP_LINK_CAP_ENHANCED_FRAMING", "DP_LINK_INFO_ENHANCED_FRAMING")
            functions[n] = body.replace("int drm_dp_link_" + n, "int sde_dp_link_" + n)
    else:
        files.extend([c, h])
        types = re.search(r"struct dp_link_info \{.*?\n};", h.read_text(), re.S).group()
        cap = re.search(r"^#define DP_LINK_INFO_ENHANCED_FRAMING .*$", h.read_text(), re.M).group()
        names = ["sde_dp_link_configure", "sde_dp_link_set_power", "sde_dp_link_power_up", "sde_dp_link_power_down"]
        functions = {n: extractor.extract(c.read_text(), n) for n in names}
    macros = []
    for n in ["DP_LINK_BW_SET", "DP_LANE_COUNT_ENHANCED_FRAME_EN", "DP_SET_POWER", "DP_SET_POWER_MASK", "DP_SET_POWER_D0", "DP_SET_POWER_D3", "DP_LINK_BW_10", "DP_LINK_BW_13_5", "DP_LINK_BW_20"]:
        macros.append(re.search(r"^#\s*define\s+" + n + r"\s+.*$", defs.read_text(), re.M).group())
    (a.output / "actual-definitions.h").write_text("\n".join(macros) + "\n" + cap + "\n" + types + "\n")
    converter = re.search(r"^u8 drm_dp_link_rate_to_bw_code\([^;{]*\)\n\{.*?^\}", core.read_text(), re.M | re.S).group()
    (a.output / "actual-functions.h").write_text(converter + "\n" + "\n".join(functions.values()) + "\n")
    runs = []
    commands = [
        ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(a.output), str(ROOT / "tests/sde_dp_link_test.c"), "-o", str(a.output / "check")],
        [str(a.output / "check")],
    ]
    for label, command in zip(["compile", "run"], commands):
        r = subprocess.run(command, capture_output=True, text=True, timeout=30,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
        runs.append(dict(label=label, command=command, stdout=r.stdout, stderr=r.stderr, exit_status=r.returncode))
        (a.output / (label + ".stdout.txt")).write_text(r.stdout)
        (a.output / (label + ".stderr.txt")).write_text(r.stderr)
        if r.returncode:
            break
    result = dict(status="pass" if runs[-1]["exit_status"] == 0 else "fail", reference=bool(a.reference),
                  inputs={str(f.resolve()): sha(f) for f in files}, functions=list(functions), runs=runs,
                  wiring_checks=wiring,
                  hardware_validated=False, full_module_validated=False)
    (a.output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
    print(runs[-1]["stdout"], end="")
    print("RESULT", a.output / "RESULT.json", result["status"])
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
