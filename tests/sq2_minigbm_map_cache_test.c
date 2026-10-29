#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drv_priv.h"
#include "drv_array_helpers.h"
#undef drv_loge
#define drv_loge(...) ((void)0)
#include "sq2-map-lifetime.inc"
#include "sq2-minigbm-map-cache.h"
static unsigned released;
static int unmap_test(struct bo *bo,struct vma *vma){(void)bo;(void)vma;++released;return 0;}
int main(void){
 struct backend backend={.name="msm",.bo_unmap=unmap_test};
 struct driver driver={.backend=&backend};pthread_mutex_init(&driver.mappings_lock,NULL);
 driver.mappings=drv_array_init(sizeof(struct mapping));
 struct bo bo={.drv=&driver};bo.handle.u32=17;bo.meta.format=DRM_FORMAT_NV12;bo.meta.num_planes=2;
 struct vma *vma=calloc(1,sizeof(*vma));*vma=(struct vma){.length=8*1024*1024,.map_flags=BO_MAP_READ_WRITE,.refcount=1,.handle=17};
 struct mapping sample={.vma=vma,.refcount=1};struct mapping *m=drv_array_append(driver.mappings,&sample);
 unsetenv("SQ2_VIDEO_MAP_CACHE");assert(!sq2_cache_mapping(&bo,m)&&m->refcount==1);
 setenv("SQ2_VIDEO_MAP_CACHE","1",1);
 bo.meta.cached=true;assert(!sq2_cache_mapping(&bo,m));bo.meta.cached=false;
 bo.meta.use_flags=BO_USE_PROTECTED;assert(!sq2_cache_mapping(&bo,m));bo.meta.use_flags=0;
 bo.meta.format_modifier=1;assert(!sq2_cache_mapping(&bo,m));bo.meta.format_modifier=0;
 assert(sq2_cache_mapping(&bo,m)&&m->refcount==2);
 assert(!drv_bo_unmap(&bo,m)&&m->refcount==1&&!released);
 for(unsigned n=0;n<10000;n++){++m->refcount;assert(sq2_cache_mapping(&bo,m));assert(!drv_bo_unmap(&bo,m)&&m->refcount==1);}
 drv_bo_mapping_destroy(&bo);assert(released==1&&drv_array_size(driver.mappings)==0);
 /* Sixteen 12MiB mappings reach both limits exactly. A further active mapping
  * must use the unchanged unmap path and must not become retained. */
 for(unsigned n=0;n<16;n++){
  vma=calloc(1,sizeof(*vma));*vma=(struct vma){.length=12*1024*1024,.map_flags=BO_MAP_READ_WRITE,.refcount=1,.handle=17};
  sample=(struct mapping){.vma=vma,.refcount=1};m=drv_array_append(driver.mappings,&sample);
  assert(sq2_cache_mapping(&bo,m));assert(!drv_bo_unmap(&bo,m));
 }
 vma=calloc(1,sizeof(*vma));*vma=(struct vma){.length=4096,.map_flags=BO_MAP_READ_WRITE,.refcount=1,.handle=17};
 sample=(struct mapping){.vma=vma,.refcount=1};m=drv_array_append(driver.mappings,&sample);
 assert(!sq2_cache_mapping(&bo,m)&&!m->sq2_cached);assert(!drv_bo_unmap(&bo,m)&&released==2);
 drv_bo_mapping_destroy(&bo);assert(released==18&&drv_array_size(driver.mappings)==0);
 /* Reach the byte budget before the entry budget. */
 for(unsigned n=0;n<12;n++){
  vma=calloc(1,sizeof(*vma));*vma=(struct vma){.length=16*1024*1024,.map_flags=BO_MAP_READ_WRITE,.refcount=1,.handle=17};
  sample=(struct mapping){.vma=vma,.refcount=1};m=drv_array_append(driver.mappings,&sample);
  assert(sq2_cache_mapping(&bo,m));assert(!drv_bo_unmap(&bo,m));
 }
 vma=calloc(1,sizeof(*vma));*vma=(struct vma){.length=4096,.map_flags=BO_MAP_READ_WRITE,.refcount=1,.handle=17};
 sample=(struct mapping){.vma=vma,.refcount=1};m=drv_array_append(driver.mappings,&sample);
 assert(!sq2_cache_mapping(&bo,m));assert(!drv_bo_unmap(&bo,m));
 drv_bo_mapping_destroy(&bo);assert(released==31&&drv_array_size(driver.mappings)==0);
 drv_array_destroy(driver.mappings);pthread_mutex_destroy(&driver.mappings_lock);
 puts("MAP_CACHE_DEFAULT_BOUNDS_10000_REUSE_DESTROY_PASS");
}
