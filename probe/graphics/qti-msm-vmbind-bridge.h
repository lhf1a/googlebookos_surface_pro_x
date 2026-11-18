/* SPDX-License-Identifier: MIT */
#ifndef QTI_MSM_VMBIND_BRIDGE_H
#define QTI_MSM_VMBIND_BRIDGE_H
#include <stdbool.h>
/* Explicit experimental opt-in only; no change to the Vulkan router choice. */
bool qti_vm_app_selected(unsigned uid,const char *process,const char *package);
int qti_vm_gsl_open(const char *path,int flags,...);
int qti_vm_gsl_open2(const char *path,int flags);
int qti_vm_gsl_close(int fd);
int qti_vm_gsl_dup(int fd);
int qti_vm_gsl_fcntl(int fd,int command,...);
int qti_vm_gsl_ioctl(int fd,unsigned long request,...);
#endif
