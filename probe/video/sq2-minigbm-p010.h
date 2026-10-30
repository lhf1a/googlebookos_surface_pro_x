/* SQ2 KMS video planes expose linear P010. Keep other chip tables unchanged. */
#ifndef SQ2_MINIGBM_P010_H
#define SQ2_MINIGBM_P010_H
static bool sq2_p010_scanout_chip(int fd)
{
 struct drm_msm_param param = {.pipe=MSM_PIPE_3D0,.param=MSM_PARAM_CHIP_ID};
 return drmIoctl(fd,DRM_IOCTL_MSM_GET_PARAM,&param)==0 &&
        param.value==0x06080001ULL;
}
#endif
