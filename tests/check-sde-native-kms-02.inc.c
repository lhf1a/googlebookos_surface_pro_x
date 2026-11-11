/* Host-only model/fixture; see check-sde-native-README.md. */

static int p,fails;
#define CHECK(n,c) do{if(c){p++;printf("PASS %s\n",n);}else{fails++;printf("FAIL %s\n",n);}}while(0)
int main(void){struct drm_device d={(void*)1};struct sde_kms k={.dev=&d};int ret;
 create_err=-ENODEV;ret=_sde_kms_mmu_init(&k);CHECK("required domain error propagated",ret==-ENODEV&&!k.base.aspace);
 create_err=-EBUSY;ret=_sde_kms_mmu_init(&k);CHECK("foreign owner error propagated",ret==-EBUSY);
 create_err=0;attach_err=-ESTALE;destroyed=0;ret=_sde_kms_mmu_init(&k);CHECK("init attach failure unpublished",ret==-ESTALE&&!k.aspace[0]&&destroyed==1);
 k=(struct sde_kms){.dev=&d};attach_err=0;aspace_err=-ENOMEM;destroyed=0;ret=_sde_kms_mmu_init(&k);CHECK("aspace allocation unwind",ret==-ENOMEM&&!k.base.aspace&&destroyed==1);
 aspace_err=0;k=(struct sde_kms){.dev=&d};k.splash_data.num_splash_regions=1;ret=_sde_kms_mmu_init(&k);CHECK("splash refused",ret==-EOPNOTSUPP&&!k.base.aspace);
 k=(struct sde_kms){.dev=&d};k.splash_data.num_splash_displays=1;ret=_sde_kms_mmu_init(&k);CHECK("unknown splash display refused",ret==-EOPNOTSUPP&&!k.base.aspace);
 k=(struct sde_kms){.dev=&d};ret=_sde_kms_mmu_init(&k);CHECK("nonsecure init success",ret==0&&k.base.aspace==&aspace&&k.aspace[0]==&aspace);
 CHECK("unsupported secure absent",!k.aspace[2]&&!k.aspace[3]);
 aspace.domain_attached=false;attach_err=-ESTALE;updates=0;ret=sde_kms_mmu_attach(&k,false);CHECK("attach error retains unpublished state",ret==-ESTALE&&!aspace.domain_attached&&updates==0);
 attach_err=0;ret=sde_kms_mmu_attach(&k,false);CHECK("attach success publishes",ret==0&&aspace.domain_attached);
 update_err=-ENOMEM;ret=sde_kms_mmu_attach(&k,false);CHECK("snapshot ENOMEM retained attached owner",ret==-ENOMEM&&aspace.domain_attached);update_err=-EBUSY;ret=sde_kms_mmu_attach(&k,false);CHECK("snapshot EBUSY retained attached owner",ret==-EBUSY&&aspace.domain_attached);update_err=0;
 updates=0;ret=sde_kms_mmu_detach(&k,false);CHECK("detach refusal before owner cleanup",ret==-EOPNOTSUPP&&aspace.domain_attached&&updates==0);
 ret=sde_kms_mmu_attach(&k,true);CHECK("secure attach refused",ret==-EOPNOTSUPP);
 _sde_kms_mmu_destroy(&k);CHECK("destroy unpublishes base",!k.base.aspace&&!k.aspace[0]);
 printf("RESULT %d green %d red\n",p,fails);return fails?1:0;}
