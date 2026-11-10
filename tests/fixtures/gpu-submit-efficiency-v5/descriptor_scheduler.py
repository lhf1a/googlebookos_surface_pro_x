# SPDX-License-Identifier: GPL-2.0-only
# Extract actual source; primitives are mocked, not a full-kernel proof.
from pathlib import Path
import subprocess,json,re,sys
import os
D=Path(os.environ["GPU_SUBMIT_TEST_DIR"])
common='#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <errno.h>\n'
submit_pre=common+r'''
#define __user
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define u64_to_user_ptr(x) ((void*)(uintptr_t)(x))
#define MSM_SUBMIT_BO_READ 1
#define MSM_SUBMIT_BO_WRITE 2
#define MSM_SUBMIT_BO_FLAGS 15
#define SUBMIT_ERROR(e,s,...) (-(e))
struct drm_msm_gem_submit_bo {uint32_t flags,handle;uint64_t presumed;};
struct drm_msm_gem_submit {uint32_t nr_bos;uint64_t bos;};
struct drm_gem_object {unsigned refs;};
struct bo {unsigned flags,handle;struct drm_gem_object*obj;};
struct msm_gem_submit {struct bo*bos;unsigned nr_bos;};
struct drm_file {int table_lock,object_idr;};
static struct drm_msm_gem_submit_bo *input;
static struct drm_gem_object *objs;
static size_t bytes,calls;static unsigned badhandle,nobjs;
static size_t copy_from_user(void*d,const void*s,size_t n){calls++;size_t off=(const char*)s-(char*)input;size_t available=off<bytes?bytes-off:0;size_t copied=available<n?available:n;if(copied)memcpy(d,s,copied);memset((char*)d+copied,0,n-copied);return n-copied;}
static void spin_lock(int*l){assert(!*l);*l=1;}
static void spin_unlock(int*l){assert(*l);*l=0;}
static struct drm_gem_object*idr_find(int*idr,unsigned h){(void)idr;if(!h||h>nobjs||h==badhandle)return NULL;return &objs[h-1];}
static void drm_gem_object_get(struct drm_gem_object*o){o->refs++;}
'''
submit_post=r'''
static unsigned cases;
static size_t test(unsigned n,size_t accessible,int badflag,unsigned missing){
 input=calloc(n?n:1,sizeof(*input));objs=calloc(n?n:1,sizeof(*objs));struct bo*bos=calloc(n?n:1,sizeof(*bos));assert(input&&objs&&bos);nobjs=n;badhandle=missing;bytes=accessible;calls=0;
 for(unsigned i=0;i<n;i++){input[i].handle=i+1;input[i].flags=(i%3)+1;}if(badflag>=0)input[badflag].flags=0;
 int expected=0;unsigned owned=n;for(unsigned i=0;i<n;i++){if((i+1)*sizeof(*input)>accessible){expected=-EFAULT;owned=0;break;}if((int)i==badflag){expected=-EINVAL;owned=0;break;}}
 if(!expected&&missing&&missing<=n){expected=-EINVAL;owned=missing-1;}
 struct drm_file f={0};struct msm_gem_submit s={.bos=bos};struct drm_msm_gem_submit a={.nr_bos=n,.bos=(uintptr_t)input};int ret=submit_lookup_objects(&s,&a,&f);assert(ret==expected&&s.nr_bos==owned&&!f.table_lock);
 for(unsigned i=0;i<n;i++){assert(objs[i].refs==(unsigned)(i<owned));if(i<owned){assert(bos[i].obj==&objs[i]&&bos[i].handle==i+1&&bos[i].flags==input[i].flags);objs[i].refs--;}}
 size_t result=calls;free(input);free(objs);free(bos);cases++;return result;
}
int main(int argc,char**argv){unsigned sizes[]={0,1,2,31,32,33,63,64,65,127};for(unsigned k=0;k<ARRAY_SIZE(sizes);k++){unsigned n=sizes[k];test(n,n*16,-1,0);for(unsigned i=0;i<n;i++){test(n,n*16,i,0);test(n,n*16,-1,i+1);for(unsigned off=0;off<16;off+=7){test(n,i*16+off,-1,0);if(i)test(n,i*16+off,i-1,0);}}}
 size_t successful_calls=test(15000,15000*16,-1,0);printf("submit_input=PASS cases=%u descriptors=15000 copy_calls=%zu fault_fallback_order=PASS lookup_refs=PASS\n",cases,successful_calls);return argc>1&&!strcmp(argv[1],"--require-batched")&&successful_calls>469?1:0;}
'''
fence_pre=common+r'''
typedef uint32_t u32;
#define GFP_KERNEL 0
#define xa_limit_32b 0
struct dma_fence {uint64_t context,seqno;bool seq64;int refs;};
struct xarray {struct dma_fence*f[16];};
struct drm_sched_job {struct xarray dependencies;};
static unsigned gets,put_count;static bool alloc_fail;
static struct dma_fence*dma_fence_get(struct dma_fence*f){if(f){assert(f->refs>0);f->refs++;gets++;}return f;}
static void dma_fence_put(struct dma_fence*f){if(f){assert(f->refs>0);f->refs--;put_count++;}}
static bool dma_fence_is_later(struct dma_fence*a,struct dma_fence*b){assert(a->context==b->context);return a->seq64?(int64_t)(a->seqno-b->seqno)>0:(int32_t)((uint32_t)a->seqno-(uint32_t)b->seqno)>0;}
#define xa_for_each(x,i,e) for(i=0;i<16;i++) if((e=(x)->f[i]))
static void*xa_store(struct xarray*x,unsigned i,struct dma_fence*f,int g){(void)g;assert(i<16&&x->f[i]);void*old=x->f[i];x->f[i]=f;return old;}
static int xa_alloc(struct xarray*x,u32*i,struct dma_fence*f,int l,int g){(void)l;(void)g;if(alloc_fail)return -ENOMEM;for(*i=0;*i<16;(*i)++)if(!x->f[*i]){x->f[*i]=f;return 0;}return -ENOMEM;}
enum dma_resv_usage {USAGE_KERNEL,USAGE_WRITE,USAGE_READ,USAGE_BOOKKEEP};
struct dma_resv {bool locked;unsigned n;struct dma_fence*f[16];enum dma_resv_usage usage[16];};
struct dma_resv_iter{unsigned i;};
#define dma_resv_assert_held(r) assert((r)->locked)
#define dma_resv_for_each_fence(c,r,u,fence) for((c)->i=0;(c)->i<(r)->n;(c)->i++) if((r)->usage[(c)->i]<=u && ((fence)=(r)->f[(c)->i]))
'''
fence_post=r'''
static void clear(struct drm_sched_job*j){for(unsigned i=0;i<16;i++)if(j->dependencies.f[i]){dma_fence_put(j->dependencies.f[i]);j->dependencies.f[i]=NULL;}}
static int borrowed(struct drm_sched_job*j,struct dma_fence*f){struct dma_resv r={.locked=true,.n=1,.f={f},.usage={USAGE_WRITE}};return drm_sched_job_add_resv_dependencies(j,&r,USAGE_READ);}
int main(int argc,char**argv){
 struct dma_fence a={.context=1,.seqno=0xfffffffe,.refs=1},b={.context=1,.seqno=1,.refs=1},c={.context=2,.seqno=9,.seq64=true,.refs=1},d={.context=2,.seqno=10,.seq64=true,.refs=1};struct drm_sched_job j={0};
 assert(!drm_sched_job_add_dependency(&j,NULL));assert(!borrowed(&j,&a));assert(a.refs==2);assert(!borrowed(&j,&a)&&a.refs==2);assert(!borrowed(&j,&b)&&a.refs==1&&b.refs==2);assert(!borrowed(&j,&a)&&a.refs==1);assert(!borrowed(&j,&c));assert(!borrowed(&j,&d)&&c.refs==1&&d.refs==2);
 assert(!drm_sched_job_add_dependency(&j,dma_fence_get(&b))&&b.refs==2);clear(&j);assert(a.refs==1&&b.refs==1&&c.refs==1&&d.refs==1);
 alloc_fail=true;assert(borrowed(&j,&a)==-ENOMEM&&a.refs==1);assert(drm_sched_job_add_dependency(&j,dma_fence_get(&a))==-ENOMEM&&a.refs==1);alloc_fail=false;
 struct dma_resv r={.locked=true,.n=2,.f={&a,&c},.usage={USAGE_READ,USAGE_WRITE}};assert(!drm_sched_job_add_resv_dependencies(&j,&r,USAGE_WRITE));assert(a.refs==1&&c.refs==2);clear(&j);
 // Existing dependencies remain owned if a new context fails allocation.
 assert(!borrowed(&j,&a));alloc_fail=true;assert(borrowed(&j,&c)==-ENOMEM&&a.refs==2&&c.refs==1);alloc_fail=false;clear(&j);
 gets=put_count=0;for(unsigned i=0;i<15000;i++)assert(!borrowed(&j,&a));assert(a.refs==2);unsigned measured_gets=gets,measured_puts=put_count;clear(&j);assert(a.refs==1);
 printf("fence_refs=PASS borrowed_duplicates=15000 gets=%u puts_before_cleanup=%u cross_context=PASS sequence_wrap=PASS public_owned=PASS allocation_failure=PASS usage_filter=PASS final_refs=1\n",measured_gets,measured_puts);
 return argc>1&&!strcmp(argv[1],"--require-borrowed-dedup")&&(measured_gets!=1||measured_puts!=0)?1:0;
}
'''
def extract(s,name):
 pat=re.search(r'^(?:static )?(?:int|void) '+name+r'\(',s,re.M);assert pat,name
 start=pat.start();begin=s.index('{',start);level=0
 for i in range(begin,len(s)):
  if s[i]=='{':level+=1
  if s[i]=='}':
   level-=1
   if not level:return s[start:i+1]+'\n'
ledger=[]
def run(label,cmd):
 q=subprocess.run(cmd,capture_output=True,text=True);r=dict(label=label,command=cmd,stdout=q.stdout,stderr=q.stderr,exit=q.returncode);ledger.append(r);(D/'combined-test-results.json').write_text(json.dumps(ledger,indent=2));print(label,q.returncode,q.stdout,q.stderr,flush=True);return q.returncode
for role,pre,post,gate in [('submit',submit_pre,submit_post,'--require-batched'),('scheduler',fence_pre,fence_post,'--require-borrowed-dedup')]:
 if len(sys.argv)>1 and role!=sys.argv[1]:continue
 restored=D/('combined-restored.'+role+'.c');restored.write_bytes((D/('MODIFIED_FILE.'+role+'.c')).read_bytes());assert run('rollback-'+role,['sh',str(D/'ROLLBACK.sh'),str(restored),role])==0;assert restored.read_bytes()==(D/('BASELINE.'+role+'.c')).read_bytes()
 for label,file,expected in [('BASELINE',D/('BASELINE.'+role+'.c'),1),('MODIFIED',D/('MODIFIED_FILE.'+role+'.c'),0),('ROLLBACK',restored,1)]:
  s=file.read_text();names=['submit_lookup_objects'] if role=='submit' else (['drm_sched_job_add_dependency_ref'] if 'static int drm_sched_job_add_dependency_ref(' in s else [])+['drm_sched_job_add_dependency','drm_sched_job_add_resv_dependencies'];h=D/(label+'-combined-'+role+'.c');h.write_text(pre+'\n'.join(extract(s,n) for n in names)+post);exe=h.with_suffix('')
  assert run(label+'-'+role+'-compile',['clang','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(h),'-o',str(exe)])==0
  assert run(label+'-'+role,[str(exe),gate])==expected
(D/'combined-test-results.json').write_text(json.dumps(ledger,indent=2))
with (D/'VERIFICATION.txt').open('a') as f:f.write('\nCOMBINED ACTUAL-FUNCTION TESTS\n'+json.dumps(ledger,indent=2)+'\n')
