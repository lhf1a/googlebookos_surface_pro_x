/* Independent allocation probe; no display commit and no global library swap. */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include "drv.h"
int main(int argc,char **argv){
 if(argc!=3)return 2;setbuf(stdout,NULL);alarm(15);
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 1;}
 struct driver *(*create)(int)=dlsym(lib,"drv_create");
 void (*destroy)(struct driver*)=dlsym(lib,"drv_destroy");
 struct combination *(*combination)(struct driver*,uint32_t,uint64_t)=dlsym(lib,"drv_get_combination");
 struct bo *(*allocate)(struct driver*,uint32_t,uint32_t,uint32_t,uint64_t)=dlsym(lib,"drv_bo_create");
 void (*release)(struct bo*)=dlsym(lib,"drv_bo_destroy");
 if(!create||!destroy||!combination||!allocate||!release)return 2;
 int fd=open(argv[2],O_RDWR|O_CLOEXEC);if(fd<0){perror("open");return 1;}
 struct driver *drv=create(fd);if(!drv){close(fd);return 1;}
 uint64_t usage=BO_USE_SW_READ_OFTEN|BO_USE_SW_WRITE_OFTEN|BO_USE_TEXTURE|BO_USE_SCANOUT;
 int supported=combination(drv,DRM_FORMAT_P010,usage)!=NULL;
 printf("P010_SURFACE_USAGE_SUPPORTED=%d\n",supported);
 struct bo *bo=supported?allocate(drv,320,240,DRM_FORMAT_P010,usage):NULL;
 printf("P010_SURFACE_ALLOCATED=%d\n",bo!=NULL);
 int result=bo?0:3;if(bo)release(bo);destroy(drv);close(fd);dlclose(lib);puts("RELEASED");return result;
}
