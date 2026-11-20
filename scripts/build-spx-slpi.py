#!/usr/bin/env python3
"""Build a dedicated SLPI EFI-attach driver and upstream FastRPC; no live DSP replacement."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def source_for_slpi(source):
    if "SQ2_EL2_PREBOOT_HANDOFF" not in source:
        raise ValueError("matching EFI attach kernel source required")
    original = '''!(of_device_is_compatible(np, "qcom,sc8180x-adsp-pas") ||
	      of_device_is_compatible(np, "qcom,sc8180x-cdsp-pas") ||
	      of_device_is_compatible(np, "qcom,sc8180x-mpss-pas"))'''
    if source.count(original) != 1:
        raise ValueError("unexpected preboot compatibility check")
    source = source.replace(original, '!of_device_is_compatible(np, "qcom,sc8180x-slpi-pas")')
    source, count = re.subn(
        r"static const struct of_device_id qcom_pas_of_match\[\] = \{.*?\n\};",
        '''static const struct of_device_id qcom_pas_of_match[] = {
	{ .compatible = "qcom,sc8180x-slpi-pas", .data = &sdm845_slpi_resource_init },
	{ },
};''', source, flags=re.S)
    if count != 1 or source.count('.name = "qcom_q6v5_pas"') != 1:
        raise ValueError("unexpected PAS match table or driver name")
    return source.replace('.name = "qcom_q6v5_pas"', '.name = "spx_slpi_pas"')


def source_for_fastrpc(source):
    freezable = "wait_for_completion_state(&ctx->work,\n\t\t\t\t\t\tTASK_INTERRUPTIBLE | TASK_FREEZABLE)"
    if freezable in source or "TASK_INTERRUPTIBLE | TASK_FREEZABLE" in source:
        return source
    original = "\t\terr = wait_for_completion_interruptible(&ctx->work);\n"
    if source.count(original) != 1:
        raise ValueError("unexpected fastrpc invoke wait sequence")
    replacement = (
        "\t\t/* Freeze FastRPC reverse-RPC listeners in place during s2idle instead of\n"
        "\t\t * returning -ERESTARTSYS and replaying a duplicate invoke on thaw.\n"
        "\t\t */\n"
        "\t\terr = wait_for_completion_state(&ctx->work,\n"
        "\t\t\t\t\t\tTASK_INTERRUPTIBLE | TASK_FREEZABLE);\n"
    )
    return source.replace(original, replacement, 1)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("build", type=Path)
    p.add_argument("output", type=Path)
    a = p.parse_args()
    source, build, out = (x.resolve() for x in (a.source, a.build, a.output))
    if not (build/".config").is_file() or not (build/"Module.symvers").stat().st_size:
        p.error("complete matching kernel build required")
    code = source_for_slpi((source/"drivers/remoteproc/qcom_q6v5_pas.c").read_text())
    fastrpc_code = source_for_fastrpc((source/"drivers/misc/fastrpc.c").read_text())
    out.mkdir(parents=True, exist_ok=False)
    (out/"spx-slpi-pas.c").write_text(code)
    (out/"fastrpc.c").write_text(fastrpc_code)
    (out/"Makefile").write_text(
        "obj-m += spx-slpi-pas.o fastrpc.o\n"
        f"ccflags-y += -I{source}/drivers/remoteproc\n")
    subprocess.run(["make", "-C", str(source), "O="+str(build), "ARCH=arm64",
                    "CROSS_COMPILE="+os.environ.get("CROSS_COMPILE", "aarch64-linux-gnu-"), "M="+str(out), "modules"], check=True)
