#!/usr/bin/env python3
"""Accept a ready reply on the last HFI poll; preserve the normal poll limit."""
import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OLD_HEADER_SHA = "7ca1c54ff807e2c431282a4b9a196ceed1574cdedb1da88f2288fde556d18719"
OLD = "\tif (count >= max_tries)\n\t\tret = -ETIMEDOUT;"
NEW = "\tif (!ctrl_status && count >= max_tries)\n\t\tret = -ETIMEDOUT;"


def apply(directory):
    path = directory / "hfi_venus.c"
    source = path.read_text()
    start = source.index("static int venus_boot_core(")
    end = source.index("static u32 venus_hwversion(", start)
    body = source[start:end]
    for anchor in (
        "max_tries = 100;",
        "while (!ctrl_status && count < max_tries)",
        "if ((ctrl_status & CPU_CS_SCIACMDARG0_ERROR_STATUS_MASK) == 4)",
    ):
        if body.count(anchor) != 1:
            raise ValueError("Unknown HFI boot loop: " + anchor)
    if body.count(OLD) == 1 and NEW not in body:
        body = body.replace(OLD, NEW)
    elif body.count(NEW) != 1 or OLD in body:
        raise ValueError("Unknown HFI timeout branch")
    path.write_text(source[:start] + body + source[end:])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    apply(parser.parse_args().directory)
