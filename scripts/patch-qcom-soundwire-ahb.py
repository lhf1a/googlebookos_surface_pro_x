#!/usr/bin/env python3
"""Serialize WCD AHB transactions and initialize immutable FIFO capacity before IRQs."""
import argparse
import hashlib
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
BEFORE = "8842b2cdedae205eb676d4f8554a1112758a803423e6a20004d9d91333cb26fa"
AFTER = "5f3dd15adeca17920bccf9f91a87c41afc9bfdc9f0a98092f0f6a68af9de5f87"
PATCH = "701ec221f2bc290d6bf81f2d62cbcb1a46b3c3e7b523c33f7ffa8b6a518754a3"
def apply(kernel):
    source=kernel/"drivers/soundwire/qcom.c"
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    if digest==AFTER:
        return
    if digest!=BEFORE:
        raise ValueError("Unrecognized SoundWire source; review the patch against this version")
    patch=ROOT/"patches/qcom-soundwire-ahb-capacity.patch"
    if hashlib.sha256(patch.read_bytes()).hexdigest()!=PATCH:
        raise ValueError("SoundWire patch hash mismatch")
    subprocess.run(["patch","--batch","--fuzz=0","-p1","-i",str(patch)],cwd=kernel,check=True)
    if hashlib.sha256(source.read_bytes()).hexdigest()!=AFTER:
        raise ValueError("Unexpected patched SoundWire source")
if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel",type=Path)
    apply(parser.parse_args().kernel)
