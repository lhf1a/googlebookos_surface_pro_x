# SPDX-License-Identifier: GPL-2.0-only
# Extract actual source; primitives are mocked, not a full-kernel proof.
from pathlib import Path
import json,re,subprocess,shutil,hashlib
import os
D=Path(os.environ["GPU_SUBMIT_TEST_DIR"])
def function(s,name):
    m=re.search(r'(?m)^(?:static\s+)?(?:int|void)\s+'+name+r'\(',s);assert m,name
    a=s.index('{',m.start());b=a+1;depth=1
    while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[m.start():b]+'\n'
results=[]
def run(label,cmd,want=0):
    p=subprocess.run(cmd,capture_output=True,text=True)
    r=dict(label=label,command=cmd,stdout=p.stdout,stderr=p.stderr,exit=p.returncode);results.append(r)
    (D/'combined-v5-test-results.json').write_text(json.dumps(results,indent=2)+'\n')
    with (D/'VERIFICATION.txt').open('a') as f:f.write('\nV5 ACTUAL-FUNCTION TEST\n'+json.dumps(r,indent=2)+'\n')
    print(label,p.returncode,p.stdout,p.stderr,flush=True);assert p.returncode==want,r
    return r
restored=D/'combined-v5-restored.submit.c';shutil.copyfile(D/'MODIFIED_FILE.submit.c',restored)
run('rollback-submit',['sh',str(D/'ROLLBACK.sh'),str(restored),'submit'])
assert restored.read_bytes()==(D/'BASELINE.submit.c').read_bytes()

prefix=r'''
#include <limits.h>
#include <stddef.h>
#define MSM_SUBMIT_BO_READ 1
#define MSM_SUBMIT_BO_WRITE 2
#define MSM_SUBMIT_BO_NO_IMPLICIT 4
#define DMA_RESV_USAGE_READ 2
#define DMA_RESV_USAGE_WRITE 1
#define DMA_RESV_USAGE_BOOKKEEP 3
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095)
struct ctx {bool vmbind;};struct queue {struct ctx *ctx;};struct dma_fence {int unused;};
struct vm {struct drm_gem_object *r_obj;struct dma_fence *last_fence;};
#define msm_gem_vm vm
#define to_msm_vm(x) (x)
#define to_msm_bo(x) (x)
struct drm_gpuvm_bo {unsigned refs;};
struct drm_gpuva {struct drm_gpuvm_bo *vm_bo;struct {uint64_t addr;} va;};
struct msm_drm_private {struct {int lock;} lru;};struct device {struct msm_drm_private *dev_private;};
struct msm_gem_submit {struct drm_exec exec;struct queue *queue;struct vm *vm;struct device *dev;unsigned nr_bos;bool bos_pinned;int base;struct dma_fence *user_fence;struct bo {struct drm_gem_object *obj;struct drm_gpuvm_bo *vm_bo;uint64_t iova;unsigned flags;} *bos;};
static unsigned hints,dep_calls,attach_calls,map_calls,pin_calls,unpin_calls,vmbind_calls,merge_calls,fence_puts;
static unsigned initial_capacity;static int dep_error_at=-1,map_error_at=-1,pin_error_at=-1;
static struct msm_gem_submit *current;
static struct drm_gpuva *vmas;static struct drm_gpuvm_bo *groups;
static int pos(struct drm_gem_object *o){for(unsigned i=0;i<current->nr_bos;i++)if(current->bos[i].obj==o)return i;assert(0);return -1;}
static void hint(const void *p){assert(p);hints++;}
#define prefetch(x) hint(x)
static bool msm_context_is_vmbind(struct ctx *c){return c->vmbind;}
static struct drm_gem_object *drm_gpuvm_resv_obj(struct vm *v){return v->r_obj;}
static int submit_lock_objects_vmbind(struct msm_gem_submit *s){vmbind_calls++;drm_exec_init(&s->exec,DRM_EXEC_INTERRUPTIBLE_WAIT|DRM_EXEC_IGNORE_DUPLICATES,1);assert(drm_exec_cleanup(&s->exec));int r=drm_exec_lock_obj(&s->exec,s->vm->r_obj);assert(!drm_exec_cleanup(&s->exec));return r;}
static int drm_sched_job_add_implicit_dependencies(int *base,struct drm_gem_object *o,bool write){assert(base==&current->base);int i=pos(o);assert(o->resv->locked);assert(!(current->bos[i].flags&MSM_SUBMIT_BO_NO_IMPLICIT));assert(write==!!(current->bos[i].flags&MSM_SUBMIT_BO_WRITE));dep_calls++;return i==dep_error_at?-ENOMEM:0;}
static void dma_resv_add_fence(struct dma_resv *r,struct dma_fence *f,int usage){assert(f==current->user_fence);int i=pos(r->owner);assert(r->locked);assert(usage==((current->bos[i].flags&MSM_SUBMIT_BO_WRITE)?DMA_RESV_USAGE_WRITE:DMA_RESV_USAGE_READ));attach_calls++;}
static void drm_gpuvm_resv_add_fence(struct vm *v,struct drm_exec *e,struct dma_fence *f,int a,int b){assert(v==current->vm && e==&current->exec && f==current->user_fence);assert(a==DMA_RESV_USAGE_BOOKKEEP && b==a);vmbind_calls++;}
static struct dma_fence *dma_fence_unwrap_merge(struct dma_fence *a,struct dma_fence *b){(void)b;merge_calls++;return a;}
static void dma_fence_put(struct dma_fence *f){(void)f;fence_puts++;}
static void mutex_lock(int *l){assert(!*l);*l=1;}
static void mutex_unlock(int *l){assert(*l);*l=0;}
static struct drm_gpuva *msm_gem_get_vma_locked(struct drm_gem_object *o,struct vm *v){assert(v==current->vm && o->resv->locked);int i=pos(o);map_calls++;return i==map_error_at?ERR_PTR(-ENOMEM):&vmas[i];}
static int msm_gem_pin_vma_locked(struct drm_gem_object *o,struct drm_gpuva *v){assert(o->resv->locked);int i=pos(o);assert(v==&vmas[i]);pin_calls++;return i==pin_error_at?-EIO:0;}
static struct drm_gpuvm_bo *drm_gpuvm_bo_get(struct drm_gpuvm_bo *b){assert(b->refs==1);b->refs++;return b;}
static void msm_gem_pin_obj_locked(struct drm_gem_object *o){assert(current->dev->dev_private->lru.lock);assert(o->resv->locked);o->madv++;}
static void msm_gem_unpin_locked(struct drm_gem_object *o){assert(o->madv==1 && o->resv->locked);o->madv--;unpin_calls++;}
'''
main=r'''
static void dispose(struct msm_gem_submit *s){drm_exec_fini(&s->exec);for(unsigned i=0;i<s->nr_bos;i++){assert(s->bos[i].obj->refs==1);drm_gem_object_put(s->bos[i].obj);}drm_gem_object_put(s->vm->r_obj);free(s->bos);free(vmas);free(groups);}
static unsigned matrix(unsigned n,unsigned pattern,int error){
 reset();hints=dep_calls=attach_calls=map_calls=pin_calls=unpin_calls=vmbind_calls=0;grows=0;
 struct ctx c={0};struct queue q={&c};struct vm vm={object()};struct msm_drm_private priv={0};struct device dev={&priv};struct dma_fence f={0};
 struct msm_gem_submit s={.queue=&q,.vm=&vm,.dev=&dev,.nr_bos=n,.user_fence=&f};current=&s;
 s.bos=calloc(n,sizeof(*s.bos));vmas=calloc(n,sizeof(*vmas));groups=calloc(n,sizeof(*groups));
 for(unsigned i=0;i<n;i++){s.bos[i].obj=object();s.bos[i].flags=1+((i+pattern)%3);if((i+pattern)%4==0)s.bos[i].flags|=MSM_SUBMIT_BO_NO_IMPLICIT;groups[i].refs=1;vmas[i].vm_bo=&groups[i];vmas[i].va.addr=4096*(i+1);}
 assert(!submit_lock_objects(&s));unsigned growth=grows;
 dep_error_at=error>=0?error:-1;unsigned want=0;int wanted=0;
 for(unsigned i=0;i<n;i++)if(!(s.bos[i].flags&MSM_SUBMIT_BO_NO_IMPLICIT)){want++;if((int)i==dep_error_at){wanted=-ENOMEM;break;}}
 assert(submit_fence_sync(&s)==wanted);assert(dep_calls==want);
 submit_attach_object_fences(&s);assert(attach_calls==n);
 map_error_at=error>=0?error:-1;pin_error_at=-1;
 int r=submit_pin_objects(&s);assert(r==(error>=0 && (unsigned)error<n?-ENOMEM:0));assert(s.bos_pinned);
 unsigned done=error>=0 && (unsigned)error<n?(unsigned)error:n;
 assert(map_calls==done+(done<n));assert(pin_calls==done);
 for(unsigned i=0;i<n;i++){assert(groups[i].refs==(i<done?2u:1u));assert(s.bos[i].obj->madv==1);if(i<done){assert(s.bos[i].iova==4096*(i+1));groups[i].refs--;}}
 submit_unpin_objects(&s);assert(!s.bos_pinned && unpin_calls==n);
 // Hint-only lookahead must not bypass the original read/write and error paths.
 unsigned hint_count=hints;dispose(&s);assert(frees==n+1);return (growth<<24)|(hint_count&0xffffff);
}
int main(int argc,char **argv){
 unsigned cases=0;unsigned sizes[]={0,1,7,8,9,17,33};
 for(unsigned z=0;z<sizeof(sizes)/sizeof(sizes[0]);z++)for(unsigned p=0;p<8;p++)for(int er=-1;er<(int)sizes[z];er++){matrix(sizes[z],p,er);cases++;}
 // Distinct pin failure path, with balanced later unpin and mapping refs.
 reset();struct ctx c={0};struct queue q={&c};struct vm vm={object()};struct msm_drm_private priv={0};struct device dev={&priv};struct msm_gem_submit s={.queue=&q,.vm=&vm,.dev=&dev,.nr_bos=2};current=&s;s.bos=calloc(2,sizeof(*s.bos));vmas=calloc(2,sizeof(*vmas));groups=calloc(2,sizeof(*groups));for(unsigned i=0;i<2;i++){s.bos[i].obj=object();groups[i].refs=1;vmas[i].vm_bo=&groups[i];}assert(!submit_lock_objects(&s));map_error_at=-1;pin_error_at=1;assert(submit_pin_objects(&s)==-EIO);assert(groups[0].refs==2 && groups[1].refs==1);groups[0].refs--;submit_unpin_objects(&s);dispose(&s);cases++;
 // Capacity accounting is independent of a particular game or submission size.
 unsigned growth15000=0,hints15000=0;
 unsigned large[]={512,15000,16384};
 for(unsigned z=0;z<3;z++){
  unsigned n=large[z];reset();grows=hints=0;struct ctx c={0};struct queue q={&c};struct vm v={object()};struct msm_gem_submit s={.queue=&q,.vm=&v,.nr_bos=n};s.bos=calloc(n,sizeof(*s.bos));for(unsigned i=0;i<n;i++)s.bos[i].obj=object();current=&s;assert(!submit_lock_objects(&s));assert(s.exec.num_objects==n+1);if(n==15000){growth15000=grows;hints15000=hints;}drm_exec_fini(&s.exec);for(unsigned i=0;i<n;i++)drm_gem_object_put(s.bos[i].obj);drm_gem_object_put(v.r_obj);free(s.bos);assert(frees==n+1);cases++;
 }
#if HAVE_LOOKAHEAD
 // End/tail/UINT_MAX indices never touch descriptor memory.
 struct msm_gem_submit empty={0};assert(!submit_bo_lookahead(&empty,0));assert(!submit_bo_lookahead(&empty,UINT_MAX));
 struct msm_gem_submit edge={.nr_bos=UINT_MAX,.bos=NULL};assert(!submit_bo_lookahead(&edge,UINT_MAX));assert(!submit_bo_lookahead(&edge,UINT_MAX-8));
 struct msm_gem_submit b={.nr_bos=17};b.bos=calloc(17,sizeof(*b.bos));for(unsigned i=0;i<17;i++)b.bos[i].obj=(void *)(uintptr_t)(i+1);for(unsigned i=0;i<17;i++)assert(submit_bo_lookahead(&b,i)==(i+8<17?b.bos[i+8].obj:NULL));assert(!submit_bo_lookahead(&b,17));free(b.bos);
#endif
 printf("submit_walk=PASS scenarios=%u bos=15000 growth_calls=%u reservation_hints=%u fence_modes=PASS no_implicit=PASS error_order=PASS pin_unpin_refs=PASS bounds=PASS\n",cases,growth15000,hints15000);
 return argc>1 && !strcmp(argv[1],"--require-exact-capacity-prefetch") && (growth15000!=0 || hints15000!=29984);
}
'''

for label,src,ownedcore in [('BASELINE',D/'BASELINE.submit.c',False),('PARENT',D/'archive-combined-v4/MODIFIED_FILE.submit.c',True),
                           ('MODIFIED',D/'MODIFIED_FILE.submit.c',True),('ROLLBACK',restored,False)]:
    core=(D/('MODIFIED-v4-exec.c' if ownedcore else 'BASELINE-v4-exec.c')).read_text().split('int main(int argc,char **argv)')[0]
    core=core.replace('struct drm_gem_object {int refs;', 'struct drm_gem_object {int gpuva,lru,madv;int refs;')
    core='static unsigned grows;\n'+core
    old='static void *kvrealloc(void *p,size_t n,int f){(void)f;alloc_calls++;'
    assert old in core;core=core.replace(old,old+'grows++;')
    s=src.read_text();has='submit_bo_lookahead(' in s
    helpers=s[s.index('/* All descriptors have been looked up'):s.index('/* This is where we make sure')] if has else ''
    text=core+prefix+'\n#define HAVE_LOOKAHEAD '+str(int(has))+'\n'+helpers
    for name in ['submit_lock_objects','submit_fence_sync','submit_pin_objects','submit_unpin_objects','submit_attach_object_fences']:text+=function(s,name)
    text+=main
    c=D/(label+'-v5-walk.c');exe=D/(label+'-v5-walk');c.write_text(text)
    run(label+'-compile',['clang','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)])
    run(label+'-walk',[str(exe),'--require-exact-capacity-prefetch'],0 if label=='MODIFIED' else 1)

# Compile the actual capacity arithmetic with boundary inputs, without attempting
# a multi-terabyte allocation which cannot reach this function in a real submit.
s=(D/'MODIFIED_FILE.submit.c').read_text();f=function(s,'submit_lock_objects')
assert 'if (nr_objects != UINT_MAX)\n\t\tnr_objects++;' in f
capacity='''#include <assert.h>\n#include <limits.h>\n#include <stdio.h>\nunsigned capacity(unsigned nr_objects) {\n'''+f[f.index('\tif (nr_objects != UINT_MAX)'):f.index('\tdrm_exec_init')]+'''return nr_objects;}\nint main(void){assert(capacity(0)==1);assert(capacity(15000)==15001);assert(capacity(UINT_MAX-1)==UINT_MAX);assert(capacity(UINT_MAX)==UINT_MAX);puts("capacity_overflow=PASS zero=1 normal=15001 max=saturated");}\n'''
c=D/'v5-capacity.c';c.write_text(capacity);exe=D/'v5-capacity'
run('capacity-compile',['clang','-fsanitize=address,undefined',str(c),'-o',str(exe)])
run('capacity-boundaries',[str(exe)])

# Reconstruction from pristine inputs verifies the complete five-source patch.
rebuild=D/'combined-v5-patch-rebuild';rebuild.mkdir(exist_ok=True)
entries=[('BASELINE.c','MODIFIED_FILE.c','msm_gem.c'),('BASELINE.submit.c','MODIFIED_FILE.submit.c','msm_gem_submit.c'),
         ('BASELINE.scheduler.c','MODIFIED_FILE.scheduler.c','sched_main.c'),('BASELINE.exec.c','MODIFIED_FILE.exec.c','drm_exec.c'),
         ('BASELINE.exec.h','MODIFIED_FILE.exec.h','drm_exec.h')]
for b,m,n in entries:shutil.copyfile(D/b,rebuild/n)
run('reapply-full-goal',['patch','-d',str(rebuild),'-p1','-i',str(D/'DIFF_FILE.patch')])
for b,m,n in entries:assert (rebuild/n).read_bytes()==(D/m).read_bytes()
(D/'MODIFIED_FILE.submit.c').write_bytes((rebuild/'msm_gem_submit.c').read_bytes())
print('V5_ACTUAL_SOURCE_TESTS_AND_EXACT_PATCH_PASS')
