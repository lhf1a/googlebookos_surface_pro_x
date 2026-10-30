/* Host-only fake syscalls: exercise actual session ordering without DRM/files. */
#define main probe_main
#define open mock_open
#define close mock_close
#define ioctl mock_ioctl
#define mmap mock_mmap
#define munmap mock_munmap
#define read mock_read
#define write mock_write
#define fsync mock_fsync
#define unlink mock_unlink
#define sleep mock_sleep
#define usleep mock_usleep
#define clock_gettime mock_clock_gettime
#include "../probe/graphics/sq2-edp-mode-test.c"
#undef main
#include <assert.h>
#include <stdarg.h>

static double tick;
static int commands,drop_master,seen70,seen75,closed,abort70;
static unsigned current_hz;
int mock_open(const char *p,int flags,...)
{
    (void)flags;
    if (!strcmp(p,"/dev/dri/card0")) return 10;
    if (strstr(p,"/phase")) return 11;
    if (strstr(p,"/command")) return 12;
    assert(0); return -1;
}
int mock_close(int fd)
{
    if (fd==10) { assert(drop_master && current_hz==60); assert(commands==(abort70?2:3)); closed=1; }
    return 0;
}
int mock_ioctl(int fd,unsigned long req,...)
{
    assert(fd==10);
    if (req==DRM_IOCTL_SET_MASTER) return 0;
    if (req==DRM_IOCTL_DROP_MASTER) {drop_master=1; return 0;}
    va_list ap; va_start(ap,req); void *p=va_arg(ap,void*); va_end(ap);
    if (req==DRM_IOCTL_MODE_GETCRTC) {struct drm_mode_crtc *c=p; c->mode_valid=1;c->mode=native_mode(60);c->fb_id=91;}
    else if (req==DRM_IOCTL_MODE_GETCONNECTOR) {struct drm_mode_get_connector *c=p;c->connection=1;}
    else if (req==DRM_IOCTL_MODE_CREATE_DUMB) {struct drm_mode_create_dumb *d=p;d->pitch=11520;d->size=11520*1920;d->handle=1;}
    else if (req==DRM_IOCTL_MODE_MAP_DUMB) {}
    else if (req==DRM_IOCTL_MODE_ADDFB2) {struct drm_mode_fb_cmd2 *f=p;f->fb_id=100;}
    else if (req==DRM_IOCTL_MODE_SETCRTC) {struct drm_mode_crtc *c=p;current_hz=c->mode.vrefresh;if(current_hz==70)seen70=1;if(current_hz==75)seen75=1;}
    else if (req==DRM_IOCTL_WAIT_VBLANK) {union drm_wait_vblank *v=p;assert(v->request.sequence==current_hz*2);tick+=2;v->reply.sequence=123;}
    else assert(0);
    return 0;
}
void *mock_mmap(void *p,size_t n,int prot,int flags,int fd,off_t off)
{(void)p;(void)prot;(void)flags;(void)fd;(void)off;return calloc(1,n);}
int mock_munmap(void *p,size_t n) {(void)n;assert(drop_master);free(p);return 0;}
ssize_t mock_read(int fd,void *p,size_t n)
{
    (void)n;assert(fd==12 && current_hz==60);
    const char *command;
    if (commands==0) {assert(seen70 && !seen75 && !drop_master);command=abort70?"abort":"next75";}
    else if (!abort70 && commands==1) {assert(seen75 && !drop_master);command="finish";}
    else {assert(drop_master);command="release";}
    commands++;strcpy(p,command);return strlen(command);
}
ssize_t mock_write(int fd,const void *p,size_t n) {(void)p;assert(fd==11);return n;}
int mock_fsync(int fd) {(void)fd;return 0;}
int mock_unlink(const char *p) {(void)p;return 0;}
unsigned mock_sleep(unsigned n) {tick+=n;return 0;}
int mock_usleep(useconds_t n) {tick+=n/1000000.;return 0;}
int mock_clock_gettime(clockid_t id,struct timespec *t) {(void)id;t->tv_sec=(time_t)tick;t->tv_nsec=(long)((tick-t->tv_sec)*1e9);return 0;}
int main(int argc,char **argv)
{
    abort70=argc==2 && !strcmp(argv[1],"abort");
    char *args[]={"probe","--step-session",NULL};
    int rc=probe_main(2,args);
    assert(closed && seen70 && seen75==!abort70);
    assert(rc==(abort70?1:0));
    puts("PASS: stage gate, restore60 between stages, DROP_MASTER before release, retained FB until handoff; no reboot syscall");
    return 0;
}
