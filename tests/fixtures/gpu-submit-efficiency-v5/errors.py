# SPDX-License-Identifier: GPL-2.0-only
# Extract actual source; primitives are mocked, not a full-kernel proof.
from pathlib import Path
import subprocess,json
import os
D=Path(os.environ["GPU_SUBMIT_TEST_DIR"])
main=r'''
int main(void){
 checks=0;cases(0);cases(DRM_EXEC_INTERRUPTIBLE_WAIT);cases(DRM_EXEC_OBJECTS_REF_HELD);cases(DRM_EXEC_INTERRUPTIBLE_WAIT|DRM_EXEC_OBJECTS_REF_HELD);
 unsigned scenarios=0;
 for(int kind=0;kind<9;kind++){
  reset();grows=hints=0;struct ctx c={.vmbind=kind==7};struct queue q={&c};struct vm v={object()};
  unsigned n=c.vmbind?0:17;struct msm_gem_submit s={.queue=&q,.vm=&v,.nr_bos=n};current=&s;s.bos=calloc(n,sizeof(*s.bos));
  for(unsigned i=0;i<n;i++)s.bos[i].obj=object();
  if(kind==1){s.bos[9].obj->resv->fail=-EINTR;s.bos[9].obj->resv->fail_count=1;}
  if(kind==2)s.bos[9].obj->resv->reserve_error=-ENOMEM;
  if(kind==3 || kind==6){s.bos[9].obj->resv->fail=-EDEADLK;s.bos[9].obj->resv->fail_count=1;}
  if(kind==4){drm_gem_object_put(s.bos[16].obj);s.bos[16].obj=s.bos[0].obj;drm_gem_object_get(s.bos[0].obj);}
  if(kind==5)alloc_fail_at=1;
  if(kind==6)slow_error=-EINTR;
  int ret=submit_lock_objects(&s);int expected=kind==1||kind==6?-EINTR:kind==2?-ENOMEM:kind==4?-EALREADY:0;
  assert(ret==expected);
  if(kind==3)assert(slow_calls==1);
  if(c.vmbind)assert(!(s.exec.flags&DRM_EXEC_OBJECTS_REF_HELD));
  if(kind==8)for(unsigned i=0;i<n;i++)s.bos[i].obj->retire_on_unlock=true;
  drm_exec_fini(&s.exec);
  if(kind!=8)for(unsigned i=0;i<n;i++){assert(!s.bos[i].obj->resv->locked);drm_gem_object_put(s.bos[i].obj);}
  assert(v.r_obj->refs==1 && !v.r_obj->resv->locked);drm_gem_object_put(v.r_obj);free(s.bos);assert(frees==n+1);scenarios++;
 }
 printf("submit_errors=PASS scenarios=%u exec_reference_scenarios=%u contention_retry=PASS interrupted_slow=PASS init_alloc_recovery=PASS duplicate=PASS vmbind_default=PASS retire_at_unlock=PASS\n",scenarios,checks);
 return 0;
}
'''
results=[]
for label in ['BASELINE','PARENT','MODIFIED','ROLLBACK']:
    s=(D/(label+'-v5-walk.c')).read_text().split('int main(int argc,char **argv)')[0]+main
    c=D/(label+'-v5-errors.c');exe=D/(label+'-v5-errors');c.write_text(s)
    for tag,cmd in [('compile',['clang','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)]),('run',[str(exe)])]:
        p=subprocess.run(cmd,capture_output=True,text=True);r=dict(label=label+'-'+tag,command=cmd,stdout=p.stdout,stderr=p.stderr,exit=p.returncode);results.append(r)
        (D/'combined-v5-error-results.json').write_text(json.dumps(results,indent=2)+'\n')
        with (D/'VERIFICATION.txt').open('a') as f:f.write('\nV5 ERROR/LIFETIME SUPPLEMENT\n'+json.dumps(r,indent=2)+'\n')
        print(label,tag,p.returncode,p.stdout,p.stderr,flush=True);assert p.returncode==0
