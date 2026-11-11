/* Host-only model/fixture; see check-sde-native-README.md. */

 map_nents=2;sg[1].dma_address=0x9000;unmaps=0;ret=msm_smmu_map_dma_buf(mmu,&tab,DMA_TO_DEVICE,0);CHECK("noncontiguous local unwound",ret==-ERANGE&&unmaps==1);
 maps=unmaps=0;tab.nents=2;ret=msm_smmu_map_dma_buf(mmu,&tab,DMA_TO_DEVICE,MSM_BO_EXTBUF);CHECK("bad import retained by attachment",ret==-ERANGE&&maps==0&&unmaps==0);
 sg[1].dma_address=0x2000;tab.nents=1;map_nents=1;
 ret=msm_smmu_map_dma_buf(mmu,&tab,DMA_TO_DEVICE,0);CHECK("remap valid",ret==0);
 CHECK("duplicate rejected",msm_smmu_map_dma_buf(mmu,&tab,DMA_TO_DEVICE,0)==-EBUSY);
 device_puts=0;msm_smmu_destroy(mmu);CHECK("live owner not destroyed",device_puts==0&&dev.refs==1);
 msm_smmu_unmap_dma_buf(mmu,&tab,DMA_TO_DEVICE,0);
 alloc_fail=1;CHECK("allocation failure",msm_smmu_map_dma_buf(mmu,&tab,DMA_TO_DEVICE,0)==-ENOMEM);alloc_fail=0;
