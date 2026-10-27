#!/usr/bin/env python3
"""Extend the minigbm mapper5 helper to bounded multi-plane DMA buffers."""
from pathlib import Path
import sys

p=Path(sys.argv[1]) if len(sys.argv)>1 else Path('/root/.cache/sq2-graphics/src/mesa-26.2.4/src/util/u_gralloc/u_gralloc_vm_mapper5.cpp')
s=p.read_text()
if 'SQ2_MAPPER_IMPORT_V1' not in s:
    old='''   Reader fourcc,modifier,planes;
   if(!metadata(m,hnd->handle,7,fourcc)||!metadata(m,hnd->handle,8,modifier)||!metadata(m,hnd->handle,15,planes))return -1;'''
    new='''   // SQ2_MAPPER_IMPORT_V1: libui may use a different linker namespace.
   // Import into this mapper instance before asking its driver for metadata.
   // Keep output fds borrowed from the original handle, not this temporary copy.
   struct Imported {
      AIMapper *m; buffer_handle_t h=nullptr;
      ~Imported(){if(h)m->v5.freeBuffer(h);}
   } imported{m};
   if(m->v5.importBuffer(hnd->handle,&imported.h)!=AIMAPPER_ERROR_NONE)return -1;
   Reader fourcc,modifier,planes;
   if(!metadata(m,imported.h,7,fourcc)||!metadata(m,imported.h,8,modifier)||!metadata(m,imported.h,15,planes))return -1;'''
    if old not in s: raise SystemExit('Unexpected mapper metadata source')
    s=s.replace(old,new,1)
if 'SQ2_MULTIPLANE_V1' in s:
    p.write_text(s)
    raise SystemExit(0)
start=s.index('   if(!fourcc.bytes(')
end=s.index('   return 0;',start)
s=s[:start]+'''   // SQ2_MULTIPLANE_V1: minigbm exports one fd per image plane,
   // optionally followed by a metadata fd. Single-fd shared layouts are
   // supported too. Never mistake that trailing metadata fd for a plane.
   if(!fourcc.bytes(&info.drm_fourcc,4)||fourcc.pos!=fourcc.size||
      !modifier.integer(info.modifier)||modifier.pos!=modifier.size||
      !planes.integer(count)||count<1||count>4)return -1;
   if(hnd->handle->numFds!=1 && (uint64_t)hnd->handle->numFds<count)return -1;
   info.num_planes=(int)count;
   for(uint64_t plane=0;plane<count;plane++) {
      uint64_t components,unused;
      if(!planes.integer(components)||components>16)return -1;
      for(uint64_t i=0;i<components;i++)
         if(!planes.string_skip()||!planes.integer(unused)||!planes.integer(unused)||!planes.integer(unused))return -1;
      uint64_t fields[8];for(auto &v:fields)if(!planes.integer(v))return -1;
      if(fields[0]>INT_MAX||!fields[2]||fields[2]>INT_MAX)return -1;
      info.fds[plane]=hnd->handle->data[hnd->handle->numFds==1?0:plane];
      if(info.fds[plane]<0)return -1;
      info.offsets[plane]=(int)fields[0];
      info.strides[plane]=(int)fields[2];
   }
   if(planes.pos!=planes.size)return -1;
   *out=info;
''' + s[end:]
p.write_text(s)
print('Mapper5 multi-plane support applied; maximum four planes')
