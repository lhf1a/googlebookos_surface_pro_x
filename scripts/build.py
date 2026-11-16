#!/usr/bin/env python3
"""Compatibility filename for the single Linux 6.18 LTS build entry.

Subcommands: verify / package / compile / import-private / full-image /
single-user / disk-image. The full-image flow builds the universal hybrid whole disk with initial setup skipped and the current
touch calibration wizard. Hybrid is the only graphics selection. See
docs/STANDARD-BUILD.md. Historical obsolete disk/model/kernel options
remain rejected; they must not rebuild an obsolete kernel runtime.
"""
from pathlib import Path
import runpy

if __name__ == '__main__':
    runpy.run_path(str(Path(__file__).with_name('build-lts.py')), run_name='__main__')
