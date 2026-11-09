"""Compile actual QTI HDR handlers and sequence-event case, not a reimplementation."""
import argparse
from pathlib import Path
import subprocess


def block(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--headers', type=Path, required=True)
    parser.add_argument('--video-api', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = (args.source / 'mm-video-v4l2/vidc/vdec/src/omx_vdec_v4l2.cpp').read_text()
    api = args.video_api.read_text()
    payloads = (args.headers / 'media/msm_vidc_utils.h').read_text()
    hdr = block(api, 'struct __attribute__ ((__packed__)) HDRStaticInfo') + ';'
    methods = '\n'.join(block(source, marker) for marker in (
        'bool omx_vdec::handle_content_light_level_info(',
        'bool omx_vdec::handle_mastering_display_color_info(',
        'void omx_vdec::get_preferred_hdr_info('))
    start = source.index('case VDEC_MSG_EVT_CONFIG_CHANGED:')
    case = source[start:source.index('break;', start) + len('break;')]
    definitions = '\n'.join(line for line in source.splitlines()
                            if line.startswith('#define LUMINANCE_'))
    harness = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <linux/videodev2.h>
#define DEBUG_PRINT_HIGH(...) ((void)0)
#define OMX_CORE_OUTPUT_PORT_INDEX 1
#define OMX_IndexParamPortDefinition 2
#define OMX_COMPONENT_GENERATE_PORT_RECONFIG 3
#define VDEC_MSG_EVT_CONFIG_CHANGED 4
@DEFS@
@HDR@
@PAYLOADS@
struct info { unsigned nPortIndex; HDRStaticInfo sInfo; };
class omx_vdec {
public:
    info m_internal_hdr_info{}, m_client_hdr_info{};
    unsigned output_capability=V4L2_PIX_FMT_HEVC;
    unsigned m_reconfig_width=0, m_reconfig_height=0;
    bool isPortReconfigInsufficient=false;
    int notices=0;
    void post_event(int port,int index,int event) {
        assert(port==1 && index==2 && event==3);++notices;
    }
    bool handle_content_light_level_info(void*);
    bool handle_mastering_display_color_info(void*);
    void get_preferred_hdr_info(HDRStaticInfo&);
};
@METHODS@
struct message { struct { struct {struct {unsigned frame_width,frame_height;} picsize; bool flags;} output_frame;} msgdata; };
void event(omx_vdec &decoder, int type) {
    omx_vdec *omx=&decoder;
    message msg{}; message *vdec_msg=&msg;
    msg.msgdata.output_frame.picsize={640,480};msg.msgdata.output_frame.flags=true;
    switch(type) {@CASE@ default:break;}
}
bool empty(const HDRStaticInfo &info) {
    HDRStaticInfo zero{};return !memcmp(&info,&zero,sizeof(zero));
}
int main() {
    omx_vdec a,b;
    a.m_internal_hdr_info.nPortIndex=1;
    msm_vidc_mastering_display_colour_sei_payload m{};
    m.nDisplayPrimariesX[0]=13250;m.nDisplayPrimariesY[0]=34500;
    m.nDisplayPrimariesX[1]=7500;m.nDisplayPrimariesY[1]=3000;
    m.nDisplayPrimariesX[2]=34000;m.nDisplayPrimariesY[2]=16000;
    m.nWhitePointX=15635;m.nWhitePointY=16450;
    m.nMaxDisplayMasteringLuminance=10000000;m.nMinDisplayMasteringLuminance=50;
    msm_vidc_content_light_level_sei_payload cll{};
    cll.nMaxContentLight=1000;cll.nMaxPicAverageLight=400;
    assert(a.handle_mastering_display_color_info(&m));
    assert(a.handle_content_light_level_info(&cll));
    HDRStaticInfo original{};a.get_preferred_hdr_info(original);
    assert(original.sType1.mMaxDisplayLuminance==1000);
    for(int i=0;i<10;++i){HDRStaticInfo out{};a.get_preferred_hdr_info(out);assert(!memcmp(&out,&original,sizeof(out)));}
    event(a,99);assert(a.notices==0);
    assert(!a.handle_mastering_display_color_info(&m));assert(!a.handle_content_light_level_info(&cll));
    cll.nMaxContentLight=800;assert(a.handle_content_light_level_info(&cll));
    HDRStaticInfo changed{};a.get_preferred_hdr_info(changed);assert(changed.sType1.mMaxContentLightLevel==800);
    event(a,VDEC_MSG_EVT_CONFIG_CHANGED);
    assert(empty(a.m_internal_hdr_info.sInfo));assert(a.m_internal_hdr_info.nPortIndex==1);
    assert(a.notices==1 && a.m_reconfig_width==640 && a.m_reconfig_height==480 && a.isPortReconfigInsufficient);
    HDRStaticInfo sdr{};a.get_preferred_hdr_info(sdr);assert(empty(sdr));
    for(int i=0;i<10;++i){HDRStaticInfo out{};a.get_preferred_hdr_info(out);assert(empty(out));}
    event(a,VDEC_MSG_EVT_CONFIG_CHANGED);assert(a.notices==2);
    assert(a.handle_mastering_display_color_info(&m));assert(a.handle_content_light_level_info(&cll));
    assert(empty(b.m_internal_hdr_info.sInfo));
    a.m_client_hdr_info.sInfo.sType1.mMaxContentLightLevel=600;
    event(a,VDEC_MSG_EVT_CONFIG_CHANGED);
    HDRStaticInfo defaults{};a.get_preferred_hdr_info(defaults);assert(defaults.sType1.mMaxContentLightLevel==600);
    assert(a.handle_content_light_level_info(&cll));
    HDRStaticInfo hevc{};a.get_preferred_hdr_info(hevc);assert(hevc.sType1.mMaxContentLightLevel==800);
    a.output_capability=V4L2_PIX_FMT_VP9;
    HDRStaticInfo vp9{};a.get_preferred_hdr_info(vp9);assert(vp9.sType1.mMaxContentLightLevel==600);
    puts("PASS actual HDR handlers: persistence, update, unrelated event, sequence reset, new metadata, node isolation, client fallback, VP9 precedence");
}
"""
    structures = '\n'.join(block(payloads, 'struct ' + name) + ';' for name in (
        'msm_vidc_content_light_level_sei_payload', 'msm_vidc_mastering_display_colour_sei_payload'))
    for key, value in {'DEFS': definitions, 'HDR': hdr, 'PAYLOADS': structures,
                       'METHODS': methods, 'CASE': case}.items():
        harness = harness.replace('@' + key + '@', value)
    args.output.mkdir(parents=True, exist_ok=True)
    cpp = args.output / 'hdr.cpp'
    cpp.write_text(harness)
    exe = args.output / 'hdr-test'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
