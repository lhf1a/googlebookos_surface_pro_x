/* Exercise the patched SM8350 composer class against real AHB/mapper5 handles. */
#include "QtiComposerHandleImporter.h"
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <system/window.h>
#include <android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
using vendor::qti::hardware::display::composer::V3_0::ComposerHandleImporter;
static AIMapper *mapper;
static PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC client_buffer;
using Metadata = aidl::android::hardware::graphics::common::StandardMetadataType;
static int fd_count() {
  DIR *dir = opendir("/proc/self/fd"); assert(dir);
  int n = 0; while (readdir(dir)) ++n; closedir(dir); return n;
}
template<Metadata T> static uint64_t metadata(buffer_handle_t handle) {
  int32_t size = mapper->v5.getStandardMetadata(handle, int64_t(T), nullptr, 0);
  assert(size > 0 && size < 4096);
  std::vector<uint8_t> bytes(size);
  assert(mapper->v5.getStandardMetadata(handle, int64_t(T), bytes.data(), bytes.size()) == size);
  auto value = android::hardware::graphics::mapper::StandardMetadata<T>::value::decode(
      bytes.data(), bytes.size());
  assert(value);
  return *value;
}
static void check(ComposerHandleImporter &importer, uint32_t format, unsigned width,
                  unsigned height, bool ubwc) {
  AHardwareBuffer_Desc desc{};
  desc.width = width; desc.height = height; desc.layers = 1; desc.format = format;
  desc.usage = ubwc ? 0x10000900 : 0xb00;
  bool pixels = !ubwc && (format == 1 || format == 2 || format == 5);
  if (pixels) desc.usage |= AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                            AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
  AHardwareBuffer *buffer = nullptr;
  assert(!AHardwareBuffer_allocate(&desc, &buffer) && buffer);
  AHardwareBuffer_describe(buffer, &desc);
  if (pixels) {
    void *data = nullptr;
    assert(!AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &data));
    memset(data, 0x5a, size_t(desc.stride) * height * 4);
    assert(!AHardwareBuffer_unlock(buffer, nullptr));
  }
  const auto *window = static_cast<ANativeWindowBuffer_t *>(client_buffer(buffer));
  assert(window);
  buffer_handle_t first = window->handle, second = first;
  assert(importer.importBuffer(first) && first);
  assert(importer.importBuffer(second) && second && first != second);
  // Standard metadata IDs are fixed by graphics.common StandardMetadataType.
  auto id = metadata<Metadata::BUFFER_ID>(first);
  assert(metadata<Metadata::BUFFER_ID>(second) == id);
  AHardwareBuffer_release(buffer);
  importer.freeBuffer(first);
  assert(metadata<Metadata::BUFFER_ID>(second) == id);
  assert(metadata<Metadata::WIDTH>(second) == width && metadata<Metadata::HEIGHT>(second) == height);
  assert(metadata<Metadata::PIXEL_FORMAT_MODIFIER>(second) == (ubwc ? 0x0500000000000001ULL : 0));
  if (pixels) {
    void *data = nullptr;
    ARect rect{0, 0, int32_t(width), int32_t(height)};
    assert(mapper->v5.lock(second, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                           rect, -1, &data) == AIMAPPER_ERROR_NONE);
    for (unsigned y = 0; y < height; ++y)
      for (unsigned x = 0; x < width * 4; ++x)
        assert(static_cast<unsigned char *>(data)[size_t(y) * desc.stride * 4 + x] == 0x5a);
    int fence = -1;
    assert(mapper->v5.unlock(second, &fence) == AIMAPPER_ERROR_NONE);
    if (fence >= 0) close(fence);
  }
  importer.freeBuffer(second);
  printf("QTI_COMPOSER_IMPORT_PASS format=%u width=%u height=%u ubwc=%d retained_pixel_check=%d\n",
         format, width, height, ubwc, pixels);
}
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  void *support = dlopen("libvndksupport.so", RTLD_NOW | RTLD_LOCAL); assert(support);
  auto load = reinterpret_cast<void *(*)(const char *, int)>(
      dlsym(support, "android_load_sphal_library")); assert(load);
  void *library = load("mapper.quest2.so", RTLD_NOW | RTLD_LOCAL); assert(library);
  auto get = reinterpret_cast<AIMapper_Error (*)(AIMapper **)>(
      dlsym(library, "AIMapper_loadIMapper")); assert(get);
  assert(get(&mapper) == AIMAPPER_ERROR_NONE && mapper->version == AIMAPPER_VERSION_5);
  client_buffer = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
      eglGetProcAddress("eglGetNativeClientBufferANDROID")); assert(client_buffer);
  ComposerHandleImporter importer;
  buffer_handle_t empty = nullptr; assert(importer.importBuffer(empty) && !empty);
  native_handle_t *zero = native_handle_create(0, 0); assert(zero);
  empty = zero; assert(importer.importBuffer(empty) && !empty); native_handle_delete(zero);
  native_handle_t *bad = native_handle_create(0, 1); assert(bad);
  bad->data[0] = 0; buffer_handle_t malformed = bad;
  assert(!importer.importBuffer(malformed) && malformed == bad); native_handle_delete(bad);
  const uint32_t formats[] = {1, 2, 5, 43, 22, 0x7fa30c04, 0x36};
  const unsigned widths[] = {64, 130, 1280}, heights[] = {64, 74, 720};
  int warm = -1;
  for (unsigned run = 0; run < 2; ++run) {
    for (auto f : formats) for (unsigned i = 0; i < 3; ++i)
      check(importer, f, widths[i], heights[i], false);
    for (auto f : {1U, 2U, 43U, 22U}) for (unsigned i = 0; i < 3; ++i)
      check(importer, f, widths[i], heights[i], true);
    if (!run) warm = fd_count(); else assert(fd_count() == warm);
  }
  importer.cleanup();
  importer.initialize();
  importer.cleanup();
  FILE *maps = fopen("/proc/self/maps", "r"); assert(maps); char line[2048];
  while (fgets(line, sizeof(line), maps))
    assert(!strstr(line, "mapper@3.0-impl") && !strstr(line, "mapper@4.0-impl"));
  fclose(maps);
  printf("QTI_COMPOSER_IMPORT_ALL_PASS buffers=66 imports=132 retained_pixel_checks=18 fd_count=%d legacy_mapper_implementation=0 hwc_service_verified=0\n", warm);
}
