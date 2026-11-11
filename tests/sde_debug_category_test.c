/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual macro/helper bodies; printk, descriptor/static-key leaves modeled. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define BIT(x) (1UL << (x))
#define unlikely(x) (x)
#define DRM_NAME "drm"
#define KERN_INFO "I"
#define KERN_DEBUG "D"
#define KERN_ERR "E"
#define pr_fmt(x) x
#define __UNIQUE_ID(x) model_descriptor
#define _DPRINTK_CLASS_DFLT 99
static unsigned long __drm_debug;
#ifdef TEST_LEGACY_ALIAS
#define drm_debug __drm_debug
#endif
struct device {int unused;};
struct _ddebug {int category;};
struct va_format {const char *fmt;va_list *va;};
struct task {int pid;};
static struct task task={42};
#define current (&task)
static int pr_enabled, core_enabled, evals, events, instrumented;
static char route[32], text[256];
#define DEFINE_DYNAMIC_DEBUG_METADATA_CLS(id,cls,fmt) struct _ddebug id = {cls}
#define DYNAMIC_DEBUG_BRANCH(id) ((id).category == _DPRINTK_CLASS_DFLT ? pr_enabled : core_enabled)
#define dynamic_pr_debug(fmt,...) _dynamic_func_call_cls(_DPRINTK_CLASS_DFLT,fmt,model_dynamic_pr,fmt,##__VA_ARGS__)
static void record(const char *r,const char *fmt,va_list args)
{
    if (!strncmp(fmt,"todo:",5)) {instrumented++;return;}
    events++;snprintf(route,sizeof(route),"%s",r);vsnprintf(text,sizeof(text),fmt,args);
}
#if PRINT_MODE == 2
static void __attribute__((unused)) model_dynamic_pr(struct _ddebug *desc,const char *fmt,...)
{va_list args;va_start(args,fmt);record("pr_debug",fmt,args);va_end(args);}
#endif
static int printk(const char *fmt,...)
{
    va_list args;va_start(args,fmt);
    record(fmt[0]=='D'?"pr_debug":!strncmp(fmt+1,"[drm]",5)?"drm_info":"pr_info",fmt+1,args);
    va_end(args);return 0;
}
#define _printk printk
#define pr_info(fmt,...) printk(KERN_INFO fmt,##__VA_ARGS__)
static void __drm_dev_vprintk(const struct device *dev,const char *level,void *origin,
                            const char *prefix,struct va_format *vaf)
{record(level[0]=='E'?"drm_error":"drm_debug",vaf->fmt,*vaf->va);}
#include "actual-kernel.h"
#include "actual-product.h"
static void invoke_macro(int which)
{
    switch(which) {
    case 0:SDE_DEBUG("v=%d",++evals);break;
    case 1:SDE_INFO("v=%d",++evals);break;
    case 2:SDE_DEBUG_DRIVER("v=%d",++evals);break;
    case 3:DP_DEBUG("v=%d",++evals);break;
    case 4:DP_INFO("v=%d",++evals);break;
    }
}
int main(void)
{
    const unsigned long masks[]={0,BIT(DRM_UT_CORE),BIT(DRM_UT_DRIVER),BIT(DRM_UT_KMS),
        BIT(DRM_UT_CORE)|BIT(DRM_UT_KMS),BIT(DRM_UT_CORE)|BIT(DRM_UT_DRIVER),
        BIT(DRM_UT_DRIVER)|BIT(DRM_UT_KMS),7,BIT(DRM_UT_ATOMIC),BIT(DRM_UT_DP),1023};
    int cases=0,failed=0;
    for(size_t m=0;m<sizeof(masks)/sizeof(masks[0]);m++)
    for(int which=0;which<5;which++)for(int p=0;p<2;p++)for(int c=0;c<2;c++) {
        __drm_debug=masks[m];pr_enabled=p;core_enabled=c;evals=events=instrumented=0;
        route[0]=text[0]=0;cases++;invoke_macro(which);
        bool enabled=(masks[m]&BIT(which==2?DRM_UT_DRIVER:DRM_UT_KMS))!=0;
        bool is_info=(which==1||which==4), is_driver=(which==2);
        bool pr_on=PRINT_MODE==1||(PRINT_MODE==2&&p);
        int want_eval=0,want_events=0,want_instrument=0;const char *want_route="";
#ifdef CONFIG_DRM_USE_DYNAMIC_DEBUG
        bool core_on=c;
        want_instrument=pr_on?1:0;
#else
        bool core_on=(masks[m]&BIT(DRM_UT_CORE))!=0;
#endif
        if(is_info){want_eval=want_events=1;want_route=enabled?"drm_info":"pr_info";}
        else if(enabled&&is_driver){want_eval=want_events=1;want_route="drm_error";}
        else if(enabled){want_events=core_on;want_route=core_on?"drm_debug":"";
#ifdef CONFIG_DRM_USE_DYNAMIC_DEBUG
            want_eval=core_on;
#else
            want_eval=1;
#endif
        }else{want_eval=want_events=pr_on;want_route=pr_on?"pr_debug":"";}
        char want_text[256]="";
        if(want_events){
            if(which>=3)snprintf(want_text,sizeof(want_text),enabled?"[msm-dp-%s][%-4d]v=1":"[drm:invoke_macro][msm-dp-%s][%-4d]v=1",is_info?"info":"debug",42);
            else snprintf(want_text,sizeof(want_text),"v=1");
            if(is_info&&enabled){char tmp[256];snprintf(tmp,sizeof(tmp),"[drm] %.240s",want_text);strcpy(want_text,tmp);}
        }
        if(evals!=want_eval||events!=want_events||strcmp(route,want_route)||strcmp(text,want_text)||instrumented!=want_instrument){
            if(failed<6)fprintf(stderr,"FAIL mask=%lx macro=%d pr=%d corekey=%d eval=%d/%d events=%d/%d route=%s/%s instrument=%d/%d text=%s/%s\n",masks[m],which,p,c,evals,want_eval,events,want_events,route,want_route,instrumented,want_instrument,text,want_text);
            failed++;
        }
    }
    printf("RESULT %d scenarios %d failures PRINT_MODE=%d dynamic=%d\n",cases,failed,PRINT_MODE,
#ifdef CONFIG_DRM_USE_DYNAMIC_DEBUG
    1
#else
    0
#endif
    );return failed?1:0;
}
