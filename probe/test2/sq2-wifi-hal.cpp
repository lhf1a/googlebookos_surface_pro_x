/* Minimal real-interface legacy HAL for SQ2 ath10k + Android wificond.
 * Discovery, scan and association are done by wificond / AIDL supplicant via
 * nl80211. Optional QTI firmware offloads remain the caller's NOT_SUPPORTED
 * stubs. No fabricated firmware versions, scan channels or radio statistics.
 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <new>
#include <unistd.h>
#include <wifi_hal.h>
#include <android/log.h>

extern "C" int is_wifi_driver_loaded(void);
namespace {
struct Context {
    std::mutex mutex;
    std::condition_variable ready;
    bool stopping=false;
    wifi_cleaned_up_handler callback=nullptr;
    wifi_interface_handle iface=reinterpret_cast<wifi_interface_handle>(this);
};
wifi_error initialize(wifi_handle *handle) {
    if (!handle) return WIFI_ERROR_INVALID_ARGS;
    if (!is_wifi_driver_loaded()) return WIFI_ERROR_NOT_AVAILABLE;
    Context *ctx=new(std::nothrow) Context;
    if (!ctx) return WIFI_ERROR_OUT_OF_MEMORY;
    *handle=reinterpret_cast<wifi_handle>(ctx);
    __android_log_print(ANDROID_LOG_INFO,"sq2-wifi","Generic HAL initialized for real ath10k wlan0; scan/association via nl80211");
    return WIFI_SUCCESS;
}
void cleanup(wifi_handle handle,wifi_cleaned_up_handler callback) {
    if (!handle) return;
    auto *ctx=reinterpret_cast<Context*>(handle);
    std::lock_guard<std::mutex> lock(ctx->mutex);
    ctx->callback=callback; ctx->stopping=true; ctx->ready.notify_all();
}
void eventloop(wifi_handle handle) {
    auto *ctx=reinterpret_cast<Context*>(handle);
    if (!ctx) return;
    wifi_cleaned_up_handler callback;
    {
        std::unique_lock<std::mutex> lock(ctx->mutex);
        ctx->ready.wait(lock,[ctx]{return ctx->stopping;});
        callback=ctx->callback;
    }
    if (callback) callback(handle);
    delete ctx;
}
wifi_error ifaces(wifi_handle handle,int *count,wifi_interface_handle **list) {
    if (!handle || !count || !list) return WIFI_ERROR_INVALID_ARGS;
    auto *ctx=reinterpret_cast<Context*>(handle);
    if (!is_wifi_driver_loaded()) return WIFI_ERROR_NOT_AVAILABLE;
    *count=1; *list=&ctx->iface; return WIFI_SUCCESS;
}
wifi_error ifname(wifi_interface_handle handle,char *name,size_t size) {
    if (!handle || !name || size<sizeof("wlan0")) return WIFI_ERROR_INVALID_ARGS;
    memcpy(name,"wlan0",sizeof("wlan0")); return WIFI_SUCCESS;
}
wifi_error features(wifi_interface_handle handle,feature_set *set) {
    if (!handle || !set) return WIFI_ERROR_INVALID_ARGS;
    // Both 2.4 GHz (2452 MHz) and 5 GHz (5240 MHz) association were observed
    // through the real SQ2 controller. Offload/statistics flags stay absent.
    *set=WIFI_FEATURE_INFRA|WIFI_FEATURE_INFRA_5G; return WIFI_SUCCESS;
}
wifi_error concurrency(wifi_interface_handle handle,int size,feature_set *matrix,int *count) {
    if (!handle || size<1 || !matrix || !count) return WIFI_ERROR_INVALID_ARGS;
    matrix[0]=WIFI_FEATURE_INFRA|WIFI_FEATURE_INFRA_5G; *count=1; return WIFI_SUCCESS;
}
wifi_error driver_version(wifi_interface_handle handle,char *buffer,int size) {
    if (!handle || !buffer || size<1) return WIFI_ERROR_INVALID_ARGS;
    FILE *f=fopen("/proc/sys/kernel/osrelease","re");
    if (!f) return WIFI_ERROR_NOT_AVAILABLE;
    char kernel[128]; const bool ok=fgets(kernel,sizeof(kernel),f)!=nullptr; fclose(f);
    if (!ok) return WIFI_ERROR_NOT_AVAILABLE;
    kernel[strcspn(kernel,"\n")]=0;
    int n=snprintf(buffer,(size_t)size,"ath10k_snoc %s",kernel);
    return n>=0 && n<size ? WIFI_SUCCESS : WIFI_ERROR_INVALID_ARGS;
}
}
extern "C" wifi_error init_wifi_vendor_hal_func_table(wifi_hal_fn *fn) {
    if (!fn) return WIFI_ERROR_INVALID_ARGS;
    // AOSP initializes every slot to a typed unsupported stub before calling
    // the vendor initializer. Only replace stable ABI entries implemented here.
    fn->wifi_initialize=initialize;
    fn->wifi_cleanup=cleanup;
    fn->wifi_event_loop=eventloop;
    fn->wifi_get_ifaces=ifaces;
    fn->wifi_get_iface_name=ifname;
    fn->wifi_get_supported_feature_set=features;
    fn->wifi_get_concurrency_matrix=concurrency;
    fn->wifi_get_driver_version=driver_version;
    return WIFI_SUCCESS;
}
