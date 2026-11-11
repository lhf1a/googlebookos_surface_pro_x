#!/usr/bin/env python3
'Run extracted SDE KMS native DMA publication/error tests.'
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
    def block(name):
        return common.extract(s, name)
    prefix=common.fixture(args, 'check-sde-native-kms-01.h')
    prefix=common.source_constants(args, prefix)
    body='\n'.join(block(x) for x in ['_sde_kms_mmu_destroy','_sde_kms_mmu_init','sde_kms_mmu_attach','sde_kms_mmu_detach'])
    test=common.fixture(args, 'check-sde-native-kms-02.inc.c')
    return common.run_harness(args, prefix+body+test, ('unused-parameter', 'unused-function', 'sign-compare'), functions=['_sde_kms_mmu_destroy', '_sde_kms_mmu_init', 'sde_kms_mmu_attach', 'sde_kms_mmu_detach'], native_only=0)

if __name__ == "__main__":
    raise SystemExit(common.entry(main))
