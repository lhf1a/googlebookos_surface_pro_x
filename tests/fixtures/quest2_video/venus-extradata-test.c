/* Host mock, not firmware. Execute production selection and DMA bounds helpers. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
typedef uint32_t u32;
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define max(a,b) ((a)>(b)?(a):(b))
#define HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS 0x202001
#define HFI_VIDEO_CODEC_HEVC 1
#define HFI_VIDEO_CODEC_H264 2
#define VIDC_SESSION_TYPE_DEC 1
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE 9
#define HFI_BUFFER_EXTRADATA_OUTPUT(v) ((v)==V4?10:0x1000003)
#define HFI_BUFFER_EXTRADATA_OUTPUT2(v) ((v)==V4?11:0x1000004)
#define IS_SQ2_HFI(c) ((c)->sq2)
enum hfi_version {V3,V4};
struct hfi_buffer_requirements {u32 type,size;};
union hfi_get_property {struct hfi_buffer_requirements bufreq[11];};
struct resource {enum hfi_version hfi_version;};
struct core {struct resource *res;bool sq2;};
struct venus_inst {struct core *core;u32 sq2_extra_mask,sq2_extra_size,hfi_codec,session_type;};
struct vb2_buffer {unsigned num_planes,type;struct {uint64_t dma;u32 size,data_offset;} planes[2];};
#define vb2_dma_contig_plane_dma_addr(v,i) ((v)->planes[i].dma)
#define vb2_plane_size(v,i) ((v)->planes[i].size)
static bool sq2_venus_buffer_valid(uint64_t dma,u32 size) {
 return dma && size && dma<=UINT32_MAX && size-1<=UINT32_MAX-dma;
}
static union hfi_get_property reply;
static unsigned sets,gets;static int error,property_error;
static int hfi_session_get_property(struct venus_inst *i,u32 p,union hfi_get_property *out) {
 assert(i&&p==HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS);gets++;
 if(error)return error;*out=reply;return 0;
}
static int hfi_session_set_property(struct venus_inst *i,u32 p,void *data) {
 u32 *words=data;assert(i&&words[0]==1&&words[1]==p);
 assert(p==0x120301b||p==0x120301e||p==0x120301f);sets++;
 return property_error;
}
#define SQ2_EXTRADATA_DECODER
#define SQ2_EXTRADATA_BUFFERS
#include "venus-sq2-extradata.h"
int main(void) {
 u32 total=0;
 assert(!sq2_extra_dpb_size(294912,188,4096,&total)&&total==299008);
 assert(!sq2_extra_dpb_size(294912,0,4096,&total)&&total==294912);
 assert(sq2_extra_dpb_size(UINT32_MAX,188,4096,&total)==-EOVERFLOW);
 assert(sq2_extra_dpb_size(0,188,4096,&total)==-EOVERFLOW);

 assert(sq2_extra_dpb_size(UINT32_MAX-10,0,4096,&total)==-EOVERFLOW);
 assert(sq2_extra_dpb_size(4096,1,3,&total)==-EINVAL);
 struct resource r={V3};struct core c={&r,true};
 struct venus_inst i={.core=&c,.hfi_codec=HFI_VIDEO_CODEC_HEVC,.session_type=VIDC_SESSION_TYPE_DEC};
 assert(!sq2_extra_enable(&i)&&!sets&&!gets);
 assert(!sq2_extra_property(8)&&sq2_extra_property(1)==0x120301b);
 i.sq2_extra_mask=7;
 assert(sq2_extra_enable(&i)==-ENODATA&&sets==3);
 reply.bufreq[0]=(struct hfi_buffer_requirements){0x1000003,4096};
 reply.bufreq[1]=(struct hfi_buffer_requirements){0x1000004,8192};
 assert(!sq2_extra_enable(&i)&&i.sq2_extra_size==8192);
 r.hfi_version=V4;assert(sq2_extra_query(&i)==-ENODATA);
 reply.bufreq[0].type=10;reply.bufreq[1].type=11;
 assert(!sq2_extra_query(&i)&&i.sq2_extra_size==8192);
 error=-ETIMEDOUT;assert(sq2_extra_query(&i)==-ETIMEDOUT);error=0;
 property_error=-EIO;assert(sq2_extra_enable(&i)==-EIO);property_error=0;
 i.hfi_codec=99;assert(sq2_extra_enable(&i)==-EOPNOTSUPP);
 i.hfi_codec=HFI_VIDEO_CODEC_H264;assert(sq2_extra_enable(&i)==-EOPNOTSUPP);
 i.sq2_extra_mask=1;assert(!sq2_extra_enable(&i));
 c.sq2=false;assert(sq2_extra_enable(&i)==-EOPNOTSUPP);c.sq2=true;
 struct vb2_buffer b={.num_planes=2,.type=9,.planes={{0x100000,16384,0},{0x200000,8192,0}}};
 assert(!sq2_extra_validate(&i,&b));
 b.num_planes=1;assert(sq2_extra_validate(&i,&b)==-EINVAL);b.num_planes=2;
 b.planes[1].size=4096;assert(sq2_extra_validate(&i,&b)==-EINVAL);b.planes[1].size=8192;
 b.planes[1].data_offset=1;assert(sq2_extra_validate(&i,&b)==-EINVAL);b.planes[1].data_offset=0;
 b.planes[1].dma=UINT32_MAX-1;assert(sq2_extra_validate(&i,&b)==-ERANGE);
 b.planes[1].dma=0;assert(sq2_extra_validate(&i,&b)==-ERANGE);
 b.type=10;assert(sq2_extra_validate(&i,&b)==-EINVAL);
 b.num_planes=1;assert(!sq2_extra_validate(&i,&b));
 b.type=9;i.sq2_extra_mask=0;assert(!sq2_extra_validate(&i,&b));
 b.num_planes=2;assert(sq2_extra_validate(&i,&b)==-EINVAL);
 return 0;
}
