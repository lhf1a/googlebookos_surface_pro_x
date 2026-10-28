/* Temporary lab HAL selector for an unmodified 3DMark installation.
 * No constructor, namespace changes, properties or APK modifications.
 * The installer must retain a verified, unchanged Mesa copy and provide the
 * donor dependencies in the vendor linker namespace before binding this HAL.
 * A closed-driver failure never silently falls back to Mesa for the target.
 */
#define VK_NO_PROTOTYPES
#include <hardware/hwvulkan.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define TARGET "com.futuremark.dmandroid.application"
#define DONOR "/vendor/lib64/sq2-quest2/vulkan-bridge.so"
#define BASELINE "/vendor/lib64/hw/vulkan.sq2-mesa-baseline.so"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"SQ2VkSelector",__VA_ARGS__)

static pthread_once_t once=PTHREAD_ONCE_INIT;
static hw_module_t *selected;
static void *library;
static const char *selected_path;

static void select_driver(void) {
    char name[256]={0};
    FILE *stream=fopen("/proc/self/cmdline","rb");
    size_t count=stream?fread(name,1,sizeof(name)-1,stream):0;
    if(stream)fclose(stream);
    /* cmdline stores NUL-delimited argv. Requiring the complete first field
     * prevents a truncated prefix or a similarly named app from selecting it. */
    int target=count>sizeof(TARGET)-1 && !memcmp(name,TARGET,sizeof(TARGET));
    selected_path=target?DONOR:BASELINE;
    LOG("SELECT process=%s driver=%s",count?name:"(unavailable)",selected_path);
    library=dlopen(selected_path,RTLD_NOW|RTLD_LOCAL);
    if(!library){LOG("FAIL dlopen %s: %s",selected_path,dlerror());return;}
    hw_module_t *module=(hw_module_t*)dlsym(library,HAL_MODULE_INFO_SYM_AS_STR);
    if(!module || module->tag!=HARDWARE_MODULE_TAG || !module->id ||
       strcmp(module->id,HWVULKAN_HARDWARE_MODULE_ID) || !module->methods || !module->methods->open) {
        LOG("FAIL invalid Vulkan HMI at %s",selected_path);return;
    }
    selected=module;
    /* Keep library open until process exit: delegate function tables and all
     * Vulkan dispatch pointers refer into it. Do not modify donor HMI fields. */
}

static int open_driver(const hw_module_t *module,const char *id,hw_device_t **device) {
    (void)module;
    if(!device)return -EINVAL;
    *device=NULL;
    if(!id || strcmp(id,HWVULKAN_DEVICE_0))return -EINVAL;
    pthread_once(&once,select_driver);
    if(!selected)return -ENODEV;
    /* Pass the real module, and return its real device intact. This forwards
     * EnumerateInstanceExtensionProperties, CreateInstance, GIPA and close
     * with their original ABI, without wrapping dispatchable object handles. */
    int result=selected->methods->open(selected,id,device);
    if(result || !*device) {
        LOG("FAIL delegate open path=%s result=%d",selected_path,result);
        return result?result:-ENODEV;
    }
    hwvulkan_device_t *vk=(hwvulkan_device_t*)*device;
    if(vk->common.tag!=HARDWARE_DEVICE_TAG || !vk->EnumerateInstanceExtensionProperties ||
       !vk->CreateInstance || !vk->GetInstanceProcAddr) {
        LOG("FAIL delegate callbacks at %s",selected_path);
        if(vk->common.close)vk->common.close(*device);
        *device=NULL;return -ENODEV;
    }
    LOG("OPEN_OK path=%s",selected_path);
    return 0;
}

static hw_module_methods_t methods={.open=open_driver};
__attribute__((visibility("default"))) hwvulkan_module_t HAL_MODULE_INFO_SYM={
    .common={.tag=HARDWARE_MODULE_TAG,
        .module_api_version=HWVULKAN_MODULE_API_VERSION_0_1,
        .hal_api_version=HARDWARE_HAL_API_VERSION,
        .id=HWVULKAN_HARDWARE_MODULE_ID,
        .name="SQ2 temporary process-scoped Vulkan selector",
        .author="SQ2 lab port",.methods=&methods}
};
