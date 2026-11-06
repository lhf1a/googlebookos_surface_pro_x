#!/usr/bin/env python3
"""Compatibility filename for the single Linux 6.18 LTS build entry.

Use verify / package / compile subcommands. Historical disk/model/kernel options
are intentionally rejected; they must not rebuild an obsolete kernel runtime.
"""
from pathlib import Path
import runpy

if __name__ == '__main__':
    runpy.run_path(str(Path(__file__).with_name('build-lts.py')), run_name='__main__')
