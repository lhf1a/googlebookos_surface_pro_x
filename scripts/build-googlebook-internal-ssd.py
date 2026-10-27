#!/usr/bin/env python3
"""512 GB replacement NVMe profile; build only regular image files."""
import importlib.util
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder', root/'scripts/build-googlebook-ssd.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)
b.O = root/'out/internal-ssd'
b.C = pathlib.Path('/root/.cache/sq2-probe-build/internal-ssd')
b.SIZE = 512110190592
b.ID = 'SQ2-INTERNAL-512-20261027'
b.DATA_UUID = '4f46d2ec-323e-486e-b473-fc2adc0849ba'
b.ESP_UUID = 'B027-0512'
b.BOOT = b.O
b.GRUB = root/'configs/grub-internal-ssd.cfg'
b.IMAGE_NAME = 'googlebook-sq2-internal-512.img'
if '--prepare' in sys.argv:
    b.prepare()
else:
    b.build()
