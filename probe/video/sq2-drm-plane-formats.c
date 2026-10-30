/* Read-only KMS format inventory. No master, framebuffer or modeset calls. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <drm.h>
#include <drm_mode.h>
int main(int argc,char **argv){
 if(argc!=2)return 2;
 int fd=open(argv[1],O_RDONLY|O_CLOEXEC);if(fd<0){perror("open");return 1;}
 struct drm_set_client_cap cap={.capability=DRM_CLIENT_CAP_UNIVERSAL_PLANES,.value=1};
 if(ioctl(fd,DRM_IOCTL_SET_CLIENT_CAP,&cap)){perror("universal planes");close(fd);return 1;}
 struct drm_mode_get_plane_res res={0};
 if(ioctl(fd,DRM_IOCTL_MODE_GETPLANERESOURCES,&res)||!res.count_planes||res.count_planes>128){close(fd);return 1;}
 uint32_t ids[128]={0};res.plane_id_ptr=(uintptr_t)ids;
 if(ioctl(fd,DRM_IOCTL_MODE_GETPLANERESOURCES,&res)||res.count_planes>128){close(fd);return 1;}
 for(unsigned i=0;i<res.count_planes;i++){
  struct drm_mode_get_plane p={.plane_id=ids[i]};uint32_t formats[256]={0};
  if(ioctl(fd,DRM_IOCTL_MODE_GETPLANE,&p)||p.count_format_types>256){close(fd);return 1;}
  p.format_type_ptr=(uintptr_t)formats;
  if(ioctl(fd,DRM_IOCTL_MODE_GETPLANE,&p)||p.count_format_types>256){close(fd);return 1;}
  printf("PLANE id=%u crtc_mask=%x formats=%u",p.plane_id,p.possible_crtcs,p.count_format_types);
  for(unsigned j=0;j<p.count_format_types;j++)printf(" %c%c%c%c",formats[j]&255,(formats[j]>>8)&255,(formats[j]>>16)&255,(formats[j]>>24)&255);
  puts("");
 }
 close(fd);return 0;
}
