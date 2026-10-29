/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_CAPABILITIES_H
#define VENUS_SQ2_CAPABILITIES_H
#include <linux/slab.h>
#include "sq2-hfi-properties.h"
/* Included after core.h/hfi_helper.h. No changes to other Venus platforms. */
struct sq2_caps_transaction {
 struct hfi_plat_caps caps[MAX_CODEC_NUM];
 u32 count, dec, enc, sessions, codecs, domains;
 bool declared;
};
static int sq2_caps_property(void *context,u32 id,const u32 *p,u32 words)
{
 struct sq2_caps_transaction *t=context;
 u32 i,j,n,at;
 (void)words; /* Walker has validated every source span. */
 if(id==0x1009){
  if(t->declared||!p[0])return -1;
  t->declared=true;t->dec=p[0];t->enc=p[1];
  for(i=1;i<=2;i++)for(j=0;j<32;j++){
   u32 bit=1U<<j,mask=i==1?t->enc:t->dec;
   struct hfi_plat_caps *c;
   if(!(mask&bit))continue;
   if(t->count>=MAX_CODEC_NUM)return -1;
   c=&t->caps[t->count++];c->codec=bit;c->domain=i;
  }
  return 0;
 }
 if(id==0x1010){if(!p[0]||p[0]>MAX_SESSIONS)return -1;t->sessions=p[0];return 0;}
 if(id==0x100a||id==0x1016)return 0;
 if(!t->declared)return -1;
 if(id==0x100e){
  if(!p[0]||!p[1]||(p[1]&~3U)|| (p[0]&~(t->dec|t->enc)))return -1;
  t->codecs=p[0];t->domains=p[1];return 0;
 }
 if(!t->codecs||!t->domains)return -1;
 for(i=0;i<t->count;i++){
  struct hfi_plat_caps *c=&t->caps[i];
  if(!(c->codec&t->codecs)||!(c->domain&t->domains))continue;
  switch(id){
  case 0x1007:
   n=p[0];if(n>MAX_CAP_ENTRIES-c->num_caps)return -1;
   for(j=0;j<n;j++){
    const u32 *v=p+1+j*4;
    if(v[1]>v[2]||!v[3])return -1;
   }
   memcpy(c->caps+c->num_caps,p+1,n*sizeof(c->caps[0]));c->num_caps+=n;break;
  case 0x1006:
   n=p[0];if(n>HFI_MAX_PROFILE_COUNT-c->num_pl)return -1;
   memcpy(c->pl+c->num_pl,p+1,n*sizeof(c->pl[0]));c->num_pl+=n;break;
  case 0x1004:
   n=p[1];if(n>MAX_FMT_ENTRIES-c->num_fmts)return -1;
   at=2;
   for(j=0;j<n;j++){
    c->fmts[c->num_fmts].buftype=p[0];c->fmts[c->num_fmts++].fmt=p[at];
    at+=2+4*p[at+1];
   }
   break;
  case 0x20100b:
   if(p[1]>MAX_ALLOC_MODE_ENTRIES)return -1;
   if(p[0]==HFI_BUFFER_OUTPUT||p[0]==HFI_BUFFER_OUTPUT2)
    for(j=0;j<p[1];j++)if(p[j+2]==HFI_BUFFER_MODE_DYNAMIC)c->cap_bufs_mode_dynamic=true;
   break;
  default:return -1;
  }
 }
 return 0;
}
static u32 venus_sq2_parse_capabilities(struct venus_core *core,const void *body,u32 size,u32 expected)
{
 struct sq2_caps_transaction *t;
 u32 records=0,result=HFI_ERR_SYS_INSUFFICIENT_RESOURCES;
 if(!size||size%4||!expected)return result;
 t=kzalloc(sizeof(*t),GFP_KERNEL);if(!t)return result;
 if(sq2_hfi_properties(body,size/4,sq2_caps_property,t,&records)||records!=expected||!t->declared||!t->count)
  goto done;
 /* Publish only a complete, validated table. Retain no pointers into packet. */
 memcpy(core->caps,t->caps,sizeof(t->caps));
 core->codecs_count=t->count;core->dec_codecs=t->dec;core->enc_codecs=t->enc;
 core->max_sessions_supported=t->sessions?t->sessions:MAX_SESSIONS;
 result=HFI_ERR_NONE;
done:
 kfree(t);return result;
}
#endif
