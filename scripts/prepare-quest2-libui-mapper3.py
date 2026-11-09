#!/usr/bin/env python3
"""Enable the included legacy fallback in one verified Android 17 libui input.

An explicit private candidate, never a device API override or automatic deployment.
Gralloc5 and Gralloc4 retain priority. Only their failure may reach Gralloc3/2.
"""
import argparse
import hashlib
import json
from pathlib import Path

INPUT_SHA256 = "c6210d688bc2ac75f01ed3f34d4e32dd1fb840082406af6474ffa000a6bd8a07"
OFFSET = 0x598D4
CONTEXT = bytes.fromhex("b92b00941f8c0071ec040054")
NOP = bytes.fromhex("1f2003d5")

def patch(data):
    if hashlib.sha256(data).hexdigest() != INPUT_SHA256:
        raise ValueError("Unsupported libui input; inspect and pin its actual selector first")
    return patch_selector(data)

def patch_selector(data):
    """Apply the bounded branch edit after provenance verification."""
    if data[OFFSET-8:OFFSET+4] != CONTEXT:
        raise ValueError("libui API-level fallback branch differs")
    result = bytearray(data)
    result[OFFSET:OFFSET+4] = NOP
    return bytes(result)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--libui",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    data=args.libui.read_bytes()
    result=patch(data)
    args.output.mkdir(parents=True,exist_ok=False)
    (args.output/"libui.so").write_bytes(result)
    (args.output/"BUILD.json").write_text(json.dumps({
        "input_sha256":hashlib.sha256(data).hexdigest(),
        "output_sha256":hashlib.sha256(result).hexdigest(),
        "patch_offset":OFFSET,"original":CONTEXT[-4:].hex(),"replacement":NOP.hex(),
        "status":"private-candidate-not-system-deployed",
        "limits":["Only restores included Gralloc3/2 fallback; no mapper4/5 metadata",
                  "No changes to device API, allocator implementations or buffer contents"]
    },indent=2)+"\n")

if __name__=="__main__":
    main()
