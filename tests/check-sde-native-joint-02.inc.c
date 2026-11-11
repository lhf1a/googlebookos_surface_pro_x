/* Host-only model/fixture; see check-sde-native-README.md. */

static int p,f;
#define CHECK(n,c) do{if(c){p++;printf("PASS %s\n",n);}else{f++;printf("FAIL %s\n",n);}}while(0)
int main(void){struct device_node node={1,1};struct iommu_domain dom={2,(void*)1,1};uint64_t mask=~0ULL;struct device dev={.of_node=&node,.domain=&dom,.driver=(void*)1,.dma_mask=&mask};
 struct msm_mmu*m=msm_smmu_new(&dev,MSM_SMMU_DOMAIN_UNSECURE);struct msm_gem_address_space a={m,true,{1}};struct msm_gem_vma v={0};struct scatterlist sg[3]={{0x1000,0x1000},{0x2000,0x1000},{0x3000,0x1000}};struct sg_table tab={sg,3,3};int ret;
 map_nents=2;maps=unmaps=0;ret=smmu_aspace_map_vma(&a,&v,&tab,2,0);CHECK("local real chain maps once",ret==0&&maps==1&&v.mapped&&v.iova==0x1000&&a.kref.refs==2&&tab.nents==2);
 ret=smmu_aspace_map_vma(&a,&v,&tab,2,0);CHECK("repeat VMA no duplicate DMA map",ret==0&&maps==1&&a.kref.refs==2);
 smmu_aspace_unmap_vma(&a,&v,&tab,0);CHECK("local real chain unmaps orig_nents",unmaps==1&&last_unmap_nents==3&&!v.mapped&&a.kref.refs==1);
 smmu_aspace_unmap_vma(&a,&v,&tab,0);CHECK("repeat VMA unmap no duplicate",unmaps==1&&a.kref.refs==1);
 map_nents=1;tab.nents=3;maps=unmaps=0;ret=smmu_aspace_map_vma(&a,&v,&tab,3,0);CHECK("local short span unwinds once unpublished",ret==-ERANGE&&maps==1&&unmaps==1&&!v.mapped&&a.kref.refs==1&&list_empty(&to_msm_smmu(m)->mappings));
 maps=unmaps=0;tab.nents=2;ret=smmu_aspace_map_vma(&a,&v,&tab,2,MSM_BO_EXTBUF);CHECK("import real chain no DMA remap",ret==0&&maps==0&&v.mapped&&a.kref.refs==2);
 smmu_aspace_unmap_vma(&a,&v,&tab,MSM_BO_EXTBUF);CHECK("import real chain no DMA unmap",unmaps==0&&!v.mapped&&a.kref.refs==1);
 tab.nents=1;ret=smmu_aspace_map_vma(&a,&v,&tab,2,MSM_BO_EXTBUF);CHECK("short import preserves attachment DMA owner",ret==-ERANGE&&maps==0&&unmaps==0&&!v.mapped&&a.kref.refs==1&&list_empty(&to_msm_smmu(m)->mappings));
 dma_unmap_sgtable(&dev,&tab,DMA_BIDIRECTIONAL,0);CHECK("attachment final unmap exactly once",unmaps==1);
 sg[0].dma_address=0;tab.nents=1;ret=smmu_aspace_map_vma(&a,&v,&tab,1,MSM_BO_EXTBUF);CHECK("DMA zero is mapped address",ret==0&&v.mapped&&v.iova==0&&a.kref.refs==2);smmu_aspace_unmap_vma(&a,&v,&tab,MSM_BO_EXTBUF);CHECK("DMA zero unmaps via mapped flag",!v.mapped&&a.kref.refs==1);
 sg[0].dma_address=0x1000;sg[1].dma_address=0x9000;tab.nents=2;ret=smmu_aspace_map_vma(&a,&v,&tab,2,MSM_BO_EXTBUF);CHECK("provider rejects noncontiguous before VMA",ret==-ERANGE&&!v.mapped&&a.kref.refs==1);
 map_error=-EIO;tab.nents=3;ret=smmu_aspace_map_vma(&a,&v,&tab,1,0);CHECK("actual provider errno reaches VMA",ret==-EIO&&!v.mapped&&a.kref.refs==1);map_error=0;
 msm_smmu_destroy(m);CHECK("joint teardown puts MDSS only",dev.refs==0&&unregisters==0);printf("RESULT %d green %d red\n",p,f);return f?1:0;}
