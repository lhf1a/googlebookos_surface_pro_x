/* Host-only model/fixture; see check-sde-native-README.md. */

 mmu=msm_smmu_new(&dev,MSM_SMMU_DOMAIN_UNSECURE);CHECK("native MDSS creation",!IS_ERR(mmu));smmu=to_msm_smmu(mmu);
 CHECK("device ref held",dev.refs==1);
 struct device child={.parent=&dev};struct msm_mmu *c=msm_smmu_new(&child,MSM_SMMU_DOMAIN_UNSECURE);CHECK("DPU selects MDSS",!IS_ERR(c)&&c->dev==&dev);c->funcs->destroy(c);
 CHECK("secure denied",PTR_ERR(msm_smmu_new(&dev,MSM_SMMU_DOMAIN_SECURE))==-EOPNOTSUPP);
 CHECK("NRT denied",PTR_ERR(msm_smmu_new(&dev,MSM_SMMU_DOMAIN_NRT_UNSECURE))==-EOPNOTSUPP);
 node.iommus=0;CHECK("missing iommus denied",PTR_ERR(msm_smmu_new(&dev,MSM_SMMU_DOMAIN_UNSECURE))==-ENODEV);node.iommus=1;
 dom.type=1;CHECK("paging owner denied",PTR_ERR(msm_smmu_new(&dev,MSM_SMMU_DOMAIN_UNSECURE))==-EBUSY);dom.type=2;
 dom.iova_cookie=NULL;CHECK("DMA provider defer",PTR_ERR(msm_smmu_new(&dev,MSM_SMMU_DOMAIN_UNSECURE))==-EPROBE_DEFER);dom.iova_cookie=(void*)1;
 dom.cookie_type=2;CHECK("wrong cookie deferred",PTR_ERR(msm_smmu_new(&dev,MSM_SMMU_DOMAIN_UNSECURE))==-EPROBE_DEFER);dom.cookie_type=1;
 dev.driver=NULL;CHECK("unbound MDSS defer",PTR_ERR(msm_smmu_new(&dev,MSM_SMMU_DOMAIN_UNSECURE))==-EPROBE_DEFER);dev.driver=(void*)1;
 struct iommu_domain other={2,(void*)1,1};dev.domain=&other;CHECK("stale domain denied",msm_smmu_attach(mmu,NULL,0)==-ESTALE);dev.domain=&dom;
 CHECK("attach validates",msm_smmu_attach(mmu,NULL,0)==0);CHECK("detach not fake success",msm_smmu_detach(mmu,NULL,0)==-EOPNOTSUPP);
 CHECK("manual map denied",msm_smmu_map(mmu,0,&tab,1,0)==-EOPNOTSUPP);CHECK("identity map denied",msm_smmu_one_to_one_map(mmu,1,2,3,0)==-EOPNOTSUPP);
