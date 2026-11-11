/* Host-only model/fixture; see check-sde-native-README.md. */

 map_error=-EIO;ret=msm_smmu_map_dma_buf(mmu,&tab,DMA_TO_DEVICE,0);CHECK("DMA errno preserved",ret==-EIO);map_error=0;
 maps=unmaps=0;tab.nents=3;map_nents=1;
 ret=msm_smmu_map_dma_buf(mmu,&tab,DMA_TO_DEVICE,0);CHECK("local map success",ret==0&&maps==1);
 CHECK("mapped nents published",tab.nents==1);
 msm_smmu_unmap_dma_buf(mmu,&tab,DMA_TO_DEVICE,0);CHECK("orig_nents unmap",unmaps==1&&last_unmap_nents==3);
 maps=unmaps=0;tab.nents=1;ret=msm_smmu_map_dma_buf(mmu,&tab,DMA_TO_DEVICE,MSM_BO_EXTBUF);CHECK("import no double map",ret==0&&maps==0);
 msm_smmu_unmap_dma_buf(mmu,&tab,DMA_TO_DEVICE,MSM_BO_EXTBUF);CHECK("attachment owns unmap",unmaps==0);
 ret=msm_smmu_map_dma_buf(mmu,&tab,DMA_TO_DEVICE,MSM_BO_KEEPATTRS);CHECK("unsupported attrs rejected",ret==-EOPNOTSUPP);
 if(!ret)msm_smmu_unmap_dma_buf(mmu,&tab,DMA_TO_DEVICE,MSM_BO_KEEPATTRS);
