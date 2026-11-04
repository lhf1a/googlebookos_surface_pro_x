/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../probe/video/sq2-venus-system-pm.h"
struct fixture { struct sq2_system_pm pm; int idle, stop, start, ni, ns, nr, recur; };
static int idle(void *p) { struct fixture *f=p;f->ni++;return f->idle; }
static int stop(void *p);
static int start(void *p);
static const struct sq2_system_pm_ops ops={idle,stop,start};
static int stop(void *p)
{
 struct fixture *f=p;f->ns++;
 if(f->recur) assert(sq2_system_pm_suspend(&f->pm,&ops,f)==-EBUSY);
 return f->stop;
}
static int start(void *p)
{
 struct fixture *f=p;f->nr++;
 if(f->recur) assert(sq2_system_pm_resume(&f->pm,&ops,f)==-EBUSY);
 return f->start;
}
int main(void)
{
 struct fixture f={0};int i,n;
 assert(!sq2_system_pm_resume(&f.pm,&ops,&f));assert(!f.nr);
 f.idle=-EBUSY;
 for(i=0;i<10;i++)assert(sq2_system_pm_suspend(&f.pm,&ops,&f)==-EBUSY);
 assert(!f.ns&&!f.nr&&f.pm.phase==SQ2_PM_RUNNING);
 f.idle=0;f.recur=1;
 for(i=0;i<128;i++){
  assert(!sq2_system_pm_suspend(&f.pm,&ops,&f));
  n=f.ns;assert(!sq2_system_pm_suspend(&f.pm,&ops,&f));assert(n==f.ns);
  assert(f.pm.phase==SQ2_PM_OFF);
  assert(!sq2_system_pm_resume(&f.pm,&ops,&f));
  n=f.nr;assert(!sq2_system_pm_resume(&f.pm,&ops,&f));assert(n==f.nr);
 }
 assert(f.pm.stops==128&&f.pm.starts==128);
 memset(&f,0,sizeof(f));f.stop=-EIO;
 assert(sq2_system_pm_suspend(&f.pm,&ops,&f)==-EIO);
 for(i=0;i<10;i++){assert(sq2_system_pm_resume(&f.pm,&ops,&f)==-EIO);assert(sq2_system_pm_suspend(&f.pm,&ops,&f)==-EIO);}
 assert(f.ns==1&&!f.nr);
 memset(&f,0,sizeof(f));assert(!sq2_system_pm_suspend(&f.pm,&ops,&f));f.start=-ETIMEDOUT;
 assert(sq2_system_pm_resume(&f.pm,&ops,&f)==-ETIMEDOUT);
 for(i=0;i<10;i++){assert(sq2_system_pm_resume(&f.pm,&ops,&f)==-ETIMEDOUT);assert(sq2_system_pm_suspend(&f.pm,&ops,&f)==-ETIMEDOUT);}
 assert(f.ns==1&&f.nr==1);
 puts("SQ2_SYSTEM_PM_STATE_PASS: 128 clean cycles, live-session EBUSY, reentry/idempotence, original errors, failed transitions never auto-retried; MOCK callbacks, not hardware proof");
 return 0;
}
