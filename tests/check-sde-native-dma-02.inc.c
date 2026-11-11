/* Host-only model/fixture; see check-sde-native-README.md. */

static int pass,fail;
#define CHECK(label,x) do{if(x){pass++;printf("PASS %s\n",label);}else{fail++;printf("FAIL %s\n",label);}}while(0)
int main(void){
 struct device_node node={1,1};struct iommu_domain dom={2,(void*)1,1};uint64_t mask=~0ULL;
 struct device dev={.of_node=&node,.domain=&dom,.driver=(void*)1,.dma_mask=&mask};
 struct scatterlist sg[3]={{0x1000,0x1000},{0x2000,0x1000},{0x3000,0x1000}};
 struct sg_table tab={sg,1,3};struct msm_smmu *smmu;struct msm_mmu *mmu;int ret;
