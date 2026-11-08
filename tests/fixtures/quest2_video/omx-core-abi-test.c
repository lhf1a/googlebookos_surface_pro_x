#include <assert.h>
#include "quest2-omx-port-abi.h"
int main(void){
 _Static_assert(sizeof(struct quest2_port32)==96 && sizeof(OMX_PARAM_PORTDEFINITIONTYPE)==112,"ABI");
 struct {struct quest2_port32 p;uint32_t canary;} b={0};b.p.size=96;b.p.version=0x20101;b.p.port=1;b.canary=0x12345678;
 OMX_PARAM_PORTDEFINITIONTYPE n;assert(quest2_port_to_native(&b.p,&n,0));assert(n.nSize==112&&n.nPortIndex==1);
 n.eDomain=OMX_PortDomainVideo;n.nBufferCountActual=6;n.format.video.nFrameWidth=320;n.format.video.nFrameHeight=240;n.format.video.nStride=384;n.format.video.nSliceHeight=256;n.format.video.eColorFormat=(OMX_COLOR_FORMATTYPE)0x7fa30c04;n.nBufferAlignment=4096;
 assert(quest2_port_to_wire(&n,&b.p));assert(b.p.video.width==320&&b.p.video.stride==384&&b.p.video.color==0x7fa30c04&&b.p.alignment==4096&&b.canary==0x12345678);
 b.p.bytes=384*256*3/2;struct quest2_port32 layout=b.p;
 assert(quest2_port_linear_layout(&layout));assert(layout.video.width==384&&layout.video.height==256&&layout.video.color==OMX_COLOR_FormatYUV420SemiPlanar);
 assert(b.p.video.width==320);layout=b.p;layout.bytes--;assert(!quest2_port_linear_layout(&layout));
 layout=b.p;layout.video.stride=319;assert(!quest2_port_linear_layout(&layout));
 layout=b.p;layout.video.slice=239;assert(!quest2_port_linear_layout(&layout));
 layout=b.p;layout.port=0;assert(quest2_port_linear_layout(&layout)&&layout.video.width==320);
 layout=b.p;layout.video.color=QUEST2_COLOR_P010_VENUS;layout.video.stride=768;layout.bytes=768*256*3/2;
 assert(quest2_port_linear_layout(&layout)&&layout.video.width==384&&layout.video.height==256&&layout.video.color==QUEST2_COLOR_P010&&layout.video.stride==768);
 layout=b.p;layout.video.color=QUEST2_COLOR_P010_VENUS;assert(!quest2_port_linear_layout(&layout));
 layout.video.stride=640;layout.bytes=640*256*3/2-1;assert(!quest2_port_linear_layout(&layout));
 layout.video.stride=642;layout.bytes=642*256*3/2;assert(!quest2_port_linear_layout(&layout));
 b.p.video.width=640;b.p.actual=8;assert(quest2_port_to_native(&b.p,&n,1));assert(n.format.video.nFrameWidth==640&&n.nBufferCountActual==8);
 b.p.video.window=1;assert(!quest2_port_to_native(&b.p,&n,1));b.p.video.window=0;b.p.size=95;assert(!quest2_port_to_native(&b.p,&n,0));return 0;
}
