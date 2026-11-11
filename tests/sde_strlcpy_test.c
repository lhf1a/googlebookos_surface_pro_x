/* SPDX-License-Identifier: GPL-2.0-only */
/* Exact string/driver bodies; allocation, locks, word load and parser leaves modeled. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
/* libc defines __BIG_ENDIAN as a numeric constant even on LE hosts; the
 * kernel word helper instead uses presence of that architecture macro. */
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error This host fixture currently models the little-endian word helper only
#endif
#undef __BIG_ENDIAN
#define __must_be_cstr(x) 0
#define IS_ENABLED(x) (x)
#define CONFIG_64BIT 1
#define PAGE_SIZE 4096
#define REPEAT_BYTE(x) (~0UL / 0xff * (x))
#define ALLBUTLAST_BYTE_MASK (~0UL >> 8)
#define unlikely(x) (x)
static int warnings,scenarios,fail_alloc,allocations;
#define WARN_ON_ONCE(x) ((x)?(warnings++,1):0)
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL scenario=%d line=%d: %s\n",scenarios,__LINE__,#x);exit(1);}}while(0)
#define min_t(type,a,b) ({type _a=(a),_b=(b);_a<_b?_a:_b;})
static unsigned long read_word_at_a_time(const void *p){unsigned long v;memcpy(&v,p,sizeof(v));return v;}
#include "actual-word-helpers.h"
#include "actual-kernel-string.h"
#ifndef NATIVE_API
#include "actual-legacy-string.h"
#endif
#include "actual-definitions.h"
#ifdef NATIVE_API
/* This is intentionally a host MODEL gate, not a kernel-header compile.
 * glibc 2.38+ can declare/export strlcpy, so merely omitting the historical
 * implementation does not test the missing Linux 6.18 API. */
#pragma GCC poison strlcpy
#endif
struct list_head{struct list_head *next,*prev;};
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define INIT_LIST_HEAD(h) ((h)->next=(h),(h)->prev=(h))
static void add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
struct sde_evtlog_filter{struct list_head list;char filter[SDE_EVTLOG_FILTER_STRSIZE];};
struct sde_dbg_evtlog{int spin_lock;struct list_head filter_list;};
#define spin_lock_irqsave(l,f) do{CHECK(!*(l));*(l)=1;(f)=0;}while(0)
#define spin_unlock_irqrestore(l,f) do{CHECK(*(l));*(l)=0;(void)(f);}while(0)
struct device{int unused;};
struct dsi_parser_prop{char *raw,*name,*value;char **items;int len,type;};
#define GFP_KERNEL 0
#define DSI_PROP_TYPE_INT_SET_ARRAY 1
#define DSI_PROP_TYPE_INT_SET 2
#define DSI_PROP_TYPE_INT_ARRAY 3
#define DSI_DEBUG(...) ((void)0)
static void *devm_kzalloc(struct device *dev,size_t n,int flags){allocations++;return fail_alloc==allocations?NULL:calloc(1,n?n:1);}
static char *dsi_parser_strim(char *p){return p;}
static int dsi_parser_get_strings(struct device *d,struct dsi_parser_prop *p,char *b){return 1;}
static int dsi_parser_get_tuples(struct device *d,struct dsi_parser_prop *p,char *b){return 0;}
static void dsi_parser_get_int_value(struct dsi_parser_prop *p,int b){}
static char *dsi_parser_get_data(char *a,char *b,char *c){return NULL;}
static int dsi_parser_get_tuples_data(struct dsi_parser_prop *p,char *s){return 0;}
struct dp_parser{void (*clear_io_buf)(struct dp_parser*);};
struct dp_sub{void (*read)(void),(*write)(void);};
struct dp_catalog{struct dp_sub *sub;};
struct dp_catalog_private{struct dp_catalog dp_catalog;struct dp_parser *parser;void (*read)(void),(*write)(void);char exe_mode[SZ_4];};
static int cleared,got;
static void clear_io(struct dp_parser *p){cleared++;}
static void dp_catalog_get_io_buf(struct dp_catalog_private *p){got++;}
static void dp_read_hw(void){}static void dp_write_hw(void){}static void dp_read_sw(void){}static void dp_write_sw(void){}
static void dp_read_sub_hw(void){}static void dp_write_sub_hw(void){}static void dp_read_sub_sw(void){}static void dp_write_sub_sw(void){}
#define DP_ERR(...) ((void)0)
#include "actual-driver-functions.h"
static void api_boundaries(void)
{
    const size_t caps[]={0,1,2,4,8,24,32,40,64,80,128,256,512};
    const size_t lengths[]={0,1,2,3,7,8,23,31,63,127,255,511,700};
    for(size_t c=0;c<sizeof(caps)/sizeof(*caps);c++)for(size_t n=0;n<sizeof(lengths)/sizeof(*lengths);n++){
        scenarios++;size_t cap=caps[c],len=lengths[n];
        unsigned char *storage=aligned_alloc(16,816),*source=CONFIG_KMSAN?malloc(len+1):aligned_alloc(16,816);CHECK(storage&&source);memset(storage,0xa5,816);memset(source,'S',CONFIG_KMSAN?len+1:816);source[len]=0;
        char *dst=(char*)storage+16;size_t copied=cap?(len<cap?len:cap-1):0;
        ssize_t result=sized_strscpy(dst,(char*)source,cap);CHECK(result==(cap&&len<cap?(ssize_t)len:-E2BIG));
        for(size_t i=0;i<16;i++)CHECK(storage[i]==0xa5);
        for(size_t i=16+cap;i<816;i++)CHECK(storage[i]==0xa5);
        if(cap){CHECK(dst[copied]==0);for(size_t i=0;i<copied;i++)CHECK(dst[i]=='S');}
#ifndef NATIVE_API
        char *old=malloc(cap+16);CHECK(old);memset(old,0xa5,cap+16);CHECK(strlcpy(old,(char*)source,cap)==len);if(cap)CHECK(!strcmp(old,dst));free(old);
#endif
        free(storage);free(source);
    }
    scenarios++;char out[8]="stable";int before=warnings;CHECK(sized_strscpy(out,"x",(size_t)INT_MAX+1)==-E2BIG);CHECK(!strcmp(out,"stable")&&warnings==before+1);
}
static void filter_boundaries(void)
{
    const size_t caps[]={1,2,8,63,64,65,128,(size_t)INT_MAX+1,SIZE_MAX};
    const size_t lengths[]={0,1,7,31,63};
    struct sde_dbg_evtlog log={0};INIT_LIST_HEAD(&log.filter_list);struct sde_evtlog_filter node={0};add_tail(&node.list,&log.filter_list);
    for(size_t n=0;n<sizeof(lengths)/sizeof(*lengths);n++)for(size_t c=0;c<sizeof(caps)/sizeof(*caps);c++){
        scenarios++;memset(node.filter,'F',sizeof(node.filter));node.filter[lengths[n]]=0;char out[256];memset(out,0xa5,sizeof(out));int before=warnings;
        CHECK(sde_evtlog_get_filter(&log,0,out,caps[c])==0);CHECK(warnings==before);size_t copied=lengths[n]<caps[c]?lengths[n]:caps[c]-1;CHECK(out[copied]==0);for(size_t i=0;i<copied;i++)CHECK(out[i]=='F');for(size_t i=(caps[c]<sizeof(out)?caps[c]:sizeof(out));i<sizeof(out);i++)CHECK((unsigned char)out[i]==0xa5);CHECK(!log.spin_lock);
    }
    char out[8]="stable";scenarios++;CHECK(sde_evtlog_get_filter(NULL,0,out,8)==-EINVAL);CHECK(sde_evtlog_get_filter(&log,-1,out,8)==-EINVAL);CHECK(sde_evtlog_get_filter(&log,0,NULL,8)==-EINVAL);CHECK(sde_evtlog_get_filter(&log,0,out,0)==-EINVAL);CHECK(!strcmp(out,"stable"));
    scenarios++;CHECK(sde_evtlog_get_filter(&log,1,out,8)==-EFAULT);CHECK(!strcmp(out,"stable"));
}
static void parser_boundaries(void)
{
    const size_t lengths[]={0,1,7,31,127,511,700};
    for(size_t n=0;n<sizeof(lengths)/sizeof(*lengths);n++){
        scenarios++;size_t len=lengths[n];char *buf=malloc(len+3);CHECK(buf);buf[0]='k';buf[1]='=';memset(buf+2,'P',len);buf[len+2]=0;struct device dev={0};struct dsi_parser_prop prop={0};fail_alloc=allocations=0;
        CHECK(dsi_parser_parse_prop(&dev,&prop,buf));CHECK(prop.raw&&strlen(prop.raw)==len);for(size_t i=0;i<len;i++)CHECK(prop.raw[i]=='P');memset(buf,'Q',len+2);CHECK(strlen(prop.raw)==len);free(prop.raw);free(buf);
    }
    scenarios++;char bad[]="no-equals";struct device dev={0};struct dsi_parser_prop p={0};CHECK(!dsi_parser_parse_prop(&dev,&p,bad)&&!p.raw);
    scenarios++;char value[]="k=payload";fail_alloc=1;allocations=0;CHECK(!dsi_parser_parse_prop(&dev,&p,value)&&!p.raw);fail_alloc=0;
}
static void mode_boundaries(void)
{
    char *modes[]={"","h","hw","all","all-extra","hardware","abcdefg"};
    for(size_t i=0;i<sizeof(modes)/sizeof(*modes);i++){
        scenarios++;struct dp_parser parser={clear_io};struct dp_sub sub={0};struct dp_catalog_private p={.dp_catalog={&sub},.parser=&parser};cleared=got=0;dp_catalog_set_exe_mode(&p.dp_catalog,modes[i]);char expected[4];size_t len=strlen(modes[i]);if(len>3)len=3;memcpy(expected,modes[i],len);expected[len]=0;CHECK(!strcmp(p.exe_mode,expected));bool hw=!strcmp(expected,"hw"),all=!strcmp(expected,"all");CHECK(cleared==hw&&got==!hw);CHECK(p.read==(hw||all?dp_read_hw:dp_read_sw));CHECK(sub.read==(hw||all?dp_read_sub_hw:dp_read_sub_sw));
    }
    scenarios++;cleared=got=0;dp_catalog_set_exe_mode(NULL,"hw");CHECK(!cleared&&!got);
}
int main(void){api_boundaries();filter_boundaries();parser_boundaries();mode_boundaries();printf("PASS %d actual driver/string scenarios CONFIG_KMSAN=%d kernel_api_model=%d\n",scenarios,CONFIG_KMSAN,
#ifdef NATIVE_API
1
#else
0
#endif
);return 0;}
