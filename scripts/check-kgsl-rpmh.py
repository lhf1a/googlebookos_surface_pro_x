#!/usr/bin/env python3
"""Check actual KGSL RPMh transforms with fault injection and a read-only target cmd-db dump."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("kgsl_check", ROOT / "scripts/check-kgsl-dmabuf.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def block(text, pattern):
    matches = re.findall(pattern, text, re.M | re.S)
    if len(matches) != 1:
        raise ValueError("Expected one block: " + pattern)
    return matches[0]


def macro(text, name):
    lines = text.splitlines()
    indices = [i for i, line in enumerate(lines) if re.match(r"^#define " + name + r"(?=[\s(])", line)]
    if len(indices) != 1:
        raise ValueError("Expected one macro: " + name)
    i = indices[0]
    result = [lines[i]]
    while result[-1].endswith(chr(92)):
        i += 1
        result.append(lines[i])
    return "\n".join(result)


def check(source, kernel, dump, output):
    output.mkdir(parents=True, exist_ok=False)
    rpmh = (source / "adreno_a6xx_rpmh.c").read_text()
    hfi = (source / "adreno_a6xx_hfi.h").read_text()
    core = (source / "kgsl_gmu_core.h").read_text()
    tcs = (kernel / "include/soc/qcom/tcs.h").read_text()
    types = [block(rpmh, r"^(struct " + name + r" \{.*?^\};)") for name in
             ("rpmh_arc_vals", "bcm", "bcm_data", "rpmh_bw_votes")]
    types += [block(hfi, r"^(struct hfi_bwtable_cmd \{.*?^\} __packed;)")]
    for name in ("MAX_GX_LEVELS", "MAX_CNOC_LEVELS", "MAX_CNOC_CMDS", "MAX_BW_CMDS"):
        types.insert(0, macro(core, name))
    for text, names in ((rpmh, ("ARC_VOTE_SET",)),
                        (tcs, ("BCM_TCS_CMD_COMMIT_MASK", "BCM_TCS_CMD_VALID_MASK",
                               "BCM_TCS_CMD_VOTE_MASK", "BCM_TCS_CMD_VOTE_X_MASK", "BCM_TCS_CMD_VOTE_Y_MASK", "BCM_TCS_CMD")),
                        (hfi, ("HFI_MSG_CMD", "CREATE_MSG_HDR", "H2F_MSG_BW_VOTE_TBL",
                               "HFI_MAX_MSG_SIZE", "CMD_MSG_HDR"))):
        types.extend(macro(text, name) for name in names)
    types += [block(rpmh, r"^(static const struct bcm " + name + r"\[\] = \{.*?^\};)")
              for name in ("sc8180x_ddr_bcms", "sc8180x_cnoc_bcms")]
    (output / "rpmh-types.h").write_text("\n\n".join(types) + "\n")
    functions = [module.extract(rpmh, name) for name in
                 ("rpmh_arc_cmds", "setup_volt_dependency_tbl", "tcs_cmd_data",
                  "free_rpmh_bw_votes", "build_rpmh_bw_votes", "build_bw_table_cmd", "build_bw_table")]
    (output / "rpmh-functions.h").write_text("\n\n".join(functions) + "\n")
    entries = {}
    for address, name, data in re.findall(r"^0x([0-9a-f]+): (\S+) \[([0-9a-f ]+)\]$", dump.read_text(), re.M):
        entries[name] = (int(address, 16), bytes.fromhex(data))
    names = ("gfx.lvl", "cx.lvl", "mx.lvl", "MC0", "SH0", "ACV", "SH2", "SN15", "SN3", "CN0")
    fixture = ["static const struct db_entry target_entries[] = {"]
    for name in names:
        address, data = entries[name]
        if len(data) > 64:
            raise ValueError("oversized fixture")
        fixture.append('{"%s", 0x%x, %d, {%s}},' % (name, address, len(data),
                       ",".join(str(b) for b in data)))
    fixture.append("};")
    (output / "rpmh-target.h").write_text("\n".join(fixture) + "\n")
    (output / "ddr-functions.h").write_text("\n\n".join([
        module.extract((source / "kgsl.c").read_text(), "kgsl_get_ddrtype"),
        module.extract((source / "kgsl.c").read_text(), "kgsl_of_property_read_ddrtype"),
        module.extract((source / "kgsl_bus.c").read_text(), "kgsl_bus_get_table")]) + "\n")
    harness = ROOT / "tests/kgsl_rpmh_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined",
               "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    ddr_harness = ROOT / "tests/kgsl_rpmh_ddr_test.c"
    ddr_command = [str(ddr_harness) if part == str(harness) else
                   str(output / "check-ddr") if part == str(output / "check") else part for part in command]
    subprocess.run(ddr_command, check=True)
    ddr_result = subprocess.run([str(output / "check-ddr")], capture_output=True, text=True)
    (output / "ddr.log").write_text(ddr_result.stdout + ddr_result.stderr)
    files = [source / "adreno_a6xx_rpmh.c", source / "adreno_a6xx_hfi.h", source / "kgsl_gmu_core.h",
             kernel / "include/soc/qcom/tcs.h", dump, harness, ddr_harness, source / "kgsl.c", source / "kgsl_bus.c"]
    report = dict(status="host-tested-target-metadata-not-hardware-voting" if not (result.returncode or ddr_result.returncode) else "failed",
                  sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}, command=command,
                  extracted_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.h")},
                  exit=result.returncode, ddr_exit=ddr_result.returncode, ddr_command=ddr_command,
                  result=result.stdout + result.stderr + ddr_result.stdout + ddr_result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    ddr_result.check_returncode()
    print(report["result"], end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--cmd-db-dump", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.kernel_source.resolve(), args.cmd_db_dump.resolve(), args.output.absolute())
