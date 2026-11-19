/* SPDX-License-Identifier: MIT */
#ifndef QTI_MSM_VMBIND_BRIDGE_H
#define QTI_MSM_VMBIND_BRIDGE_H
#include <stdbool.h>
/* Normal Android app UID only; Vulkan router selection is unchanged. */
bool qti_vm_app_uid(unsigned uid);
int qti_vm_gsl_open(const char *path,int flags,...);
int qti_vm_gsl_open2(const char *path,int flags);
int qti_vm_gsl_close(int fd);
int qti_vm_gsl_dup(int fd);
int qti_vm_gsl_fcntl(int fd,int command,...);
int qti_vm_gsl_ioctl(int fd,unsigned long request,...);
#endif
