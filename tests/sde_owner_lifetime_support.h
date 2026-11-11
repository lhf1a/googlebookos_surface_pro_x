/* SPDX-License-Identifier: GPL-2.0-only */
/* Explicit managed-core/resource leaves; none of these model hardware stop. */
#define DMA_ATTR_NO_KERNEL_MAPPING 1UL
#define DMA_ATTR_WRITE_COMBINE 2UL
#define SDE_POWER_HANDLE_DBUS_ID_MAX 1
#define SDE_POWER_HANDLE_CONT_SPLASH_BUS_AB_QUOTA 1
#define SDE_POWER_HANDLE_CONT_SPLASH_BUS_IB_QUOTA 1
#define unlikely(x) (x)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define dev_info(...) ((void)0)
static int drm_alloc_error,action_error,power_error,debug_error,pm_error,core_init_error;
static int private_releases,device_releases,kms_destroys,wq_destroys,thread_stops,mm_takedowns,dma_frees,vram_nodes;
static int pm_refs,pm_put_syncs,pm_put_noidles,pm_disabled,power_alive,debug_alive;
static int hook_open,hook_pre,hook_post;
static bool backing_alive;
static unsigned long freed_attrs;
static pthread_mutex_t blocker_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t blocker_cv=PTHREAD_COND_INITIALIZER;
static int block_map,block_post,entered_block,release_block;
static atomic_int teardown_done;

static void blocking_point(int enabled)
{
 if(!enabled)return;
 pthread_mutex_lock(&blocker_mutex);entered_block=1;pthread_cond_broadcast(&blocker_cv);
 while(!release_block)pthread_cond_wait(&blocker_cv,&blocker_mutex);
 pthread_mutex_unlock(&blocker_mutex);
}
static void block_attachment_map(void){blocking_point(block_map);}
static void wait_block(void){pthread_mutex_lock(&blocker_mutex);while(!entered_block)pthread_cond_wait(&blocker_cv,&blocker_mutex);pthread_mutex_unlock(&blocker_mutex);}
static void unblock(void){pthread_mutex_lock(&blocker_mutex);release_block=1;pthread_cond_broadcast(&blocker_cv);pthread_mutex_unlock(&blocker_mutex);}

static void drm_dev_get(struct drm_device*d){CHECK(atomic_fetch_add(&d->refs,1)>0);}
static void drm_dev_put(struct drm_device*d)
{
 if(!d)return;
 CHECK(atomic_load(&d->refs)>0);
 if(atomic_fetch_sub(&d->refs,1)==1){
  if(d->managed_action){private_releases++;d->managed_action(d,d->managed_priv);}
  if(d->managed_priv)kfree(d->managed_priv);
  device_releases++;free(d);
 }
}
static struct drm_device*drm_dev_alloc(struct driver*drv,struct device*parent)
{
 if(drm_alloc_error)return ERR_PTR(drm_alloc_error);
 struct drm_device*d=calloc(1,sizeof(*d));CHECK(d);d->dev=parent;d->driver=drv;
 atomic_init(&d->refs,1);mutex_init(&d->struct_mutex);mutex_init(&d->object_name_lock);return d;
}
static void*drmm_kzalloc(struct drm_device*d,size_t n,int g){void*p=kzalloc(n,g);if(p)d->managed_priv=p;return p;}
static int drmm_add_action_or_reset(struct drm_device*d,void(*action)(struct drm_device*,void*),void*p)
{
 if(action_error){private_releases++;action(d,p);return action_error;}
 d->managed_action=action;return 0;
}
static void drm_mm_takedown(struct drm_mm*m){(void)m;CHECK(vram_nodes==0);mm_takedowns++;}
static void drm_mm_remove_node(struct drm_mm_node*n){CHECK(n&&vram_nodes>0);vram_nodes--;}
static void dma_free_attrs(struct device*d,unsigned long n,void*v,dma_addr_t p,unsigned long attrs)
{(void)d;(void)n;(void)v;(void)p;CHECK(backing_alive&&vram_nodes==0);backing_alive=false;freed_attrs=attrs;dma_frees++;}
#define spin_lock(p) ((void)(p))
#define spin_unlock(p) ((void)(p))
static void put_pages_vram(struct drm_gem_object*);
static void put_pages(struct drm_gem_object*o)
{
 struct msm_gem_object*m=to_msm_bo(o);
 if(m->pages&&m->vram_node){put_pages_vram(o);m->pages=NULL;}
}
static int drm_gem_object_init(struct drm_device*d,struct drm_gem_object*o,size_t n)
{drm_gem_private_object_init(d,o,n);return core_init_error;}
static void flush_workqueue(void*w){CHECK(w);}
static void destroy_workqueue(void*w){CHECK(w);wq_destroys++;kfree(w);}
static void kthread_flush_worker(int*w){(void)w;}
static void kthread_stop(void*t){CHECK(t);thread_stops++;}
static int pm_runtime_get_sync(struct device*d){(void)d;pm_refs++;return pm_error;}
static void pm_runtime_put_sync(struct device*d){(void)d;CHECK(pm_refs>0);pm_refs--;pm_put_syncs++;}
static void pm_runtime_put_noidle(struct device*d){(void)d;CHECK(pm_refs>0);pm_refs--;pm_put_noidles++;}
static void pm_runtime_enable(struct device*d){(void)d;pm_disabled=0;}
static void pm_runtime_disable(struct device*d){(void)d;pm_disabled=1;}
static int sde_power_resource_init(struct platform_device*p,int*h){(void)p;(void)h;if(power_error)return power_error;power_alive=1;return 0;}
static void sde_power_resource_deinit(struct platform_device*p,int*h){(void)p;(void)h;CHECK(power_alive);power_alive=0;}
static int sde_dbg_init(struct device*d){(void)d;if(debug_error)return debug_error;debug_alive=1;return 0;}
static void sde_dbg_destroy(void){debug_alive=0;}
static void sde_power_data_bus_set_quota(int*h,int i,int a,int b){(void)h;(void)i;(void)a;(void)b;}
#define drm_mode_config_init(d) ((void)(d))
#define drm_mode_config_cleanup(d) ((void)(d))
#define drm_kms_helper_poll_fini(d) ((void)(d))
#define drm_dev_unregister(d) ((void)(d))
#define drm_atomic_helper_shutdown(d) ((void)(d))
#define drm_irq_uninstall(d) ((void)(d))
#define component_unbind_all(d,g) ((void)(d),(void)(g))
#define msm_mdss_destroy(d) ((void)(d))
#define debugfs_remove_recursive(d) ((void)(d))
struct drm_msm_power_ctrl {int enable;};

static struct device*select_device(struct msm_kms*k,u32 domain){CHECK(k&&domain==0);return &attach_device;}
static struct msm_gem_address_space*select_aspace(struct msm_kms*k,u32 domain){CHECK(k&&domain==0);return kms_aspace;}
static void kms_debug_destroy(struct msm_kms*k){(void)k;}
static void kms_destroy(struct msm_kms*k){(void)k;kms_destroys++;if(kms_aspace){msm_gem_address_space_put(kms_aspace);kms_aspace=NULL;}}
static void postopen(struct msm_kms*k,struct drm_file*f){(void)k;(void)f;CHECK(!kms_destroys);hook_open++;}
static void preclose(struct msm_kms*k,struct drm_file*f){(void)k;(void)f;CHECK(!kms_destroys);hook_pre++;}
static void postclose(struct msm_kms*k,struct drm_file*f){(void)k;(void)f;CHECK(!kms_destroys);hook_post++;blocking_point(block_post);CHECK(!kms_destroys);}
static struct kms_funcs owner_kms_funcs={.get_address_space_device=select_device,.get_address_space=select_aspace,.debugfs_destroy=kms_debug_destroy,.destroy=kms_destroy,.postopen=postopen,.preclose=preclose,.postclose=postclose};
static struct msm_kms owner_kms={.funcs=&owner_kms_funcs};
static void*msm_gem_get_vaddr(struct drm_gem_object*o);
int msm_gem_delayed_import(struct drm_gem_object*o);
