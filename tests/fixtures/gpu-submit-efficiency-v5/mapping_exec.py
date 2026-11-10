# SPDX-License-Identifier: GPL-2.0-only
# Extract actual source; primitives are mocked, not a full-kernel proof.
from pathlib import Path
import hashlib,json,re,shutil,subprocess
import os
D=Path(os.environ["GPU_SUBMIT_TEST_DIR"])
K=Path(os.environ["GPU_SUBMIT_KERNEL_SOURCE"])
def function(s,name):
    m=re.search(r'(?m)^(?:static\s+)?(?:struct page \*\*|int\s+|void\s+|bool\s+)'+re.escape(name)+r'\(',s)
    assert m,name
    a=s.index('{',m.start()); depth=1; b=a+1
    while depth:
        if s[b]=='{':depth+=1
        elif s[b]=='}':depth-=1
        b+=1
    return s[m.start():b]+'\n'
def execute(label,cmd,expected=0):
    p=subprocess.run(cmd,capture_output=True,text=True)
    r=dict(label=label,command=cmd,stdout=p.stdout,stderr=p.stderr,exit=p.returncode)
    results.append(r)
    (D/'combined-v4-test-results.json').write_text(json.dumps(results,indent=2)+'\n')
    with (D/'VERIFICATION.txt').open('a') as f:f.write('\nCOMBINED V4 TEST\n'+json.dumps(r,indent=2)+'\n')
    print(label,'EXIT',p.returncode,p.stdout,p.stderr,flush=True)
    assert p.returncode==expected,r
    return r
results=json.loads((D/'combined-v4-test-results.json').read_text()) if (D/'combined-v4-test-results.json').exists() else []
roles=[('','source','msm_gem.c'),('submit.','submit','msm_gem_submit.c'),
       ('scheduler.','scheduler','sched_main.c'),('exec.','exec','drm_exec.c')]
restore=D/'combined-v4-restored';restore.mkdir(exist_ok=True)
for part,role,name in roles:
    src=D/('MODIFIED_FILE.'+part+'c') if part else D/'MODIFIED_FILE.c'
    # exec. and submit. already include their separator, not their suffix.
    shutil.copyfile(src,restore/name)
    execute('rollback-'+role,['sh',str(D/'ROLLBACK.sh'),str(restore/name),role])
    assert (restore/name).read_bytes()==(D/('BASELINE.'+part+'c') if part else D/'BASELINE.c').read_bytes()
shutil.copyfile(D/'MODIFIED_FILE.exec.h',restore/'drm_exec.h')
execute('rollback-exec-header',['sh',str(D/'ROLLBACK.sh'),str(restore/'drm_exec.h'),'exec-header'])
assert (restore/'drm_exec.h').read_bytes()==(D/'BASELINE.exec.h').read_bytes()

map_prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
struct page {int unused;};
struct sg_table {int unused;};
struct drm_gem_object {void *dev;};
struct drm_gpuvm {int unused;};
struct msm_gem_vm {struct drm_gpuvm base;bool managed;int mmu_lock;};
struct drm_gpuva {struct drm_gpuvm *vm;struct {uint64_t addr,range;} va;struct {uint64_t offset;} gem;};
struct msm_gem_vma {struct drm_gpuva base;bool mapped;};
struct msm_gem_object {struct drm_gem_object base;unsigned flags,madv;struct page **pages;struct sg_table *sgt;};
struct msm_vm_map_op {uint64_t iova,range,offset;struct sg_table *sgt;int prot;};
#define to_msm_bo(x) ((struct msm_gem_object *)(x))
#define to_msm_vma(x) ((struct msm_gem_vma *)(x))
#define to_msm_vm(x) ((struct msm_gem_vm *)(x))
#define MSM_MADV_WILLNEED 0
#define MSM_BO_GPU_READONLY 1
#define MSM_BO_MAP_PRIV 2
#define MSM_BO_CACHED_COHERENT 4
#define IOMMU_READ 1
#define IOMMU_WRITE 2
#define IOMMU_PRIV 4
#define IOMMU_CACHE 8
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095)
static unsigned warns,logs,gets,mapchecks,actualmaps,locks,prot_calls;
static int page_error,map_error,last_prot;static bool held;
static struct page page;static struct page *pages=&page;static struct sg_table sgt;
#define GEM_WARN_ON(x) ((x)?(warns++,true):false)
#define DRM_DEV_DEBUG_DRIVER(...) (logs++)
#define msm_gem_assert_locked(x) assert(held)
static void mutex_lock(int *x){assert(!*x);*x=1;locks++;}
static void mutex_unlock(int *x){assert(*x);*x=0;}
static struct page **get_pages(struct drm_gem_object *obj){
 gets++;struct msm_gem_object *o=to_msm_bo(obj);
 if(o->pages)return o->pages;
 if(page_error)return ERR_PTR(page_error);
 o->pages=&pages;o->sgt=&sgt;return o->pages;
}
static int vm_map_op(struct msm_gem_vm *vm,const struct msm_vm_map_op *op){
 assert(held);assert(op->sgt);assert(vm->managed || vm->mmu_lock);
 actualmaps++;last_prot=op->prot;return map_error;
}
'''
map_main=r'''
int main(int argc,char **argv){
 unsigned cases=0;held=true;
 for(unsigned flag=0;flag<8;flag++)for(unsigned madv=0;madv<3;madv++)
 for(unsigned mapped=0;mapped<2;mapped++)for(unsigned backing=0;backing<2;backing++)
 for(unsigned address=0;address<2;address++)for(unsigned managed=0;managed<2;managed++)
 for(int pe=0;pe<3;pe++)for(int me=0;me<3;me++){
  struct msm_gem_object o={.flags=flag,.madv=madv,.pages=backing?&pages:NULL,.sgt=backing?&sgt:NULL};
  struct msm_gem_vm vm={.managed=managed};struct msm_gem_vma v={.base={.vm=&vm.base,.va={address?0x10000:0,4096}},.mapped=mapped};
  page_error=pe==0?0:pe==1?-ENOMEM:-EIO;map_error=me==0?0:me==1?-ENOSPC:-EIO;
  warns=logs=gets=mapchecks=actualmaps=locks=prot_calls=0;
  int expected=madv?-EBUSY:(!backing && page_error)?page_error:!address?-EINVAL:mapped?0:map_error;
  int ret=msm_gem_pin_vma_locked(&o.base,&v.base);
  assert(ret==expected);
  assert(logs==(madv?1u:0u));
  bool pages_ok=!madv && (backing || !page_error);
  assert(warns==(pages_ok && !address));
  assert((o.pages!=NULL)==(backing || (!madv && !page_error)));
  bool actual=pages_ok && address && !mapped;
  assert(actualmaps==actual);assert(locks==(actual && !managed));assert(!vm.mmu_lock);
  assert(v.mapped==(actual ? !map_error : mapped));
  if(actual)assert(last_prot==(IOMMU_READ|((flag&1)?0:IOMMU_WRITE)|((flag&2)?IOMMU_PRIV:0)|((flag&4)?IOMMU_CACHE:0)));
  cases++;
 }
 struct msm_gem_object o={.pages=&pages,.sgt=&sgt};struct msm_gem_vm vm={.managed=true};
 struct msm_gem_vma v={.base={.vm=&vm.base,.va={0x10000,4096}},.mapped=true};
 gets=mapchecks=actualmaps=prot_calls=0;
 for(int i=0;i<15000;i++)assert(msm_gem_pin_vma_locked(&o.base,&v.base)==0);
 printf("mapped_semantics=PASS cases=%u hot_calls=15000 page_checks=%u map_checks=%u prot_calls=%u actual_maps=%u madv_address_error_order=PASS\n",cases,gets,mapchecks,prot_calls,actualmaps);
 return argc>1 && !strcmp(argv[1],"--require-fast") && (gets || mapchecks || prot_calls);
}
'''

exec_prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define BIT(n) (1u<<(n))
#define likely(x) (x)
#define unlikely(x) (x)
#define PAGE_SIZE 4096
#define GFP_KERNEL 0
#define EXPORT_SYMBOL(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)
#define __PASTE(a,b) a##b
#define JOIN(a,b) __PASTE(a,b)
#define __UNIQUE_ID(a) JOIN(a,__COUNTER__)
struct ww_acquire_ctx {int state;};
static int reservation_ww_class;
static void ww_acquire_init(struct ww_acquire_ctx *c,void *x){(void)x;c->state=1;}
static void ww_acquire_done(struct ww_acquire_ctx *c){assert(c->state==1);}
static void ww_acquire_fini(struct ww_acquire_ctx *c){assert(c->state==1);c->state=0;}
struct drm_gem_object;
struct dma_resv {bool locked;int fail,fail_count,reserve_error;struct drm_gem_object *owner;};
struct drm_gem_object {int refs;struct dma_resv *resv;bool retire_on_unlock;};
static unsigned gets,ref_puts,frees,lock_calls,unlock_calls,alloc_calls,slow_calls,checks;
static int alloc_fail_at,slow_error;
static void *kvmalloc_array(size_t n,size_t z,int f){(void)f;alloc_calls++;return alloc_calls==(unsigned)alloc_fail_at?NULL:malloc(n*z);}
static void *kvrealloc(void *p,size_t n,int f){(void)f;alloc_calls++;return alloc_calls==(unsigned)alloc_fail_at?NULL:realloc(p,n);}
#define kvfree free
static void drm_gem_object_get(struct drm_gem_object *o){if(!o)return;assert(o->refs>0);o->refs++;gets++;}
static void drm_gem_object_put(struct drm_gem_object *o){if(!o)return;assert(o->refs>0);ref_puts++;if(!--o->refs){assert(!o->resv->locked);free(o->resv);free(o);frees++;}}
static struct drm_gem_object *object(void){struct drm_gem_object *o=calloc(1,sizeof(*o));assert(o);o->resv=calloc(1,sizeof(*o->resv));assert(o->resv);o->refs=1;o->resv->owner=o;return o;}
static int dma_resv_lock(struct dma_resv *r,struct ww_acquire_ctx *ctx){assert(ctx->state);lock_calls++;if(r->locked)return -EALREADY;if(r->fail_count){r->fail_count--;return r->fail;}r->locked=true;return 0;}
#define dma_resv_lock_interruptible dma_resv_lock
static int dma_resv_lock_slow_interruptible(struct dma_resv *r,struct ww_acquire_ctx *ctx){assert(ctx->state);slow_calls++;if(slow_error)return slow_error;assert(!r->locked);r->locked=true;return 0;}
static void dma_resv_lock_slow(struct dma_resv *r,struct ww_acquire_ctx *ctx){assert(!dma_resv_lock_slow_interruptible(r,ctx));}
static void dma_resv_unlock(struct dma_resv *r){assert(r->locked);r->locked=false;unlock_calls++;struct drm_gem_object *o=r->owner;if(o->retire_on_unlock){o->retire_on_unlock=false;drm_gem_object_put(o);}}
static int dma_resv_reserve_fences(struct dma_resv *r,unsigned n){assert(r->locked);(void)n;return r->reserve_error;}
'''
exec_main=r'''
static void reset(void){gets=ref_puts=frees=lock_calls=unlock_calls=alloc_calls=slow_calls=0;alloc_fail_at=slow_error=0;}
static void finish(struct drm_exec *e,struct drm_gem_object **o,unsigned n){drm_exec_fini(e);for(unsigned i=0;i<n;i++){assert(o[i]->refs==1);assert(!o[i]->resv->locked);drm_gem_object_put(o[i]);}assert(frees==n);checks++;}
static void cases(unsigned flags){
 struct drm_exec e;struct drm_gem_object *o[3];
 // Baseline/default and borrowed modes must both retain valid locking.
 reset();for(int i=0;i<3;i++)o[i]=object();drm_exec_init(&e,flags,3);assert(drm_exec_cleanup(&e));
 for(int i=0;i<3;i++)assert(!drm_exec_prepare_obj(&e,o[i],1));
 drm_exec_unlock_obj(&e,o[1]);assert(e.num_objects==2);assert(!o[1]->resv->locked);
 drm_exec_unlock_obj(&e,o[1]);assert(!drm_exec_cleanup(&e));finish(&e,o,3);
 // Two successive contentions; second replaces an unconsumed prelock.
 reset();for(int i=0;i<3;i++)o[i]=object();o[1]->resv->fail=-EDEADLK;o[1]->resv->fail_count=1;o[2]->resv->fail=-EDEADLK;o[2]->resv->fail_count=1;
 drm_exec_init(&e,flags,1);assert(drm_exec_cleanup(&e));assert(!drm_exec_lock_obj(&e,o[0]));assert(drm_exec_lock_obj(&e,o[1])==-EDEADLK);
 assert(drm_exec_cleanup(&e));assert(drm_exec_lock_obj(&e,o[2])==-EDEADLK);assert(e.prelocked==o[1]);
 assert(drm_exec_cleanup(&e));for(int i=0;i<3;i++)assert(!drm_exec_prepare_obj(&e,o[i],1));assert(!drm_exec_cleanup(&e));assert(slow_calls==2);finish(&e,o,3);
 // Contended object omitted from the next enumeration; empty-array locks it.
 reset();o[0]=object();o[0]->resv->fail=-EDEADLK;o[0]->resv->fail_count=1;drm_exec_init(&e,flags,1);assert(drm_exec_cleanup(&e));assert(drm_exec_lock_obj(&e,o[0])==-EDEADLK);assert(drm_exec_cleanup(&e));assert(!drm_exec_prepare_array(&e,NULL,0,1));assert(e.prelocked==o[0]);assert(!drm_exec_cleanup(&e));finish(&e,o,1);
 // Exit with contended or prelocked ownership still present.
 for(int pre=0;pre<2;pre++){reset();o[0]=object();o[0]->resv->fail=-EDEADLK;o[0]->resv->fail_count=1;drm_exec_init(&e,flags,1);assert(drm_exec_cleanup(&e));assert(drm_exec_lock_obj(&e,o[0])==-EDEADLK);if(pre){assert(drm_exec_cleanup(&e));assert(!drm_exec_prepare_array(&e,NULL,0,1));}finish(&e,o,1);}
 // slow interrupt, regular interrupt/error, reservation failure.
 if(flags&DRM_EXEC_INTERRUPTIBLE_WAIT){reset();o[0]=object();o[0]->resv->fail=-EDEADLK;o[0]->resv->fail_count=1;drm_exec_init(&e,flags,1);assert(drm_exec_cleanup(&e));assert(drm_exec_lock_obj(&e,o[0])==-EDEADLK);assert(drm_exec_cleanup(&e));slow_error=-EINTR;assert(drm_exec_lock_obj(&e,o[0])==-EINTR);finish(&e,o,1);}
 for(int error=0;error<3;error++){reset();o[0]=object();if(error==2)o[0]->resv->reserve_error=-ENOMEM;else{o[0]->resv->fail=error?-EIO:-EINTR;o[0]->resv->fail_count=1;}drm_exec_init(&e,flags,1);assert(drm_exec_cleanup(&e));assert(drm_exec_prepare_obj(&e,o[0],1)==(error==2?-ENOMEM:error?-EIO:-EINTR));finish(&e,o,1);}
 // Init allocation failure recovers; growing an occupied array can fail.
 reset();o[0]=object();alloc_fail_at=1;drm_exec_init(&e,flags,1);assert(!e.objects);assert(drm_exec_cleanup(&e));assert(!drm_exec_lock_obj(&e,o[0]));assert(!drm_exec_cleanup(&e));finish(&e,o,1);
 reset();o[0]=object();o[1]=object();alloc_fail_at=2;drm_exec_init(&e,flags,1);assert(drm_exec_cleanup(&e));assert(!drm_exec_lock_obj(&e,o[0]));assert(drm_exec_lock_obj(&e,o[1])==-ENOMEM);finish(&e,o,2);
 // Duplicate object error vs explicit ignore option.
 reset();o[0]=object();drm_exec_init(&e,flags,1);assert(drm_exec_cleanup(&e));assert(!drm_exec_lock_obj(&e,o[0]));assert(drm_exec_lock_obj(&e,o[0])==-EALREADY);finish(&e,o,1);
 reset();o[0]=object();drm_exec_init(&e,flags|DRM_EXEC_IGNORE_DUPLICATES,1);assert(drm_exec_cleanup(&e));assert(!drm_exec_lock_obj(&e,o[0]));assert(!drm_exec_lock_obj(&e,o[0]));assert(e.num_objects==1);finish(&e,o,1);
 // Force asynchronous retirement at unlock, including freeing the object.
 // ASan detects any subsequent dereference when tracked refs are borrowed.
 reset();for(int i=0;i<3;i++)o[i]=object();drm_exec_init(&e,flags,3);assert(drm_exec_cleanup(&e));for(int i=0;i<3;i++){assert(!drm_exec_lock_obj(&e,o[i]));o[i]->retire_on_unlock=true;}assert(!drm_exec_cleanup(&e));drm_exec_unlock_obj(&e,o[1]);drm_exec_fini(&e);assert(frees==3);checks++;
 // Fini before entering the retry loop.
 reset();drm_exec_init(&e,flags,1);drm_exec_fini(&e);checks++;
}
int main(int argc,char **argv){
 checks=0;cases(0);cases(DRM_EXEC_INTERRUPTIBLE_WAIT);cases(DRM_EXEC_OBJECTS_REF_HELD);cases(DRM_EXEC_INTERRUPTIBLE_WAIT|DRM_EXEC_OBJECTS_REF_HELD);
 // Default API must still take its own references.
 reset();struct drm_gem_object *o=object();struct drm_exec e;drm_exec_init(&e,0,1);assert(drm_exec_cleanup(&e));assert(!drm_exec_lock_obj(&e,o));assert(o->refs==2);assert(!drm_exec_cleanup(&e));drm_exec_fini(&e);assert(o->refs==1);drm_gem_object_put(o);
 reset();unsigned n=15000;struct drm_gem_object **objs=calloc(n,sizeof(*objs));assert(objs);for(unsigned i=0;i<n;i++)objs[i]=object();drm_exec_init(&e,DRM_EXEC_OBJECTS_REF_HELD,n);assert(drm_exec_cleanup(&e));assert(!drm_exec_prepare_array(&e,objs,n,1));assert(!drm_exec_cleanup(&e));drm_exec_fini(&e);unsigned tracked_gets=gets,tracked_puts=ref_puts;for(unsigned i=0;i<n;i++){assert(objs[i]->refs==1);drm_gem_object_put(objs[i]);}free(objs);assert(frees==n);
 printf("exec_semantics=PASS scenarios=%u objects=15000 tracked_gets=%u tracked_puts=%u contention_prelocked=PASS errors=PASS default_owned=PASS retire_at_unlock=PASS final_objects=0\n",checks,tracked_gets,tracked_puts);
 return argc>1 && !strcmp(argv[1],"--require-borrowed") && (tracked_gets || tracked_puts);
}
'''

vm_source=(K/'drivers/gpu/drm/msm/msm_gem_vma.c').read_text()
for label in ['BASELINE','MODIFIED','ROLLBACK']:
    g=(D/'BASELINE.c') if label=='BASELINE' else (D/'MODIFIED_FILE.c') if label=='MODIFIED' else restore/'msm_gem.c'
    s=g.read_text()
    prot=function(s,'msm_gem_prot').replace('{\n','{\n\tprot_calls++;\n',1)
    mapfn=function(vm_source,'msm_gem_vma_map').replace('{\n','{\n\tmapchecks++;\n',1)
    text=map_prefix+prot+function(s,'msm_gem_get_pages_locked')+mapfn+function(s,'msm_gem_pin_vma_locked')+map_main
    c=D/(label+'-v4-map.c');exe=D/(label+'-v4-map');c.write_text(text)
    execute(label+'-map-compile',['clang','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)])
    execute(label+'-map',[str(exe),'--require-fast'],0 if label=='MODIFIED' else 1)
    e=(D/'BASELINE.exec.c') if label=='BASELINE' else (D/'MODIFIED_FILE.exec.c') if label=='MODIFIED' else restore/'drm_exec.c'
    h=(D/'BASELINE.exec.h') if label=='BASELINE' else (D/'MODIFIED_FILE.exec.h') if label=='MODIFIED' else restore/'drm_exec.h'
    strip=lambda t:re.sub(r'^#include[^\n]*\n','',t,flags=re.M)
    text=exec_prefix+strip(h.read_text())+'\n#ifndef DRM_EXEC_OBJECTS_REF_HELD\n#define DRM_EXEC_OBJECTS_REF_HELD BIT(2)\n#endif\n'+strip(e.read_text())+exec_main
    c=D/(label+'-v4-exec.c');exe=D/(label+'-v4-exec');c.write_text(text)
    execute(label+'-exec-compile',['clang','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)])
    execute(label+'-exec',[str(exe),'--require-borrowed'],0 if label=='MODIFIED' else 1)

# Reconstruct every changed role from the pristine restored copy, byte-exact.
execute('reapply-full-patch',['patch','-d',str(restore),'-p1','-i',str(D/'DIFF_FILE.patch')])
for part,role,name in roles:
    target=D/('MODIFIED_FILE.'+part+'c') if part else D/'MODIFIED_FILE.c'
    assert (restore/name).read_bytes()==target.read_bytes()
    target.write_bytes((restore/name).read_bytes())
assert (restore/'drm_exec.h').read_bytes()==(D/'MODIFIED_FILE.exec.h').read_bytes()
print('MAPPING_EXEC_AND_EXACT_ROLLBACK_REAPPLY_PASS')
