#!/usr/bin/env python3
"""Read-only, fail-closed KGSL DT preflight. Passing is not hardware acceptance.

Compare raw FDT properties, not a dtc text round trip (which can change escaped
byte strings). The explicit indexed policy must cover every original enabled OPP.
This tool never writes a DTB, disables an OPP, or infers a missing policy row.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

GPU = "/soc@0/gpu@2c00000"


def read_fdt(data):
    if len(data) < 40:
        raise ValueError("truncated FDT header")
    magic, size, so, st, ro, ver, compat, boot, ss, sz = struct.unpack_from(
        ">10I", data)
    if magic != 0xD00DFEED or not 17 <= ver or compat > 17:
        raise ValueError("unsupported FDT header")
    if not 40 <= size <= len(data) or so < 40 or st < 40 or ro < 40:
        raise ValueError("invalid FDT bounds")
    if so + sz > size or st + ss > size or ro + 16 > size:
        raise ValueError("FDT block exceeds totalsize")
    if not (so + sz <= st or st + ss <= so):
        raise ValueError("overlapping FDT blocks")
    reserve, pos = [], ro
    while pos + 16 <= size:
        entry = struct.unpack_from(">QQ", data, pos)
        pos += 16
        if entry == (0, 0):
            break
        reserve.append(entry)
    else:
        raise ValueError("unterminated reservation map")
    if not (pos <= so or ro >= so + sz) or not (pos <= st or ro >= st + ss):
        raise ValueError("overlapping reservation map")
    strings, block = data[st:st + ss], data[so:so + sz]
    nodes, stack, pos, ended = {}, [], 0, False
    while pos + 4 <= len(block):
        token = struct.unpack_from(">I", block, pos)[0]
        pos += 4
        if token == 1:
            end = block.find(b"\0", pos)
            if end < 0:
                raise ValueError("unterminated node name")
            name = block[pos:end].decode("ascii")
            if "/" in name or (not stack and name):
                raise ValueError("invalid node name")
            path = (stack[-1].rstrip("/") + "/" + name) if stack else "/"
            if path in nodes:
                raise ValueError("duplicate node")
            nodes[path] = {}
            stack.append(path)
            pos = (end + 4) & ~3
        elif token == 2:
            if not stack:
                raise ValueError("unbalanced END_NODE")
            stack.pop()
        elif token == 3:
            if not stack or pos + 8 > len(block):
                raise ValueError("invalid property header")
            length, offset = struct.unpack_from(">II", block, pos)
            pos += 8
            end = strings.find(b"\0", offset)
            if end < 0 or pos + length > len(block):
                raise ValueError("invalid property bounds")
            name = strings[offset:end].decode("ascii")
            if not name or name in nodes[stack[-1]]:
                raise ValueError("duplicate/empty property")
            nodes[stack[-1]][name] = block[pos:pos + length]
            pos = (pos + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            if stack or "/" not in nodes:
                raise ValueError("unbalanced FDT")
            ended = True
            break
        else:
            raise ValueError("unknown FDT token")
    if not ended:
        raise ValueError("missing FDT_END")
    return nodes, (reserve, boot)


def cells(value):
    if len(value) % 4:
        raise ValueError("non-cell-sized property")
    return list(struct.unpack(">" + "I" * (len(value) // 4), value))


def available(nodes, path):
    while True:
        if path not in nodes:
            return False
        if nodes[path].get("status", b"okay\0") not in (b"okay\0", b"ok\0"):
            return False
        if path == "/":
            return True
        path = path.rsplit("/", 1)[0] or "/"


def opps(nodes):
    handle = cells(nodes[GPU]["operating-points-v2"])
    if len(handle) != 1 or not handle[0]:
        raise ValueError("GPU needs one OPP-table phandle")
    tables = [p for p, v in nodes.items()
              if cells(v.get("phandle", b"")) == handle]
    if len(tables) != 1:
        raise ValueError("ambiguous/missing OPP-table phandle")
    table = tables[0]
    result = {}
    for path, props in nodes.items():
        if path.rsplit("/", 1)[0] != table or not available(nodes, path):
            continue
        hz = cells(props["opp-hz"])
        if len(hz) != 2:
            raise ValueError("OPP Hz must be a 64-bit property")
        hz = (hz[0] << 32) | hz[1]
        if not hz or hz in result:
            raise ValueError("zero/duplicate OPP frequency")
        result[hz] = path
    if not result:
        raise ValueError("no enabled GPU OPPs")
    return result


def check(baseline, candidate, policy):
    before, br = read_fdt(baseline)
    after, ar = read_fdt(candidate)
    original, enabled = opps(before), opps(after)
    errors, changes = [], []
    if set(original) != set(enabled):
        errors.append("enabled OPP set changed: removed=%s added=%s" % (
            sorted(set(original) - set(enabled)), sorted(set(enabled) - set(original))))
    if br != ar:
        errors.append("reservation map or boot CPU changed")
    policy_path = GPU + "/qcom,gpu-pwrlevels"
    # Only the exact policy subtree and two GPU policy properties may differ.
    for path in sorted(set(before) | set(after)):
        is_policy = path == policy_path or path.startswith(policy_path + "/")
        if path not in before or path not in after:
            if not is_policy:
                changes.append(path + ":node")
            continue
        for name in sorted(set(before[path]) | set(after[path])):
            if before[path].get(name) == after[path].get(name):
                continue
            if is_policy or (path == GPU and name in (
                    "qcom,kgsl-bus-policy", "qcom,bus-table-ddr")):
                continue
            changes.append(path + ":" + name)
    if changes:
        errors.append("non-policy properties changed: " + ", ".join(changes))
    if after[GPU].get("qcom,kgsl-bus-policy") != b"indexed\0":
        errors.append("explicit indexed policy is required; peak-only loses min/default/max")
    table = policy["ddr_table_kBps"]
    if (not isinstance(table, list) or not 2 <= len(table) <= 16
            or any(type(v) is not int or not 0 <= v <= 0xFFFFFFFF for v in table)
            or table[0] != 0 or any(a >= b for a, b in zip(table, table[1:]))):
        raise ValueError("invalid policy DDR table")
    if cells(after[GPU].get("qcom,bus-table-ddr", b"")) != table:
        errors.append("DT DDR table does not match explicit policy")
    expected, off_seen = {}, False
    for row in policy["frequency_bindings"]:
        hz = row["gpu_hz"]
        indices = [row[n] for n in ("min_index", "default_index", "max_index")]
        if type(hz) is int and hz == 0:
            if off_seen or any(type(v) is not int or v != 0 for v in indices):
                raise ValueError("invalid/duplicate explicit OFF policy row")
            off_seen = True
            continue
        if (type(hz) is not int or hz <= 0 or hz in expected
                or any(type(v) is not int for v in indices)
                or not 0 < indices[0] <= indices[1] <= indices[2] < len(table)):
            raise ValueError("invalid/duplicate explicit policy row")
        expected[hz] = indices
    missing = sorted(set(original) - set(expected))
    if missing or set(expected) - set(original):
        errors.append("policy coverage incomplete: missing=%s extra=%s" % (
            missing, sorted(set(expected) - set(original))))
    actual = {}
    for path, props in after.items():
        if path.rsplit("/", 1)[0] != policy_path or not available(after, path):
            continue
        vals = [cells(props[n]) for n in (
            "qcom,gpu-freq", "qcom,bus-min", "qcom,bus-freq", "qcom,bus-max")]
        if any(len(v) != 1 for v in vals) or vals[0][0] in actual:
            raise ValueError("invalid/duplicate DT policy row")
        actual[vals[0][0]] = [v[0] for v in vals[1:]]
    if actual != {0: [0, 0, 0], **expected}:
        errors.append("DT policy rows differ from exact policy (including OFF)")
    if not available(after, GPU) or not available(after, policy_path):
        errors.append("GPU or policy subtree unavailable")
    if after[GPU].get("interconnect-names") != b"gfx-mem\0":
        errors.append("expected one gfx-mem interconnect")
    for path in enabled.values():
        if any(cells(after[path].get("opp-avg-kBps", b""))):
            errors.append(path + ": nonzero average unsupported")
    if actual and max(original) in actual:
        if any(v[2] > actual[max(original)][2] for v in actual.values()):
            errors.append("policy exceeds highest GPU OPP bus maximum")
    return dict(offline_contract_only=True, hardware_validated=False,
                baseline_sha256=hashlib.sha256(baseline).hexdigest(),
                candidate_sha256=hashlib.sha256(candidate).hexdigest(),
                baseline_enabled_hz=sorted(original), candidate_enabled_hz=sorted(enabled),
                missing_policy_hz=missing, unrelated_changes=changes,
                errors=errors, passed=not errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--policy", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = check(args.baseline.read_bytes(), args.candidate.read_bytes(),
                       json.loads(args.policy.read_text(encoding="utf-8")))
    except (ValueError, KeyError, TypeError, OSError, struct.error) as error:
        print(json.dumps(dict(passed=False, errors=[str(error)])))
        return 2
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
