#!/usr/bin/env python3
'Run extracted native SDE DMA provider ownership/error tests.'
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
    s=common.read_input(args, msm_root/'msm_smmu.c')
    block=common.extract
    native='msm_smmu_check_domain' in s
    prefix=common.fixture(args, 'check-sde-native-dma-01.h')
    if native: body=re.sub(r'^#include.*$','',s,flags=re.M)
    else:
        body='\n'.join([block(s,'msm_smmu_client','struct'),block(s,'msm_smmu','struct'),'#define to_msm_smmu(x) container_of(x, struct msm_smmu, base)\n#define msm_smmu_to_client(s) ((s)->client)',*(block(s,n) for n in ['msm_smmu_map_dma_buf','msm_smmu_unmap_dma_buf','msm_smmu_destroy'])])
    prefix=common.source_constants(args, prefix)
    test=common.fixture(args, 'check-sde-native-dma-02.inc.c')
    if native:
     test+=common.fixture(args, 'check-sde-native-dma-05.inc.c')
    else:
     test+='struct msm_smmu_client client={.dev=&dev,.domain=&dom}; smmu=calloc(1,sizeof(*smmu));smmu->client=&client;smmu->client_dev=&dev;mmu=&smmu->base;dev.refs=1;\n'
    test+=common.fixture(args, 'check-sde-native-dma-03.inc.c')
    if native:
     test+=common.fixture(args, 'check-sde-native-dma-06.inc.c')
    test+=common.fixture(args, 'check-sde-native-dma-04.inc.c')
    functions=['msm_smmu_map_dma_buf', 'msm_smmu_unmap_dma_buf', 'msm_smmu_destroy']
    if native:
        functions+=['msm_smmu_check_domain', 'msm_smmu_new', 'msm_smmu_attach', 'msm_smmu_detach', 'msm_smmu_map', 'msm_smmu_one_to_one_map', 'msm_smmu_dma_contiguous']
    return common.run_harness(args, prefix+body+test, ('unused-parameter', 'unused-function', 'unused-variable', 'unused-but-set-variable'), functions=functions, native_only=21 if native else 0)

if __name__ == "__main__":
    raise SystemExit(common.entry(main))
