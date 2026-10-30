/* Lab-only allocator dependency selection. Does not alter other processes. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void){
 /* Keep Android's default namespace links for Binder and system libraries. */
 if(setenv("LD_LIBRARY_PATH","/vendor/lib64/sq2-p010-allocator",1))return 2;
 unsetenv("LD_PRELOAD");unsetenv("SQ2_VIDEO_MAP_CACHE");
 execl("/vendor/bin/hw/sq2-allocator-original","/vendor/bin/hw/android.hardware.graphics.allocator-service.minigbm",NULL);
 perror("exec allocator");return 3;
}
