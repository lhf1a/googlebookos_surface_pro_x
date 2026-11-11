#!/usr/bin/env python3
"""Validate boot-flow graph and stop locally on unresolved host prerequisites.

This checker has no SSH, deployment or reboot path. It is a host construction
gate, not a bootloader and not evidence that a future flow boots on hardware.
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST_STATES = ("VERIFY_INPUTS", "VERIFY_DT", "BUILD_BOOT_PAYLOAD", "VERIFY_HOST_BOOT")
EXPECTED_ORDER = HOST_STATES + ("DEVICE_STAGE", "ARM_ONE_SHOT", "BOOT_ONCE", "BIND_PROVIDERS", "ANDROID_HANDOFF", "RECORD_RESULT")


def check(flow):
    states = flow["states"]
    names = [s["id"] for s in states]
    if tuple(names) != EXPECTED_ORDER:
        raise ValueError("boot state order changed or mandatory state missing")
    if flow["entry"] != names[0] or flow["failure"] != "STOP_BEFORE_DEVICE_WRITE":
        raise ValueError("invalid entry/failure boundary")
    for i, state in enumerate(states):
        if state["next"] != (names[i + 1] if i + 1 < len(names) else "DONE"):
            raise ValueError("invalid next state/cycle")
        if not state["requires"] or len(set(state["requires"])) != len(state["requires"]):
            raise ValueError("missing/duplicate gate")
    if flow["runtime_failure"][-1] != "stop_no_retry":
        raise ValueError("runtime failure may not auto retry")
    if not flow["runtime_binding_checks"]["modprobe_zero_is_not_binding_success"]:
        raise ValueError("module load cannot replace binding gate")
    blockers = flow["host_blockers"]
    gates = [g for s in states[:4] for g in s["requires"]]
    if set(blockers) - set(gates):
        raise ValueError("unknown host blocker")
    # This configuration remains blocked until the implementation and its real
    # evidence are supplied. Deleting blocker labels alone is never approval.
    return dict(graph_valid=True, states=names, unresolved=blockers,
                host_construction_complete=False, device_activation_allowed=False,
                status="STOP_BEFORE_DEVICE_WRITE")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--flow", type=Path, default=ROOT / "configs/kgsl-sde-boot-flow.json")
    args = p.parse_args()
    try:
        result = check(json.loads(args.flow.read_text()))
    except (ValueError, KeyError, TypeError, OSError) as error:
        print(json.dumps(dict(graph_valid=False, error=str(error))))
        return 2
    print(json.dumps(result, indent=2))
    return 78  # Known incomplete construction; do not deploy or reboot.


if __name__ == "__main__":
    raise SystemExit(main())
