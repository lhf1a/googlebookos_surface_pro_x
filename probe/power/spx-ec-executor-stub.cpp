// Minimal no-op AIDL stub for vendor.google.desktop.IEcExecutor/default on
// non-ChromeOS-EC hardware (no /dev/cros_ec). Keeps health HAL from repeatedly
// querying servicemanager for an absent VINTF service and replies cleanly to
// fetchSmartBatteryInfo (temperature=0 mC, matching Android's default when no
// battery temperature sensor is exposed) while honestly reporting
// EX_UNSUPPORTED_OPERATION for all other EC commands.
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_stability.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>

namespace {

constexpr const char kDescriptor[] = "vendor.google.desktop.IEcExecutor";
constexpr const char kInstance[] = "vendor.google.desktop.IEcExecutor/default";
constexpr int32_t kInterfaceVersion = 2;
constexpr const char kInterfaceHash[] = "2dfc14cf19e97a887f17030ea7a15ed2944e9b48";
constexpr transaction_code_t kTxFetchSmartBatteryInfo = FIRST_CALL_TRANSACTION + 19;
constexpr transaction_code_t kTxGetInterfaceVersion = FIRST_CALL_TRANSACTION + 16777214;
constexpr transaction_code_t kTxGetInterfaceHash = FIRST_CALL_TRANSACTION + 16777213;

void* onCreate(void* args) {
    return args;
}

void onDestroy(void* /*userData*/) {}

binder_status_t writeDefaultSmartBatteryInfo(AParcel* out) {
    binder_status_t st = AParcel_writeInt32(out, EX_NONE);
    if (st != STATUS_OK) return st;
    // Non-null AIDL parcelable indicator.
    st = AParcel_writeInt32(out, 1);
    if (st != STATUS_OK) return st;

    const int32_t startPos = AParcel_getDataPosition(out);
    st = AParcel_writeInt32(out, 0);
    if (st != STATUS_OK) return st;

    // SmartBatteryInfo parcel layout (ec-executor-aidl-V1-ndk.so):
    //   string  (offset +0x00)
    //   int32   temperatureMilliCelsius (offset +0x18; 0 -> 0 tenths C, avoiding INT32_MIN warning)
    //   string  (offset +0x20)
    //   string  (offset +0x38)
    st = AParcel_writeString(out, "", 0);
    if (st != STATUS_OK) return st;
    st = AParcel_writeInt32(out, 0);
    if (st != STATUS_OK) return st;
    st = AParcel_writeString(out, "", 0);
    if (st != STATUS_OK) return st;
    st = AParcel_writeString(out, "", 0);
    if (st != STATUS_OK) return st;

    const int32_t endPos = AParcel_getDataPosition(out);
    st = AParcel_setDataPosition(out, startPos);
    if (st != STATUS_OK) return st;
    st = AParcel_writeInt32(out, endPos - startPos);
    if (st != STATUS_OK) return st;
    return AParcel_setDataPosition(out, endPos);
}

binder_status_t onTransact(AIBinder* /*binder*/,
                           transaction_code_t code,
                           const AParcel* /*in*/,
                           AParcel* out) {
    if (code == kTxFetchSmartBatteryInfo) {
        return writeDefaultSmartBatteryInfo(out);
    }
    if (code == kTxGetInterfaceVersion) {
        binder_status_t st = AParcel_writeInt32(out, EX_NONE);
        if (st != STATUS_OK) return st;
        return AParcel_writeInt32(out, kInterfaceVersion);
    }
    if (code == kTxGetInterfaceHash) {
        binder_status_t st = AParcel_writeInt32(out, EX_NONE);
        if (st != STATUS_OK) return st;
        return AParcel_writeString(out, kInterfaceHash, static_cast<int32_t>(strlen(kInterfaceHash)));
    }
    AStatus* status = AStatus_fromExceptionCodeWithMessage(
        EX_UNSUPPORTED_OPERATION, "No ChromeOS EC hardware present");
    binder_status_t st = AParcel_writeStatusHeader(out, status);
    AStatus_delete(status);
    return st;
}

}  // namespace

int main() {
    signal(SIGPIPE, SIG_IGN);
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();

    AIBinder_Class* cls = AIBinder_Class_define(kDescriptor, onCreate, onDestroy, onTransact);
    if (!cls) {
        __android_log_print(ANDROID_LOG_ERROR, "SpxEcExecutorStub", "AIBinder_Class_define failed");
        return 1;
    }

    AIBinder* binder = AIBinder_new(cls, nullptr);
    if (!binder) {
        __android_log_print(ANDROID_LOG_ERROR, "SpxEcExecutorStub", "AIBinder_new failed");
        return 1;
    }
    AIBinder_markVintfStability(binder);

    binder_status_t st = AServiceManager_addService(binder, kInstance);
    if (st != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "SpxEcExecutorStub",
                            "AServiceManager_addService(%s) failed: %d", kInstance, st);
        AIBinder_decStrong(binder);
        return 1;
    }

    __android_log_print(ANDROID_LOG_INFO, "SpxEcExecutorStub",
                        "Registered no-op stub %s (v%d)", kInstance, kInterfaceVersion);
    ABinderProcess_joinThreadPool();
    AIBinder_decStrong(binder);
    return 0;
}
