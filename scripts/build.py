#!/usr/bin/env python3
"""Compatibility filename for the single Linux 6.18 LTS build entry.

Subcommands: verify / package / compile / import-private / full-image /
single-user / disk-image. The full-image flow builds the universal whole disk;
--graphics mesa selects the full Mesa userspace chain and is documented in
docs/FULL-MESA-IMAGE-20261112.md. Historical obsolete disk/model/kernel options
remain rejected; they must not rebuild an obsolete kernel runtime.
"""
from pathlib import Path
import runpy

if __name__ == '__main__':
    runpy.run_path(str(Path(__file__).with_name('build-lts.py')), run_name='__main__')
