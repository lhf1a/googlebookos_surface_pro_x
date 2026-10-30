#include <assert.h>
#define main sq2_edp_probe_main
#include "../probe/graphics/sq2-edp-mode-test.c"
#undef main

int main(void)
{
    unsigned rates[]={60,65,70,75,90,120};
    unsigned clocks[]={355500,385125,414750,444375,533250,711000};
    for (unsigned i=0;i<6;i++) {
        struct drm_mode_modeinfo m=native_mode(rates[i]);
        assert(m.clock==clocks[i]);
        assert(m.hdisplay==2880 && m.vdisplay==1920);
        assert(m.htotal==3000 && m.vtotal==1975);
        assert((uint64_t)m.clock*1000==(uint64_t)m.htotal*m.vtotal*rates[i]);
        assert(m.flags==(DRM_MODE_FLAG_NHSYNC|DRM_MODE_FLAG_NVSYNC));
    }
    char *invalid[]={"probe","144",NULL};
    assert(sq2_edp_probe_main(2,invalid)==64);
    puts("PASS: native totals/polarities, 60/65/70/75/90/120 clocks, unsupported input rejected before DRM open");
    return 0;
}
