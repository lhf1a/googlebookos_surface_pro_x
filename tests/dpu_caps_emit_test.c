/* Execute the actual kernel emitter with fault-injected DRM allocation hooks. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
_Static_assert(SSPP_SCALER_VER(1, 4) == 65540, "Use the real catalog scaler encoding");
typedef uint32_t u32;
#define PAGE_SIZE 4096
#define GFP_KERNEL 0
#define DRM_MODE_PROP_BLOB 16
#define DRM_MODE_PROP_IMMUTABLE 4
#define SSPP_TYPE_VIG 0
#define SSPP_TYPE_RGB 1
#define SSPP_TYPE_DMA 2
#define SSPP_TYPE_CURSOR 3
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
struct seq_buf {char *buffer; size_t size, len;};
static void seq_buf_init(struct seq_buf *b, char *p, size_t n) {b->buffer=p;b->size=n;b->len=0;}
static void seq_buf_printf(struct seq_buf *b, const char *f, ...) {
    va_list a;
    if (b->len >= b->size) return;
    va_start(a,f);
    int n=vsnprintf(b->buffer+b->len,b->size-b->len,f,a);
    va_end(a);
    b->len += n;
}
static bool seq_buf_has_overflowed(struct seq_buf *b) {return b->len>=b->size;}
static unsigned seq_buf_used(struct seq_buf *b) {return b->len;}
static int fail_alloc;
static void *kzalloc(size_t n, int unused) {return fail_alloc ? NULL : calloc(1,n);}
#define kfree free
struct drm_mode_object {u32 id;};
struct drm_device {int dummy;};
struct drm_plane {struct drm_device *dev;struct drm_mode_object base;};
struct drm_crtc {struct drm_device *dev;struct drm_mode_object base;};
struct drm_property_blob {struct drm_mode_object base;};
struct drm_property {int dummy;};
static int fail_blob,fail_property,fail_action,blob_refs,property_refs,attached;
static char captured[PAGE_SIZE];
static void (*release_blob)(struct drm_device *,void *);
static void *release_arg;
static struct drm_property_blob blob;
static struct drm_property property;
static struct drm_property_blob *drm_property_create_blob(struct drm_device *d, size_t n, const void *p) {
    if (fail_blob) return ERR_PTR(-ENOMEM);
    assert(n<=sizeof(captured));
    assert(((const char *)p)[n-1]=='\0');
    memcpy(captured,p,n); ++blob_refs; blob.base.id=123;
    return &blob;
}
static void drm_property_blob_put(void *p) {assert(p==&blob && blob_refs==1);--blob_refs;}
static struct drm_property *drm_property_create(struct drm_device *d,int flags,const char *name,int n) {
    assert(flags==(DRM_MODE_PROP_BLOB|DRM_MODE_PROP_IMMUTABLE));
    assert(!strcmp(name,"QCOM_DPU_CAPS") && n==0);
    if (fail_property) return NULL;
    ++property_refs;return &property;
}
static void drm_property_destroy(struct drm_device *d,struct drm_property *p) {
    assert(p==&property && property_refs==1);--property_refs;
}
static int drmm_add_action_or_reset(struct drm_device *d,void (*action)(struct drm_device *,void *),void *p) {
    if (fail_action) {action(d,p);return -ENOMEM;}
    release_blob=action;release_arg=p;return 0;
}
static void drm_object_attach_property(struct drm_mode_object *o,struct drm_property *p,u32 id) {
    assert(p==&property && id==123);++attached;
}
struct dpu_sspp_sub_blks {u32 max_per_pipe_bw;struct {u32 len,version;} scaler_blk;struct {u32 len;} csc_blk;};
struct dpu_sspp_cfg {u32 id,type;const struct dpu_sspp_sub_blks *sblk;};
struct dpu_hw_sspp {const struct dpu_sspp_cfg *cap;struct {void (*setup_scaler)(void),(*setup_csc)(void);} ops;};
struct dpu_caps {u32 max_linewidth,max_mixer_width,max_mixer_blendstages;bool has_src_split;};
struct dpu_mdss_version {unsigned char core_major_ver,core_minor_ver;};
struct dpu_perf_cfg {
    u32 max_bw_low,max_bw_high,clk_inefficiency_factor,bw_inefficiency_factor;
    u32 min_core_ib,min_llcc_ib,min_dram_ib;
    u32 undersized_prefill_lines,xtra_prefill_lines,dest_scale_prefill_lines;
    u32 macrotile_prefill_lines,yuv_nv12_prefill_lines,linear_prefill_lines;
    u32 downscaling_prefill_lines,amortizable_threshold,min_prefill_lines;
};
struct dpu_mdss_cfg {const struct dpu_caps *caps;const struct dpu_perf_cfg *perf;const struct dpu_mdss_version *mdss_ver;};
struct qcom_ubwc_cfg_data {u32 ubwc_enc_version,ubwc_dec_version;};
struct dpu_kms {const struct dpu_mdss_cfg *catalog;const struct qcom_ubwc_cfg_data *mdss;u32 core_rev;struct {unsigned long long max_core_clk_rate;} perf;};
#include DPU_CAPS_HEADER
static void op(void) {}
static void cleanup(void) {
    if (release_blob) {release_blob(NULL,release_arg);release_blob=NULL;release_arg=NULL;}
    if (property_refs) drm_property_destroy(NULL,&property);
    assert(blob_refs==0 && property_refs==0);
}
int main(void) {
    struct drm_device dev={0};
    struct drm_plane plane={.dev=&dev};
    struct drm_crtc crtc={.dev=&dev};
    struct dpu_caps caps={.max_linewidth=2671,.max_mixer_width=2049,.max_mixer_blendstages=7,.has_src_split=true};
    struct dpu_mdss_version version={5,1};
    struct dpu_perf_cfg perf={.max_bw_low=123456,.max_bw_high=234567,.clk_inefficiency_factor=105,.bw_inefficiency_factor=120,
        .min_core_ib=11,.min_llcc_ib=12,.min_dram_ib=13,.undersized_prefill_lines=1,.xtra_prefill_lines=2,
        .dest_scale_prefill_lines=3,.macrotile_prefill_lines=4,.yuv_nv12_prefill_lines=5,.linear_prefill_lines=6,
        .downscaling_prefill_lines=7,.amortizable_threshold=8,.min_prefill_lines=9};
    struct dpu_mdss_cfg catalog={&caps,&perf,&version};
    struct qcom_ubwc_cfg_data ubwc={0x20000000,0x30000000};
    struct dpu_kms kms={.catalog=&catalog,.mdss=&ubwc,.core_rev=0x50010002,.perf.max_core_clk_rate=600000001};
    struct dpu_sspp_sub_blks blocks={.max_per_pipe_bw=456789,.scaler_blk={.len=256,.version=SSPP_SCALER_VER(1, 4)},.csc_blk={.len=32}};
    struct dpu_sspp_cfg cfg={.id=4,.type=SSPP_TYPE_VIG,.sblk=&blocks};
    struct dpu_hw_sspp pipe={.cap=&cfg,.ops={op,op}};
    assert(dpu_caps_add_plane(&plane,&kms,&pipe,20,4,3277,262144)==0);
    assert(strstr(captured,"physical_pipe=1\npipe_id=4\npipe_type=vig\n"));
    assert(strstr(captured,"max_linewidth=2671\nmax_upscale=20\nmax_downscale=4\n"));
    assert(strstr(captured,"min_scale_16_16=3277\nmax_scale_16_16=262144\n"));
    assert(strstr(captured,"max_per_pipe_bw_kbps=456789\n"));
    assert(strstr(captured,"scaler_hw_version=65540\n"));
    assert(strstr(captured,"kernel_managed_scaler=1\nkernel_managed_csc=1\ncsc_bt601_limited_only=1\n"));
    cleanup();
    pipe.ops.setup_scaler=NULL; pipe.ops.setup_csc=NULL;
    assert(dpu_caps_add_plane(&plane,&kms,&pipe,20,4,3277,262144)==0);
    assert(strstr(captured,"scaler=0\ncsc=0\n"));
    assert(strstr(captured,"max_upscale=1\nmax_downscale=1\n"));
    assert(strstr(captured,"min_scale_16_16=65536\nmax_scale_16_16=65536\n"));
    cleanup();
    assert(dpu_caps_add_plane(&plane,&kms,NULL,20,4,3277,262144)==0);
    assert(!strcmp(captured,"version=1\nobject=plane\nphysical_pipe=0\n"));
    cleanup();
    assert(dpu_caps_add_crtc(&crtc,&kms)==0);
    assert(strstr(captured,"hw_major=5\nhw_minor=1\nhw_version=1342242818\n"));
    assert(strstr(captured,"max_mixer_width=2049\nmax_blendstages=7\nhas_src_split=1\n"));
    assert(strstr(captured,"max_bw_low_kbps=123456\nmax_bw_high_kbps=234567\n"));
    assert(strstr(captured,"max_core_clk_hz=600000001\n"));
    assert(strstr(captured,"ubwc_version=536870912\nubwc_dec_version=805306368\n"));
    assert(strstr(captured,"yuv_nv12_prefill_lines=5\n"));
    assert(!strstr(captured,"has_hdr") && !strstr(captured,"secure") && !strstr(captured,"pixel_formats"));
    cleanup();
    for (int mode=0;mode<4;++mode) {
        int before=attached;
        fail_alloc=mode==0;fail_blob=mode==1;fail_property=mode==2;fail_action=mode==3;
        assert(dpu_caps_add_crtc(&crtc,&kms)==-ENOMEM);
        assert(attached==before);cleanup();
    }
    fail_alloc=fail_blob=fail_property=fail_action=0;
    int before=attached;
    kms.perf.max_core_clk_rate=0;
    assert(dpu_caps_add_crtc(&crtc,&kms)==-EINVAL);
    assert(attached==before);cleanup();
    struct seq_buf overflow={.buffer=captured,.size=1,.len=1};
    assert(dpu_caps_attach(&dev,&crtc.base,&overflow)==-EOVERFLOW);
    puts("DPU_CAPS_EMIT_PASS physical/virtual/scaler/CSC/CRTC/ownership/failures");
}
