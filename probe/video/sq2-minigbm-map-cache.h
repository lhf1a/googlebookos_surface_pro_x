/* Private MSM/NV12 experiment. Keep one mapping reference until BO destruction.
 * Caller holds mappings_lock. Existing unlock and destroy paths still run.
 * Only WC buffers from the verified MSM allocator are in the experiment scope.
 */
static bool sq2_cache_eligible(struct bo *bo, struct mapping *mapping)
{
 const char *enabled = getenv("SQ2_VIDEO_MAP_CACHE");
 return enabled && !strcmp(enabled, "1") &&
  !strcmp(bo->drv->backend->name, "msm") &&
  !bo->drv->backend->bo_flush && !bo->meta.cached &&
  bo->meta.format == DRM_FORMAT_NV12 && !bo->meta.format_modifier &&
  !(bo->meta.use_flags & BO_USE_PROTECTED) &&
  mapping->vma->map_flags == BO_MAP_READ_WRITE &&
  mapping->vma->length && mapping->vma->length <= 16u*1024*1024;
}

static bool sq2_cache_mapping(struct bo *bo, struct mapping *mapping)
{
 if (!sq2_cache_eligible(bo, mapping)) return false;
 if (!mapping->sq2_cached) {
  size_t bytes=0, count=0;
  for (uint32_t i=0; i<drv_array_size(bo->drv->mappings); ++i) {
   struct mapping *p=drv_array_at_idx(bo->drv->mappings,i);
   if (p->sq2_cached) { bytes+=p->vma->length; ++count; }
  }
  if (count>=16 || bytes>192u*1024*1024-mapping->vma->length ||
      mapping->refcount==UINT32_MAX) return false;
  ++mapping->refcount; /* private cache reference; ordinary unlock drops active ref */
  mapping->sq2_cached=true;
 }
 /* WC writes must complete before publishing the buffer to its consumer.
  * Acquire/release fences and normal mapper lock/unlock are not bypassed.
  */
#if defined(__aarch64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 __sync_synchronize();
#endif
 return true;
}
