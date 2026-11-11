/* SPDX-License-Identifier: GPL-2.0-only */
static void*msm_gem_get_vaddr(struct drm_gem_object*o){return get_vaddr(o,MSM_MADV_WILLNEED);}
static struct driver owner_driver={.gem_prime_import_sg_table=msm_gem_prime_import_sg_table};
static struct platform_device pdev;
static struct drm_device*make_owner(void)
{
 pdev.dev.id=3;pdev.dev.pdev=&pdev;
 CHECK(msm_drm_device_init(&pdev,&owner_driver)==0);
 struct drm_device*d=pdev.drvdata;struct msm_drm_private*p=d->dev_private;
#ifndef OWNER_MODEL
 mutex_init(&p->gem_lock);INIT_LIST_HEAD(&p->inactive_list);
#endif
 mutex_init(&p->vm_client_lock);INIT_LIST_HEAD(&p->vm_client_list);
 p->kms=&owner_kms;p->wq=kzalloc(1,GFP_KERNEL);CHECK(p->wq);p->pp_event_thread=(void*)1;
 return d;
}
static struct drm_gem_object*local_object(struct drm_device*d)
{struct drm_gem_object*o=_msm_gem_new(d,4096,MSM_BO_WC,false);CHECK(!IS_ERR(o));return o;}
static struct scatterlist owner_sg={0x20000,4096,NULL};
static struct sg_table owner_sgt={&owner_sg,1,1};
static struct dma_resv owner_resv;
static struct dma_buf owner_buf={.ops=&foreign_ops,.size=4096,.resv=&owner_resv,.refs=1};
static struct mmu_funcs owner_mmu_funcs={mmu_map,mmu_unmap,secure,NULL,mmu_destroy};
static struct msm_mmu owner_mmu={&owner_mmu_funcs,&attach_device,false};
static struct aspace_ops owner_aspace_ops={active_add};
static void setup_import(void)
{
 kms_aspace=kzalloc(sizeof(*kms_aspace),GFP_KERNEL);CHECK(kms_aspace);
 kms_aspace->kref.refs=1;kms_aspace->domain_attached=true;kms_aspace->mmu=&owner_mmu;kms_aspace->ops=&owner_aspace_ops;
 mutex_init(&kms_aspace->list_lock);mapped_sg=&owner_sgt;
}
static void late_local(void)
{struct drm_device*d=make_owner();struct drm_gem_object*o=local_object(d);CHECK(msm_drm_uninit(&pdev.dev)==0);drm_gem_object_put(o);CHECK(device_releases==1&&private_releases==1&&live_alloc==0&&thread_stops==1);}
static void late_import(void)
{struct drm_device*d=make_owner();setup_import();struct drm_gem_object*o=msm_gem_prime_import(d,&owner_buf);CHECK(!IS_ERR(o));CHECK(msm_drm_uninit(&pdev.dev)==0);CHECK(domain_alive);drm_gem_object_put(o);CHECK(unmaps==1&&detaches==1&&owner_buf.refs==1&&domain_destroys==1&&private_releases==1&&live_alloc==0);}
static void late_file(void)
{struct drm_device*d=make_owner();struct drm_file f={0};CHECK(context_init(d,&f)==0);struct msm_file_private*c=f.driver_priv;c->enable_refcnt=2;pm_refs++;CHECK(msm_drm_uninit(&pdev.dev)==0);msm_preclose(d,&f);msm_postclose(d,&f);CHECK(!f.driver_priv&&hook_open==1&&hook_pre==0&&hook_post==0&&pm_put_noidles==1&&device_releases==1&&private_releases==1&&live_alloc==0);}
static void live_close(void)
{struct drm_device*d=make_owner();struct drm_file f={0};CHECK(context_init(d,&f)==0);msm_preclose(d,&f);msm_postclose(d,&f);CHECK(!f.driver_priv&&hook_open==1&&hook_pre==1&&hook_post==1);CHECK(msm_drm_uninit(&pdev.dev)==0&&live_alloc==0);}
static void no_kms_close(void)
{struct drm_device*d=make_owner();struct msm_drm_private*p=d->dev_private;p->kms=NULL;struct drm_file f={0};CHECK(context_init(d,&f)==0);msm_postclose(d,&f);CHECK(!f.driver_priv);p->kms=&owner_kms;CHECK(msm_drm_uninit(&pdev.dev)==0&&live_alloc==0);}
static void object_alloc_fail(void)
{struct drm_device*d=make_owner();alloc_fail=allocations+1;struct drm_gem_object*o=_msm_gem_new(d,4096,MSM_BO_WC,false);CHECK(IS_ERR(o)&&PTR_ERR(o)==-ENOMEM&&atomic_load(&d->refs)==1&&list_empty(&((struct msm_drm_private*)d->dev_private)->inactive_list));alloc_fail=0;CHECK(msm_drm_uninit(&pdev.dev)==0&&live_alloc==0);}
static void core_init_fail(void)
{struct drm_device*d=make_owner();core_init_error=-EIO;struct drm_gem_object*o=_msm_gem_new(d,4096,MSM_BO_WC,false);CHECK(IS_ERR(o)&&PTR_ERR(o)==-EIO&&atomic_load(&d->refs)==1&&ref_inits==ref_finis&&list_empty(&((struct msm_drm_private*)d->dev_private)->inactive_list));core_init_error=0;CHECK(msm_drm_uninit(&pdev.dev)==0&&live_alloc==0);}
static void late_vram(void)
{struct drm_device*d=make_owner();struct msm_drm_private*p=d->dev_private;struct drm_gem_object*o=local_object(d);struct msm_gem_object*m=to_msm_bo(o);struct drm_mm_node node={3};m->vram_node=&node;m->pages=kzalloc(sizeof(struct page*),GFP_KERNEL);p->vram.size=4096;p->vram.paddr=0x30000;p->vram_initialized=true;p->vram_allocated=true;vram_nodes=1;backing_alive=true;CHECK(msm_drm_uninit(&pdev.dev)==0);CHECK(backing_alive&&mm_takedowns==0&&physaddr(o)==0x33000);drm_gem_object_put(o);CHECK(!backing_alive&&mm_takedowns==1&&dma_frees==1&&freed_attrs==(DMA_ATTR_NO_KERNEL_MAPPING|DMA_ATTR_WRITE_COMBINE)&&live_alloc==0);}
static void vram_zero(void)
{struct drm_device*d=make_owner();struct msm_drm_private*p=d->dev_private;p->vram_initialized=true;p->vram_allocated=true;p->vram.size=4096;p->vram.paddr=0;backing_alive=true;CHECK(msm_drm_uninit(&pdev.dev)==0);CHECK(!backing_alive&&dma_frees==1&&live_alloc==0);}
static void publication(void)
{struct drm_device*d=make_owner();struct drm_gem_object*a=local_object(d),*b=local_object(d);CHECK(early_publish==0);drm_gem_object_put(a);drm_gem_object_put(b);CHECK(msm_drm_uninit(&pdev.dev)==0&&live_alloc==0);}
static void device_power_fail(void)
{pdev.dev.pdev=&pdev;power_error=-EIO;CHECK(msm_drm_device_init(&pdev,&owner_driver)==-EIO);CHECK(!pdev.drvdata&&live_alloc==0&&device_releases==1);}
static void device_debug_fail(void)
{pdev.dev.pdev=&pdev;debug_error=-EAGAIN;CHECK(msm_drm_device_init(&pdev,&owner_driver)==-EAGAIN);CHECK(!pdev.drvdata&&!power_alive&&live_alloc==0&&device_releases==1);}
static void device_pm_fail(void)
{pdev.dev.pdev=&pdev;pm_error=-ETIMEDOUT;CHECK(msm_drm_device_init(&pdev,&owner_driver)==-ETIMEDOUT);CHECK(!pdev.drvdata&&!power_alive&&!debug_alive&&pm_refs==0&&pm_disabled&&live_alloc==0);}
static void managed_action_fail(void)
{pdev.dev.pdev=&pdev;action_error=-ENOMEM;CHECK(msm_drm_device_init(&pdev,&owner_driver)==-ENOMEM);CHECK(!pdev.drvdata&&private_releases==1&&device_releases==1&&live_alloc==0);}
static void context_alloc_fail(void)
{struct drm_device*d=make_owner();struct drm_file f={0};alloc_fail=allocations+1;CHECK(context_init(d,&f)==-ENOMEM&&!f.driver_priv&&atomic_load(&d->refs)==1);alloc_fail=0;CHECK(msm_drm_uninit(&pdev.dev)==0&&live_alloc==0);}
static void late_mapping_denied(void)
{struct drm_device*d=make_owner();struct drm_gem_object*o=local_object(d);drm_dev_get(d);CHECK(msm_drm_uninit(&pdev.dev)==0);uint64_t iova=0xdead;struct msm_gem_object*m=to_msm_bo(o);mutex_lock(&m->lock);int ret=msm_gem_get_iova_locked(o,NULL,&iova);mutex_unlock(&m->lock);CHECK(ret==-ENODEV&&iova==0xdead&&mmu_maps==0);drm_gem_object_put(o);drm_dev_put(d);CHECK(live_alloc==0);}
static void late_new_denied(void)
{struct drm_device*d=make_owner();drm_dev_get(d);CHECK(msm_drm_uninit(&pdev.dev)==0);struct drm_gem_object*o=_msm_gem_new(d,4096,MSM_BO_WC,false);CHECK(IS_ERR(o)&&PTR_ERR(o)==-ENODEV);struct drm_file f={0};CHECK(context_init(d,&f)==-ENODEV&&!f.driver_priv);drm_dev_put(d);CHECK(live_alloc==0);}
static void late_power_denied(void)
{struct drm_device*d=make_owner();struct drm_file f={0};CHECK(context_init(d,&f)==0);CHECK(msm_drm_uninit(&pdev.dev)==0);struct drm_msm_power_ctrl ctrl={.enable=1};int old=pm_refs;CHECK(msm_ioctl_power_ctrl(d,&ctrl,&f)==-ENODEV&&pm_refs==old);msm_postclose(d,&f);CHECK(live_alloc==0);}
static struct drm_gem_object*import_result;
static struct drm_device*thread_device;
static struct drm_file thread_file;
static void*import_thread(void*x){(void)x;import_result=msm_gem_prime_import(thread_device,&owner_buf);return NULL;}
static void*unbind_thread(void*x){(void)x;CHECK(msm_drm_uninit(&pdev.dev)==0);atomic_store(&teardown_done,1);return NULL;}
static void*close_thread(void*x){(void)x;msm_postclose(thread_device,&thread_file);return NULL;}
static void wait_retired(struct msm_drm_private*p)
{
#ifdef OWNER_MODEL
 for(;;){mutex_lock(&p->lifetime_lock);bool done=p->retired;mutex_unlock(&p->lifetime_lock);if(done)break;usleep(1000);}
#else
 (void)p;while(!atomic_load(&teardown_done))usleep(1000);
#endif
}
static void import_vs_unbind(void)
{thread_device=make_owner();struct msm_drm_private*p=thread_device->dev_private;setup_import();block_map=1;pthread_t a,b;CHECK(!pthread_create(&a,NULL,import_thread,NULL));wait_block();CHECK(!pthread_create(&b,NULL,unbind_thread,NULL));wait_retired(p);
#ifdef OWNER_MODEL
 CHECK(!kms_destroys&&!device_releases&&!private_releases);CHECK(!msm_drm_owner_get_live(p));
#endif
 unblock();pthread_join(a,NULL);pthread_join(b,NULL);CHECK(IS_ERR(import_result)&&PTR_ERR(import_result)==-ENODEV&&unmaps==1&&detaches==1&&live_maps==0&&live_attach==0&&owner_buf.refs==1&&private_releases==1&&live_alloc==0);}
static void close_vs_unbind(void)
{thread_device=make_owner();struct msm_drm_private*p=thread_device->dev_private;CHECK(context_init(thread_device,&thread_file)==0);block_post=1;pthread_t a,b;CHECK(!pthread_create(&a,NULL,close_thread,NULL));wait_block();CHECK(!pthread_create(&b,NULL,unbind_thread,NULL));wait_retired(p);
#ifdef OWNER_MODEL
 CHECK(!kms_destroys&&!device_releases&&!private_releases);
#endif
 unblock();pthread_join(a,NULL);pthread_join(b,NULL);CHECK(!thread_file.driver_priv&&hook_post==1&&private_releases==1&&live_alloc==0);}
static void*free_thread(void*x){drm_gem_object_put(x);return NULL;}
static void parallel_close(void)
{struct drm_device*d=make_owner();enum{N=32};struct drm_gem_object*objects[N];pthread_t t[N],u;for(int i=0;i<N;i++)objects[i]=local_object(d);CHECK(!pthread_create(&u,NULL,unbind_thread,NULL));for(int i=0;i<N;i++)CHECK(!pthread_create(&t[i],NULL,free_thread,objects[i]));for(int i=0;i<N;i++)pthread_join(t[i],NULL);pthread_join(u,NULL);CHECK(device_releases==1&&private_releases==1&&live_alloc==0);}

int main(void)
{
 struct {const char*name;void(*run)(void);}cases[]={
  {"late local GEM owner",late_local},{"late imported GEM attachment owner",late_import},{"late file closes fully",late_file},
  {"live hooks preserved",live_close},{"no KMS context still closes",no_kms_close},{"object alloc failure",object_alloc_fail},
  {"GEM core init failure",core_init_fail},{"VRAM backing after last GEM",late_vram},{"VRAM DMA zero released",vram_zero},
  {"publish after core init",publication},{"device power failure",device_power_fail},{"device debug failure",device_debug_fail},
  {"device PM failure",device_pm_fail},{"managed action failure",managed_action_fail},{"context alloc failure",context_alloc_fail},
  {"retired mapping rejected",late_mapping_denied},{"retired new/open rejected",late_new_denied},{"retired PM vote rejected",late_power_denied},
  {"real import vs uninit",import_vs_unbind},{"real postclose vs uninit",close_vs_unbind},{"32 parallel final GEM closes",parallel_close}};
 int green=0,red=0;
 for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  fflush(NULL);pid_t child=fork();CHECK(child>=0);
  if(!child){scenarios=(int)i+1;alarm(10);cases[i].run();exit(0);}
  int status;CHECK(waitpid(child,&status,0)==child);
  if(WIFEXITED(status)&&WEXITSTATUS(status)==0){green++;printf("PASS %s\n",cases[i].name);}else{red++;printf("FAIL %s child_status=%d\n",cases[i].name,status);}
 }
 printf("RESULT %d green %d red\n",green,red);return red?1:0;
}
