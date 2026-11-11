#!/usr/bin/env python3
'Run extracted SDE secure framebuffer admission rejection tests.'
from pathlib import Path
import importlib.util
import re
_spec=importlib.util.spec_from_file_location("sde_native_common",Path(__file__).with_name("check-sde-native-common.py"))
common=importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(common)

def main():
    args=common.arguments(__doc__, joint=False)
    msm_root=args.msm
    out=args.output
    s=common.read_input(args, msm_root/'sde/sde_kms.c')
    body=common.extract(s, 'sde_kms_check_secure_transition')
    prefix=common.fixture(args, 'check-sde-native-secure-01.h')
    prefix=common.source_constants(args, prefix)
    test=common.fixture(args, 'check-sde-native-secure-02.inc.c')
    return common.run_harness(args, prefix+body+test, ('unused-but-set-variable',), functions=['sde_kms_check_secure_transition'], native_only=0)

if __name__ == "__main__":
    raise SystemExit(common.entry(main))
