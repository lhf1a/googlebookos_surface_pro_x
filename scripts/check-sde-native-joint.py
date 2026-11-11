#!/usr/bin/env python3
'Run actual native DMA provider and SDE VMA functions as one chain.'
from pathlib import Path
import importlib.util
import re
_spec=importlib.util.spec_from_file_location("sde_native_common",Path(__file__).with_name("check-sde-native-common.py"))
common=importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(common)

def main():
    args=common.arguments(__doc__, joint=True)
    msm_root=args.msm
    out=args.output
    vmap=args.vma_source
    prefix=common.fixture(args, 'check-sde-native-dma-01.h')
    body=re.sub(r'^#include.*$','',common.read_input(args, msm_root/'msm_smmu.c'),flags=re.M)
    s=common.read_input(args, vmap)
    def block(name):
        return common.extract(s, name)
    support=common.fixture(args, 'check-sde-native-joint-01.inc.c')
    prefix=common.source_constants(args, prefix)
    test=common.fixture(args, 'check-sde-native-joint-02.inc.c')
    return common.run_harness(args, prefix+body+support+block('smmu_aspace_unmap_vma')+block('smmu_aspace_map_vma')+test, ('unused-parameter', 'unused-function', 'unused-variable'), functions=['msm_smmu_new', 'msm_smmu_map_dma_buf', 'msm_smmu_unmap_dma_buf', 'msm_smmu_destroy', 'smmu_aspace_map_vma', 'smmu_aspace_unmap_vma'], native_only=0)

if __name__ == "__main__":
    raise SystemExit(common.entry(main))
