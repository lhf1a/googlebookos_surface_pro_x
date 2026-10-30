// SPDX-License-Identifier: GPL-2.0-only
/* Explicit lab-only KMS test. Never install or invoke at normal boot.
 * Requires stopped Android composer/SF, an independent reboot watchdog, and
 * the separately validated 5.4-Gbit/s candidate. Vblank is not optical proof.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <drm.h>
#include <drm_mode.h>
#include <drm_fourcc.h>

static double now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) abort();
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}

static struct drm_mode_modeinfo native_mode(unsigned hz)
{
    struct drm_mode_modeinfo m = {0};
    m.clock = 5925 * hz;
    m.hdisplay=2880; m.hsync_start=2928; m.hsync_end=2960; m.htotal=3000;
    m.vdisplay=1920; m.vsync_start=1941; m.vsync_end=1946; m.vtotal=1975;
    m.vrefresh=hz; m.flags=DRM_MODE_FLAG_NHSYNC|DRM_MODE_FLAG_NVSYNC;
    m.type=DRM_MODE_TYPE_USERDEF;
    snprintf(m.name,sizeof(m.name),"2880x1920-%u",hz);
    return m;
}

static int set_mode(int fd, uint32_t fb, unsigned hz)
{
    uint32_t connector=32;
    struct drm_mode_crtc c = {0};
    c.set_connectors_ptr=(uintptr_t)&connector; c.count_connectors=1;
    c.crtc_id=81; c.fb_id=fb; c.mode_valid=1; c.mode=native_mode(hz);
    printf("BEFORE_SET_MODE hz=%u clock_khz=%u\n",hz,c.mode.clock);
    (void)fsync(STDOUT_FILENO);
    errno=0;
    int r=ioctl(fd,DRM_IOCTL_MODE_SETCRTC,&c);
    printf("SET_MODE hz=%u clock_khz=%u rc=%d errno=%d (%s)\n",
           hz,c.mode.clock,r,errno,strerror(errno));
    return r;
}

static int sample(int fd, unsigned hz)
{
    union drm_wait_vblank v = {0};
    v.request.type=_DRM_VBLANK_RELATIVE; v.request.sequence=hz*2;
    printf("BEFORE_VBLANK hz=%u requested_count=%u\n",hz,hz*2);
    (void)fsync(STDOUT_FILENO);
    double start=now(); errno=0;
    int r=ioctl(fd,DRM_IOCTL_WAIT_VBLANK,&v);
    double elapsed=now()-start;
    printf("VBLANK hz=%u requested_count=%u rc=%d errno=%d elapsed_s=%.6f sequence=%u\n",
           hz,hz*2,r,errno,elapsed,v.reply.sequence);
    /* Loose timing check only; this is not a frame-skipping certification. */
    return r || elapsed<1.7 || elapsed>2.3 ? -1 : 0;
}

/* A root-only session directory is prepared by the lab runner. Control is
 * supplied only after the operator confirms the preceding visible stage. */
static void session_phase(const char *phase)
{
    printf("SESSION_PHASE %s\n",phase);
    (void)fsync(STDOUT_FILENO);
    int fd=open("/tmp/sq2-edp-session/phase",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if (fd<0) return;
    ssize_t written=write(fd,phase,strlen(phase));
    if (written!=(ssize_t)strlen(phase)) perror("session phase write");
    (void)fsync(fd); close(fd);
}

static int session_command(const char *expected,unsigned seconds)
{
    double deadline=now()+seconds;
    while(now()<deadline) {
        int fd=open("/tmp/sq2-edp-session/command",O_RDONLY|O_NOFOLLOW);
        if (fd>=0) {
            char b[32]={0}; ssize_t n=read(fd,b,sizeof(b)-1); close(fd);
            if (n>0) {
                b[strcspn(b,"\r\n")]=0;
                unlink("/tmp/sq2-edp-session/command");
                printf("SESSION_COMMAND %s\n",b);
                return strcmp(b,expected) ? -1 : 0;
            }
        }
        usleep(200000);
    }
    puts("SESSION_CONTROL_TIMEOUT_NO_REBOOT");
    return -1;
}

static int session_stage(int fd,uint32_t fb,unsigned hz)
{
    int result=set_mode(fd,fb,hz);
    if (!result) {
        sleep(1); result=sample(fd,hz);
        char phase[32]; snprintf(phase,sizeof(phase),"VISUAL_%u",hz);
        session_phase(phase);
        printf("VISUAL_WINDOW_COLOR_BARS hz=%u seconds=20\n",hz);
        sleep(20);
    }
    int restore=set_mode(fd,fb,60);
    int vblank=restore ? -1 : sample(fd,60);
    printf("SESSION_STAGE_END hz=%u test_rc=%d restore60_rc=%d restore60_sample=%d\n",hz,result,restore,vblank);
    return result || restore || vblank ? -1 : 0;
}

int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    if (argc!=2 || (strcmp(argv[1],"--inspect") && strcmp(argv[1],"--step-session") && strcmp(argv[1],"60") &&
                    strcmp(argv[1],"65") && strcmp(argv[1],"70") && strcmp(argv[1],"75") &&
                    strcmp(argv[1],"90") && strcmp(argv[1],"120"))) return 64;
    int fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
    if (fd<0) { perror("open"); return 1; }
    struct drm_mode_crtc old={.crtc_id=81};
    struct drm_mode_get_connector connector={.connector_id=32};
    if (ioctl(fd,DRM_IOCTL_MODE_GETCRTC,&old) ||
        ioctl(fd,DRM_IOCTL_MODE_GETCONNECTOR,&connector)) {
        perror("inspect"); close(fd); return 1;
    }
    printf("CURRENT connector=%u connected=%u crtc=%u active=%u %ux%u clock=%u hz=%u fb=%u\n",
           connector.connector_id,connector.connection,old.crtc_id,old.mode_valid,
           old.mode.hdisplay,old.mode.vdisplay,old.mode.clock,old.mode.vrefresh,old.fb_id);
    if (!strcmp(argv[1],"--inspect")) { close(fd); return 0; }
    if (connector.connection!=1) { close(fd); return 1; }
    if (ioctl(fd,DRM_IOCTL_SET_MASTER,0)) { perror("SET_MASTER"); close(fd); return 1; }
    struct drm_mode_create_dumb d={.width=2880,.height=1920,.bpp=32};
    struct drm_mode_map_dumb map={0};
    struct drm_mode_fb_cmd2 fb={.width=2880,.height=1920,.pixel_format=DRM_FORMAT_XRGB8888};
    if (ioctl(fd,DRM_IOCTL_MODE_CREATE_DUMB,&d)) { perror("CREATE_DUMB"); close(fd); return 1; }
    if (d.pitch<2880*4 || d.size<d.pitch*1920ULL || d.size>64*1024*1024ULL) {
        close(fd); return 1;
    }
    map.handle=d.handle;
    if (ioctl(fd,DRM_IOCTL_MODE_MAP_DUMB,&map)) { perror("MAP_DUMB"); close(fd); return 1; }
    uint8_t *pixels=mmap(NULL,d.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,map.offset);
    if (pixels==MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    const uint32_t colors[]={0xffffff,0xffff00,0x00ffff,0x00ff00,0xff00ff,0xff0000,0x0000ff,0x101010};
    for (unsigned y=0;y<1920;y++) for(unsigned x=0;x<2880;x++) {
        uint32_t color=colors[x/360];
        if (y>1500) color=((x/16+y/16)&1)?0xffffff:0;
        ((uint32_t*)(pixels+y*d.pitch))[x]=color;
    }
    fb.handles[0]=d.handle; fb.pitches[0]=d.pitch;
    if (ioctl(fd,DRM_IOCTL_MODE_ADDFB2,&fb)) { perror("ADDFB2"); munmap(pixels,d.size); close(fd); return 1; }
    if (!strcmp(argv[1],"--step-session")) {
        int rc=1;
        if (!set_mode(fd,fb.fb_id,60) && !sample(fd,60) &&
            !session_stage(fd,fb.fb_id,70)) {
            session_phase("WAIT_70_CONFIRM_NATIVE60");
            if (!session_command("next75",120) && !session_stage(fd,fb.fb_id,75)) {
                session_phase("WAIT_75_CONFIRM_NATIVE60");
                rc=session_command("finish",120) ? 1 : 0;
            }
        }
        /* Keep the active framebuffer alive across DROP_MASTER and Android's
         * takeover; closing an active test FB before composer starts is avoided. */
        if (ioctl(fd,DRM_IOCTL_DROP_MASTER,0)) { perror("DROP_MASTER"); rc=1; }
        session_phase("HANDOFF_READY_NATIVE60");
        if (session_command("release",90)) rc=1;
        munmap(pixels,d.size); close(fd);
        printf("SESSION_FINISHED rc=%d NO_AUTOMATIC_REBOOT\n",rc);
        return rc;
    }
    unsigned hz=(unsigned)strtoul(argv[1],NULL,10);
    int result=1;
    if (!set_mode(fd,fb.fb_id,60) && !sample(fd,60) && !set_mode(fd,fb.fb_id,hz)) {
        sleep(1);
        result=sample(fd,hz)?1:0;
        printf("VISUAL_WINDOW_COLOR_BARS seconds=%u (optical result requires human observation)\n",hz==60?3:20);
        sleep(hz==60?3:20);
    }
    int restore=set_mode(fd,fb.fb_id,60);
    int restored_vblank=restore ? -1 : sample(fd,60);
    printf("RESTORE_NATIVE60 mode_rc=%d sample_rc=%d\n",restore,restored_vblank);
    munmap(pixels,d.size);
    /* Do not remove an active framebuffer before composer resumes. The closing
     * DRM file releases test handles; Android re-establishes its own scanout. */
    close(fd);
    return result || restore || restored_vblank ? 1 : 0;
}
