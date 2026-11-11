#!/usr/bin/env python3
"""Check target GPU OPP -> KGSL -> actual ARC/HFI conversion; hardware APIs mocked."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

extractor = load("extract", ROOT / "scripts/check-kgsl-dmabuf.py")
rpmh = load("rpmh", ROOT / "scripts/check-kgsl-rpmh.py")


def check(source, dtb, dump, output):
    output.mkdir(parents=True, exist_ok=False)
    functions, hashes = [], {}
    for name, names in (
        ("adreno.c", ("adreno_of_get_initial_pwrlevel", "adreno_of_get_native_pwrlevels", "adreno_of_get_pwrlevels")),
        ("adreno_a6xx_rpmh.c", ("rpmh_arc_cmds", "setup_volt_dependency_tbl", "setup_gx_arc_votes")),
        ("adreno_a6xx_hfi.c", ("a6xx_hfi_send_dcvstbl_v1",)),
        ("kgsl_bus.c", ("validate_pwrlevels",)),
    ):
        path = source / name
        functions.extend(extractor.extract(path.read_text(), n) for n in names)
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    core = (source / "kgsl_gmu_core.h").read_text()
    hfi = (source / "adreno_a6xx_hfi.h").read_text()
    power = (source / "kgsl_pwrctrl.h").read_text()
    arc = (source / "adreno_a6xx_rpmh.c").read_text()
    types = [rpmh.macro(core, n) for n in ("MAX_GX_LEVELS", "MAX_CX_LEVELS")]
    types += [rpmh.macro(power, "KGSL_MAX_PWRLEVELS"), rpmh.block(power, r"^(struct kgsl_pwrlevel \{.*?^\};)")]
    types += [rpmh.block(hfi, r"^(struct " + n + r" \{.*?^\}(?: __packed)?;)") for n in ("opp_desc", "opp_gx_desc", "hfi_dcvstable_cmd", "hfi_dcvstable_v1_cmd")]
    types += [rpmh.block(arc, r"^(struct rpmh_arc_vals \{.*?^\};)"), rpmh.macro(arc, "ARC_VOTE_SET")]
    types += [rpmh.macro(hfi, n) for n in ("HFI_MSG_CMD", "CREATE_MSG_HDR", "H2F_MSG_PERF_TBL", "HFI_MAX_MSG_SIZE", "CMD_MSG_HDR")]
    (output / "kgsl-gpu-opp-types.h").write_text("\n\n".join(types) + "\n")
    def prop(node, name):
        return subprocess.check_output(["fdtget", "-t", "x", str(dtb), node, name], text=True).split()
    root = "/soc/gpu@2c00000"
    assert prop(root, "operating-points-v2") == prop(root + "/opp-table", "phandle")
    nodes = subprocess.check_output(["fdtget", "-l", str(dtb), root + "/opp-table"], text=True).split()
    opps = []
    for node in nodes:
        path = root + "/opp-table/" + node
        hz = [int(x, 16) for x in prop(path, "opp-hz")]
        assert len(hz) == 2
        opps.append((hz[0] << 32 | hz[1], int(prop(path, "opp-level")[0], 16)))
    opps.sort()
    fixture = "static const struct dev_pm_opp target_opps[] = {" + ",".join("{%dUL,%d}" % pair for pair in opps) + "};\n"
    entries = {n: bytes.fromhex(d) for n, d in re.findall(r"^0x[0-9a-f]+: (\S+) \[([0-9a-f ]+)\]$", dump.read_text(), re.M)}
    for n, symbol in (("gfx.lvl", "target_gfx"), ("mx.lvl", "target_mx")):
        data = entries[n]
        assert data and len(data) % 2 == 0
        fixture += "static const unsigned char " + symbol + "[] = {" + ",".join(map(str, data)) + "};\n"
    (output / "kgsl-gpu-opp-target.h").write_text(fixture)
    header = output / "kgsl-gpu-opp-functions.h"
    header.write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_gpu_opp_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare",
               "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    hashes.update({n: hashlib.sha256((source / n).read_bytes()).hexdigest() for n in ("kgsl_pwrctrl.h", "kgsl_gmu_core.h", "adreno_a6xx_hfi.h")})
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed", source_sha256=hashes,
                  command=command, exit=result.returncode, target_opps=opps, dtb_sha256=hashlib.sha256(dtb.read_bytes()).hexdigest(),
                  cmd_db_sha256=hashlib.sha256(dump.read_bytes()).hexdigest(), harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  extracted_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.h")},
                  result=result.stdout + result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--dtb", type=Path, required=True)
    parser.add_argument("--cmd-db-dump", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.dtb.resolve(), args.cmd_db_dump.resolve(), args.output.absolute())
