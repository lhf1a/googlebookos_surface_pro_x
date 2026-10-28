/* Included only by the separate Quest 2 test APK. No system driver updates. */
#include <adrenotools/driver.h>
#include <android/asset_manager.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#define CLOSED_VK_FUNCTIONS(X) \
 X(AcquireNextImageKHR) X(AllocateCommandBuffers) X(BeginCommandBuffer) \
 X(CmdBeginRenderPass) X(CmdBindPipeline) X(CmdDraw) X(CmdEndRenderPass) \
 X(CreateAndroidSurfaceKHR) X(CreateCommandPool) X(CreateDevice) X(CreateFence) \
 X(CreateFramebuffer) X(CreateGraphicsPipelines) X(CreateImageView) X(CreateInstance) \
 X(CreatePipelineLayout) X(CreateRenderPass) X(CreateSemaphore) X(CreateShaderModule) X(CreateSwapchainKHR) \
 X(DestroyCommandPool) X(DestroyDevice) X(DestroyFence) X(DestroyFramebuffer) X(DestroyImageView) \
 X(DestroyInstance) X(DestroyPipeline) X(DestroyPipelineLayout) X(DestroyRenderPass) X(DestroySemaphore) \
 X(DestroyShaderModule) X(DestroySurfaceKHR) X(DestroySwapchainKHR) X(DeviceWaitIdle) X(EndCommandBuffer) \
 X(EnumeratePhysicalDevices) X(GetDeviceQueue) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
 X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR) X(GetPhysicalDeviceSurfaceSupportKHR) \
 X(GetSwapchainImagesKHR) X(QueuePresentKHR) X(QueueSubmit) X(ResetCommandBuffer) X(ResetFences) X(WaitForFences)
#define CLOSED_DECLARE(n) static PFN_vk##n vk##n;
CLOSED_VK_FUNCTIONS(CLOSED_DECLARE)
#undef CLOSED_DECLARE
static bool load_closed_driver(struct android_app *app) {
    char hooks[PATH_MAX],dir[PATH_MAX];Dl_info self={0};
    if(!dladdr((void*)android_main,&self) || !self.dli_fname ||
       snprintf(hooks,sizeof(hooks),"%s",self.dli_fname)>=(int)sizeof(hooks))return false;
    char *slash=strrchr(hooks,'/');if(!slash || strstr(hooks,".apk!"))return false;*slash=0;
    if(!app->activity->internalDataPath ||
       snprintf(dir,sizeof(dir),"%s/gpu/",app->activity->internalDataPath)>=(int)sizeof(dir))return false;
    if(mkdir(dir,0700) && errno!=EEXIST)return false;
    const char *files[]={"vulkan-bridge.so","libqti-mapper-bridge.so","notgsl.so","notadreno_utils.so",
                         "notdmabufheap.so","notllvm-glnext.so","notllvm-qgl.so"};
    for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++) {
        char asset_path[128],dest[PATH_MAX],tmp[PATH_MAX];
        if(snprintf(asset_path,sizeof(asset_path),"gpu/%s",files[i])>=(int)sizeof(asset_path)||
           snprintf(dest,sizeof(dest),"%s%s",dir,files[i])>=(int)sizeof(dest)||
           snprintf(tmp,sizeof(tmp),"%s.new",dest)>=(int)sizeof(tmp))return false;
        AAsset *asset=AAssetManager_open(app->activity->assetManager,asset_path,AASSET_MODE_STREAMING);
        if(!asset)return false;
        int fd=open(tmp,O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC,0600);
        if(fd<0){AAsset_close(asset);return false;}
        char buffer[16384];int count;bool ok=true;
        while((count=AAsset_read(asset,buffer,sizeof(buffer)))>0) {
            int pos=0;while(pos<count) {
                ssize_t wrote=write(fd,buffer+pos,count-pos);
                if(wrote<0 && errno==EINTR)continue;
                if(wrote<=0){ok=false;break;}pos+=wrote;
            }
            if(!ok)break;
        }
        if(count<0)ok=false;
        AAsset_close(asset);
        if(fchmod(fd,0500))ok=false;
        if(close(fd))ok=false;
        if(!ok || rename(tmp,dest)){unlink(tmp);return false;}
    }
    // Only custom-driver selection; no KGSL user mapping or turbo modes.
    void *lib=adrenotools_open_libvulkan(RTLD_NOW|RTLD_LOCAL,ADRENOTOOLS_DRIVER_CUSTOM,
        NULL,hooks,dir,"vulkan-bridge.so",NULL,NULL);
    if(!lib){LOG("FAIL adrenotools: %s",dlerror());return false;}
#define CLOSED_LOAD(n) vk##n=(PFN_vk##n)dlsym(lib,"vk" #n);if(!vk##n){LOG("FAIL missing vk%s",#n);return false;}
    CLOSED_VK_FUNCTIONS(CLOSED_LOAD)
#undef CLOSED_LOAD
    LOG("CLOSED_LOADER_READY v819 candidate, identity checked during device enumeration");return true;
}
