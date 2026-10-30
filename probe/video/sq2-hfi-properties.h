/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_HFI_PROPERTIES_H
#define SQ2_HFI_PROPERTIES_H
/* Host/kernel shared walker for the observed Iris1 SYS_INIT body. u32 must be
 * supplied by the caller. Callback payloads are bounded; consumers must also
 * bound their destination arrays and publish only after the whole walk passes.
 */
typedef int (*sq2_hfi_property_fn)(void *, u32, const u32 *, u32);
static inline int sq2_hfi_properties(const u32 *body, u32 words,
                                    sq2_hfi_property_fn visit, void *context,
                                    u32 *records)
{
 u32 at=0, count=0;
 if(!body||!words||words>4096||!records)return -1;
 while(at<words){
  u32 id=body[at++], start=at, n, i, planes;
  switch(id){
  case 0x1009: case 0x100e:
   if(words-at<2)return -1;
   at+=2;break;
  case 0x1010: case 0x100a:
   if(words-at<1)return -1;
   at++;break;
  case 0x1016:
   /* Observed trailing TME version: do not infer a variable-size layout. */
   if(words-at!=1)return -1;
   at++;break;
  case 0x1007: case 0x1006:
   if(words-at<1)return -1;
   n=body[at++];i=id==0x1007?4:2;
   if(n>(words-at)/i)return -1;
   at+=n*i;break;
  case 0x20100b:
   if(words-at<2)return -1;
   n=body[at+1];at+=2;
   if(n>words-at)return -1;
   at+=n;break;
  case 0x1004:
   if(words-at<2)return -1;
   n=body[at+1];at+=2;
   if(n>(words-at)/2)return -1;
   for(i=0;i<n;i++){
    if(words-at<2)return -1;
    planes=body[at+1];at+=2;
    if(!planes||planes>4||planes>(words-at)/4)return -1;
    at+=planes*4;
   }
   break;
  default:return -1;
  }
  if(visit&&visit(context,id,body+start,at-start))return -1;
  count++;
 }
 *records=count;return 0;
}
#endif
