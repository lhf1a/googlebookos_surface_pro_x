/* Host-only model/fixture; see check-sde-native-README.md. */

 unregisters=device_puts=0;msm_smmu_destroy(mmu);CHECK("MDSS never unregistered",unregisters==0);CHECK("device ref released",device_puts==1&&dev.refs==0);
 printf("RESULT %d green %d red\n",pass,fail);return fail?1:0;}
