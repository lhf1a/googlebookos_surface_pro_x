# SPDX-License-Identifier: GPL-2.0-only
# Extract actual source; primitives are mocked, not a full-kernel proof.
from pathlib import Path
import subprocess,json,hashlib,difflib
import os
D=Path(os.environ["GPU_SUBMIT_TEST_DIR"])
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct list_head { struct list_head *next,*prev; };
static void init(struct list_head*h){h->next=h->prev=h;}
static void add(struct list_head*n,struct list_head*h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void del(struct list_head*n){n->prev->next=n->next;n->next->prev=n->prev;init(n);}
static bool list_empty(const struct list_head*h){return h->next==h;}
static bool list_is_singular(const struct list_head*h){return h->next!=h&&h->next==h->prev;}
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
struct drm_gpuvm { int id; };
struct drm_gpuva { struct drm_gpuvm *vm; struct {struct list_head entry;} gem; uint64_t address; };
struct drm_gpuvm_bo {struct drm_gpuvm*vm;struct {struct list_head gpuva;struct{struct list_head gem;}entry;}list;};
struct drm_gem_object {struct{struct list_head list;}gpuva;bool locked;};
static unsigned warnings;static uint64_t groups,va_iterations,checks;
#define GEM_WARN_ON(x) ((x)?(++warnings,1):0)
#define msm_gem_assert_locked(o) assert((o)->locked)
#define drm_gem_for_each_gpuvm_bo(pos,o) for(struct list_head*iter=(o)->gpuva.list.next;iter!=&(o)->gpuva.list&&((pos=container_of(iter,struct drm_gpuvm_bo,list.entry.gem)),++groups,1);iter=iter->next)
#define drm_gpuvm_bo_for_each_va(pos,b) for(struct list_head*viter=(b)->list.gpuva.next;viter!=&(b)->list.gpuva&&((pos=container_of(viter,struct drm_gpuva,gem.entry)),++va_iterations,1);viter=viter->next)
'''
post=r'''
static void check(struct drm_gem_object*o,struct drm_gpuvm*vm){
 struct drm_gpuva*expected=NULL;unsigned expected_warnings=0;
 for(struct list_head*g=o->gpuva.list.next;g!=&o->gpuva.list;g=g->next){struct drm_gpuvm_bo*b=container_of(g,struct drm_gpuvm_bo,list.entry.gem);
 for(struct list_head*v=b->list.gpuva.next;v!=&b->list.gpuva;v=v->next){struct drm_gpuva*a=container_of(v,struct drm_gpuva,gem.entry);assert(a->vm==b->vm);if(a->vm==vm){expected=a;expected_warnings=!list_is_singular(&b->list.gpuva);break;}}
 if(expected)break;}
 warnings=0;struct drm_gpuva*actual=lookup_vma(o,vm);assert(actual==expected&&warnings==expected_warnings);checks++;
}
int main(int argc,char**argv){
 enum{NV=16,NA=96};struct drm_gpuvm vm[NV+1];struct drm_gpuvm_bo b[NV];struct drm_gpuva a[NA];bool linked[NA]={0};
 struct drm_gem_object o={.locked=true};init(&o.gpuva.list);for(unsigned i=0;i<=NV;i++)vm[i].id=i;check(&o,&vm[0]);
 for(unsigned i=0;i<NV;i++){b[i].vm=&vm[i];init(&b[i].list.gpuva);init(&b[i].list.entry.gem);add(&b[i].list.entry.gem,&o.gpuva.list);}for(unsigned i=0;i<NA;i++)init(&a[i].gem.entry);
 // Empty retained VM groups, absent VM, single mapping, multi-mapping warning.
 for(unsigned i=0;i<=NV;i++)check(&o,&vm[i]);
 for(unsigned i=0;i<NV;i++){a[i].vm=&vm[i];a[i].address=i*4096;add(&a[i].gem.entry,&b[i].list.gpuva);linked[i]=true;}
 for(unsigned i=0;i<=NV;i++)check(&o,&vm[i]);
 a[NV].vm=&vm[0];add(&a[NV].gem.entry,&b[0].list.gpuva);linked[NV]=true;check(&o,&vm[0]);
 // Remove/rebind/split-like multi-VA/reorder transitions, always under the existing lock.
 unsigned seed=0x516d;for(unsigned n=0;n<200000;n++){unsigned k=rand_r(&seed)%NA;if(linked[k]){del(&a[k].gem.entry);linked[k]=false;}if(rand_r(&seed)%4){unsigned v=rand_r(&seed)%NV;a[k].vm=&vm[v];a[k].address=(uint64_t)n*4096;add(&a[k].gem.entry,&b[v].list.gpuva);linked[k]=true;}check(&o,&vm[rand_r(&seed)%(NV+1)]);}
 for(unsigned i=0;i<NA;i++)if(linked[i])del(&a[i].gem.entry);
 for(unsigned i=0;i<=NV;i++)check(&o,&vm[i]);
 // Heap lifetime churn: no stale VA pointer can survive a close/recreate.
 for(unsigned n=0;n<10000;n++){struct drm_gpuva*v=calloc(1,sizeof(*v));assert(v);v->vm=&vm[0];init(&v->gem.entry);add(&v->gem.entry,&b[0].list.gpuva);check(&o,&vm[0]);del(&v->gem.entry);free(v);check(&o,&vm[0]);}
 printf("lookup_semantics=PASS checks=%"PRIu64" group_iterations=%"PRIu64" va_iterations=%"PRIu64" close_recreate=10000 matching_warning_behavior=PASS\n",checks,groups,va_iterations);
 return argc>1&&!strcmp(argv[1],"--require-group-only")&&va_iterations?1:0;
}
'''.replace('#include <stdint.h>','#include <stdint.h>')
pre=pre.replace('#include <stdint.h>','#include <stdint.h>\n#include <inttypes.h>')
def extract(s):
 start=s.index('static struct drm_gpuva *lookup_vma(');a=s.index('{',start);level=0
 for i in range(a,len(s)):
  if s[i]=='{':level+=1
  elif s[i]=='}':
   level-=1
   if not level:return s[start:i+1]
 raise ValueError('function not closed')
ledger=[]
def run(label,cmd):
 q=subprocess.run(cmd,capture_output=True,text=True);r=dict(label=label,command=cmd,stdout=q.stdout,stderr=q.stderr,exit=q.returncode);ledger.append(r);print(label,'EXIT',q.returncode,q.stdout,q.stderr,flush=True);return q
candidate=(D/'MODIFIED_FILE.c').read_bytes();restored=D/'vma-restored.c';restored.write_bytes(candidate)
assert run('rollback-source',['sh',str(D/'ROLLBACK.sh'),str(restored)]).returncode==0
assert restored.read_bytes()==(D/'BASELINE.c').read_bytes()
for label,source,expected in [('BASELINE',D/'BASELINE.c',1),('MODIFIED',D/'MODIFIED_FILE.c',0),('ROLLBACK',restored,1)]:
 f=D/(label+'-vma.harness.c');f.write_text(pre+extract(source.read_text())+post);exe=D/(label+'-vma')
 assert run(label+'-vma-compile',['clang','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(f),'-o',str(exe)]).returncode==0
 assert run(label+'-vma',[str(exe),'--require-group-only']).returncode==expected
(D/'vma-test-results.json').write_text(json.dumps(ledger,indent=2)+'\n')
with (D/'VERIFICATION.txt').open('a') as f:f.write('\nVMA HOST TESTS\n'+json.dumps(ledger,indent=2)+'\n')
