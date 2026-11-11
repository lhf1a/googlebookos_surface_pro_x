#!/usr/bin/env python3
"""Exercise GMU OPP, clock binding and HFI transfer with mocked provider calls."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("kgsl_check", ROOT / "scripts/check-kgsl-dmabuf.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


rpmh_spec = importlib.util.spec_from_file_location("rpmh_check", ROOT / "scripts/check-kgsl-rpmh.py")
rpmh_module = importlib.util.module_from_spec(rpmh_spec)
rpmh_spec.loader.exec_module(rpmh_module)


def check(source, dump, output):
    snippets, hashes = [], {}
    for filename, names in (
        ("adreno_a6xx_gmu.c", ("a6xx_gmu_clocks_probe", "a6xx_gmu_enable_clks")),
        ("adreno_a6xx_rpmh.c", ("rpmh_arc_cmds", "setup_volt_dependency_tbl", "setup_cx_arc_votes")),
        ("adreno_a6xx_hfi.c", ("a6xx_hfi_send_dcvstbl_v1",)),
    ):
        path = source / filename
        snippets.extend(module.extract(path.read_text(), name) for name in names)
        hashes[filename] = hashlib.sha256(path.read_bytes()).hexdigest()
    output.mkdir(parents=True, exist_ok=False)
    hfi = (source / "adreno_a6xx_hfi.h").read_text()
    core = (source / "kgsl_gmu_core.h").read_text()
    rpmh = (source / "adreno_a6xx_rpmh.c").read_text()
    types = [rpmh_module.macro(core, name) for name in ("MAX_GX_LEVELS", "MAX_CX_LEVELS")]
    types += [rpmh_module.block(hfi, r"^(struct " + name + r" \{.*?^\}(?: __packed)?;)") for name in
              ("opp_desc", "opp_gx_desc", "hfi_dcvstable_cmd", "hfi_dcvstable_v1_cmd")]
    types += [rpmh_module.block(rpmh, r"^(struct rpmh_arc_vals \{.*?^\};)"),
              rpmh_module.macro(rpmh, "ARC_VOTE_SET")]
    for name in ("HFI_MSG_CMD", "CREATE_MSG_HDR", "H2F_MSG_PERF_TBL", "HFI_MAX_MSG_SIZE", "CMD_MSG_HDR"):
        types.append(rpmh_module.macro(hfi, name))
    (output / "kgsl-gmu-opp-types.h").write_text("\n\n".join(types) + "\n")
    entries = {name: bytes.fromhex(data) for name, data in
               re.findall(r"^0x[0-9a-f]+: (\S+) \[([0-9a-f ]+)\]$", dump.read_text(), re.M)}
    fixture = []
    for name, symbol in (("cx.lvl", "target_cx"), ("mx.lvl", "target_mx")):
        data = entries[name]
        if not data or len(data) % 2 or len(data) > 512:
            raise ValueError("invalid ARC capture: " + name)
        fixture.append("static const unsigned char " + symbol + "[] = {" + ",".join(map(str, data)) + "};")
    (output / "kgsl-gmu-opp-target.h").write_text("\n".join(fixture) + "\n")
    for name in ("adreno_a6xx_hfi.h", "kgsl_gmu_core.h"):
        hashes[name] = hashlib.sha256((source / name).read_bytes()).hexdigest()
    extracted = output / "kgsl-gmu-opp-functions.h"
    extracted.write_text("\n\n".join(snippets) + "\n")
    harness = ROOT / "tests/kgsl_gmu_opp_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined",
               "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed",
                  source_sha256=hashes, command=command, exit=result.returncode,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  extractor_sha256=hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest(),
                  extracted_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.h")},
                  cmd_db_dump=str(dump), cmd_db_sha256=hashlib.sha256(dump.read_bytes()).hexdigest(),
                  checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  result=result.stdout + result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--cmd-db-dump", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.cmd_db_dump.resolve(), args.output.absolute())
