/* Host-only model/fixture; see check-sde-native-README.md. */

int main(void){int p=0,f=0,ret;struct drm_device dev;INIT_LIST_HEAD(&dev.mode_config.crtc_list);struct sde_kms k={.dev=&dev};struct drm_crtc_state cs={true,1,0,0};struct drm_crtc c={.state=&cs};list_add(&c.head,&dev.mode_config.crtc_list);struct drm_atomic_state st={&c,&cs,1};
#define CHECK(n,x) do{if(x){p++;printf("PASS %s\n",n);}else{f++;printf("FAIL %s\n",n);}}while(0)
 CHECK("null atomic rejected",sde_kms_check_secure_transition(&k.base,NULL)==-EINVAL);
 CHECK("nonsecure admitted",sde_kms_check_secure_transition(&k.base,&st)==0);
 cs.sec=1;ret=sde_kms_check_secure_transition(&k.base,&st);CHECK("secure translated rejected",ret==-EOPNOTSUPP);
 cs.sec=0;cs.direct=1;ret=sde_kms_check_secure_transition(&k.base,&st);CHECK("secure direct rejected",ret==-EOPNOTSUPP);
 st.count=0;ret=sde_kms_check_secure_transition(&k.base,&st);CHECK("existing global secure rejected",ret==-EOPNOTSUPP);
 printf("RESULT %d green %d red\n",p,f);return f?1:0;}
