/* Run the actual patched HWC allocator against AHB/mapper5; never start a composer. */
#include "hwc_buffer_allocator.h"
#include <android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>
#include <android/hardware_buffer.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
namespace gralloc { int GetRgbDataAddress(private_handle_t *, void **); }
static AIMapper *mapper;
static int fd_count() {
  DIR *d = opendir("/proc/self/fd"); assert(d);
  int n=0; while (readdir(d)) ++n; closedir(d); return n;
}
static void check(sdm::HWCBufferAllocator &allocator, sdm::LayerBufferFormat format,
                  uint32_t width, uint32_t height, bool cache) {
  sdm::BufferInfo info{};
  auto &config=info.buffer_config;
  config.width=width; config.height=height; config.format=format;
  config.cache=cache; config.gfx_client=true;
  sdm::AllocatedBufferInfo predicted{};
  assert(allocator.GetAllocatedBufferInfo(config,&predicted)==0);
  assert(allocator.GetBufferSize(&info)==predicted.size && predicted.size);
  assert(allocator.AllocateBuffer(&info)==0);
  auto &actual=info.alloc_buffer_info;
  assert(info.private_data && actual.native_handle==info.private_data);
  assert(actual.fd>=0 && actual.size==predicted.size && actual.stride==predicted.stride);
  assert(actual.aligned_width==predicted.aligned_width && actual.aligned_height==predicted.aligned_height);
  assert(actual.format==predicted.format && actual.id);
  uint32_t stride[4]={},offset[4]={},planes=0;
  assert(allocator.GetBufferLayout(actual,stride,offset,&planes)==0);
  assert(planes && planes<=4);
  for (uint32_t p=0;p<planes;++p) assert(stride[p] && offset[p]<actual.size);
  auto *handle=static_cast<const native_handle_t *>(info.private_data);
  bool rgb=actual.format==sdm::kFormatRGBA8888 || actual.format==sdm::kFormatRGBX8888 ||
           actual.format==sdm::kFormatBGRA8888;
  if (rgb) {
    void *data=nullptr; ARect region{};
    assert(mapper->v5.lock(handle,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,region,-1,&data)==AIMAPPER_ERROR_NONE);
    assert(data); memset(data,0x69,size_t(actual.stride)*height*4);
    int fence=-1;
    assert(mapper->v5.unlock(handle,&fence)==AIMAPPER_ERROR_NONE);
    if(fence>=0)close(fence);
    assert(allocator.MapBuffer(reinterpret_cast<const private_handle_t *>(handle),nullptr)==0);
    assert(gralloc::GetRgbDataAddress(reinterpret_cast<private_handle_t *>(
      const_cast<native_handle_t *>(handle)),&data)==0 && data);
    for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width*4;++x)
      assert(static_cast<unsigned char *>(data)[size_t(y)*actual.stride*4+x]==0x69);
    fence=-1;assert(allocator.UnmapBuffer(reinterpret_cast<const private_handle_t *>(handle),&fence)==0);
    if(fence>=0)close(fence);
  }
  uint64_t id=actual.id;
  assert(allocator.FreeBuffer(&info)==0 && !info.private_data && !actual.native_handle && actual.fd==-1);
  assert(allocator.FreeBuffer(&info)==-EINVAL);
  printf("HWC_ALLOCATOR_PASS format=%d width=%u height=%u cache=%d planes=%u pixel_check=%d id=%llu\n",
    format,width,height,cache,planes,rgb,(unsigned long long)id);
}
int main() {
  setvbuf(stdout,nullptr,_IONBF,0);
  void *support=dlopen("libvndksupport.so",RTLD_NOW|RTLD_LOCAL);assert(support);
  auto load=reinterpret_cast<void *(*)(const char *,int)>(dlsym(support,"android_load_sphal_library"));assert(load);
  void *library=load("mapper.quest2.so",RTLD_NOW|RTLD_LOCAL);assert(library);
  auto get=reinterpret_cast<AIMapper_Error(*)(AIMapper **)>(dlsym(library,"AIMapper_loadIMapper"));assert(get);
  assert(get(&mapper)==AIMAPPER_ERROR_NONE && mapper->version==AIMAPPER_VERSION_5);
  sdm::HWCBufferAllocator allocator;
  assert(allocator.AllocateBuffer(nullptr)==-EINVAL);
  sdm::BufferInfo invalid{};assert(allocator.AllocateBuffer(&invalid)==-EINVAL);
  sdm::AllocatedBufferInfo no_handle{};uint32_t stride[4]={},offset[4]={},planes=0;
  assert(allocator.GetBufferLayout(no_handle,stride,offset,&planes)==-EINVAL);
  const sdm::LayerBufferFormat formats[]={
    sdm::kFormatRGBA8888,sdm::kFormatRGBX8888,sdm::kFormatBGRA8888,sdm::kFormatRGBA1010102,
    sdm::kFormatYCbCr420SemiPlanarVenus,sdm::kFormatYCbCr420P010,sdm::kFormatYCbCr420P010Venus,
    sdm::kFormatRGBA8888Ubwc,sdm::kFormatRGBX8888Ubwc,sdm::kFormatRGBA1010102Ubwc};
  const uint32_t widths[]={64,130,1280},heights[]={64,74,720};
  int warm=-1;
  for(unsigned cycle=0;cycle<2;++cycle) {
    for(auto f:formats)for(unsigned i=0;i<3;++i)for(bool cache:{true,false})
      check(allocator,f,widths[i],heights[i],cache);
    if(!cycle)warm=fd_count();else assert(fd_count()==warm);
  }
  FILE *maps=fopen("/proc/self/maps","r");assert(maps);char line[2048];
  while(fgets(line,sizeof(line),maps))
    assert(!strstr(line,"mapper@3.0-impl") && !strstr(line,"mapper@4.0-impl") &&
           !strstr(line,"mapper.minigbm.so"));
  fclose(maps);
  printf("HWC_ALLOCATOR_ALL_PASS buffers=120 pixel_checks=36 fd_count=%d hwc_service_verified=0\n",warm);
}
