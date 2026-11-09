#!/usr/bin/env python3
"""Run actual v4/v6 packetizers from a matched Venus candidate under sanitizers."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

def main(directory):
    source = (directory / "hfi_cmds.c").read_text()
    start = source.index("static int\npkt_session_set_property_4xx(")
    end = source.index("int pkt_session_get_property(", start)
    functions = source[start:end]
    def struct(name, filename):
        text = (directory / filename).read_text()
        return re.search(r"struct " + name + r" \{.*?\n\};", text, re.S)[0]
    headers = "\n".join([
        struct("hfi_session_set_property_pkt", "hfi_cmds.h")])
    code = r"""
    #include <assert.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <string.h>
    #include <errno.h>
    typedef uint32_t u32;
    typedef uint64_t u64;
    typedef int32_t s32;
    typedef uint8_t u8;
    #define ENOTSUPP 524
    #define HFI_CMD_SESSION_SET_PROPERTY 0x11001
    #define hash32_ptr(p) ((u32)(uintptr_t)(p))
    #include "hfi_helper.h"
    """ + headers + r"""
    static int pkt_session_set_property_3xx(struct hfi_session_set_property_pkt *p,void *c,u32 t,void *d){(void)p;(void)c;(void)t;(void)d;return -EINVAL;}
    """ + functions + r"""
    int main(void){
     union {u64 align;unsigned char data[256];} storage;
     struct hfi_session_set_property_pkt *pkt=(void*)storage.data;
     struct hfi_uncompressed_plane_actual_constraints_info input={.buffer_type=HFI_BUFFER_OUTPUT2,.num_planes=2};
     input.plane_format[0]=(struct hfi_uncompressed_plane_constraints){512,512,512,256};
     input.plane_format[1]=(struct hfi_uncompressed_plane_constraints){512,512,256,256};
     for(int version=4;version<=6;version+=2){
      int (*encode)(struct hfi_session_set_property_pkt*,void*,u32,void*)=version==4?pkt_session_set_property_4xx:pkt_session_set_property_6xx;
      for(u32 n=1;n<=2;n++){
       memset(storage.data,0xa5,sizeof(storage.data));input.num_planes=n;
       assert(!encode(pkt,(void*)1,HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO,&input));
       assert(pkt->shdr.hdr.size==sizeof(*pkt)+sizeof(u32)+sizeof(input));
       assert(pkt->shdr.hdr.pkt_type==HFI_CMD_SESSION_SET_PROPERTY&&pkt->shdr.session_id==1&&pkt->num_properties==1);
       assert(pkt->data[0]==HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO);
       struct hfi_uncompressed_plane_actual_constraints_info *out=(void*)&pkt->data[1];
       assert(out->buffer_type==input.buffer_type&&out->num_planes==n);
       assert(!memcmp(&out->plane_format[0],&input.plane_format[0],sizeof(input.plane_format[0])));
       if(n==2)assert(!memcmp(out,&input,sizeof(input)));
       else {struct hfi_uncompressed_plane_constraints zero={0};assert(!memcmp(&out->plane_format[1],&zero,sizeof(zero)));}
       for(unsigned i=pkt->shdr.hdr.size;i<sizeof(storage.data);i++)assert(storage.data[i]==0xa5);
      }
      u32 bad[]={0,3,UINT32_MAX};
      for(unsigned i=0;i<3;i++){input.num_planes=bad[i];assert(encode(pkt,(void*)1,HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO,&input)==-EINVAL);}
     }
     return 0;
    }
    """
    with tempfile.TemporaryDirectory() as tmp:
        d=Path(tmp)
        (d/"test.c").write_text(code)
        subprocess.run(["cc","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                        "-fno-pie","-no-pie","-I",str(directory),str(d/"test.c"),"-o",str(d/"test")],check=True)
        subprocess.run([str(d/"test")],check=True)
    print("HFI_V4_V6_NATIVE_CONSTRAINTS_PACKET_BOUNDS_PASS")

if __name__ == "__main__":
    main(Path(sys.argv[1]))
