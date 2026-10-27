/* Resident ath10k platform-driver adapter for Android libwifi-hal.
 * Only the load/firmware-mode boundary is replaced. HAL vendor operations and
 * their errors remain real; this library does not manufacture capabilities.
 */
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <unistd.h>
#include <android/log.h>

int is_wifi_driver_loaded(void) {
    char driver[PATH_MAX];
    if (access("/sys/class/net/wlan0/phy80211",F_OK)) return 0;
    if (!realpath("/sys/class/net/wlan0/device/driver",driver)) return 0;
    const char *name=strrchr(driver,'/');
    return name && !strcmp(name+1,"ath10k_snoc");
}
int wifi_load_driver(void) {
    if (!is_wifi_driver_loaded()) { errno=ENODEV; return -1; }
    __android_log_print(ANDROID_LOG_INFO,"sq2-wifi","Resident ath10k_snoc verified through sysfs");
    __system_property_set("wlan.driver.status","ok");
    return 0;
}
int wifi_unload_driver(void) {
    /* Platform driver stays resident, like an Android built-in Wi-Fi driver.
     * The HAL/supplicant owns interface state; do not rmmod shared firmware.
     */
    __android_log_print(ANDROID_LOG_INFO,"sq2-wifi","HAL released; platform ath10k remains resident");
    return 0;
}
int sq2_driver_state(const char *state) __asm__("_Z24wifi_change_driver_statePKc");
int sq2_driver_state(const char *state) {
    /* QTI's firmware-mode controller calls this private entry point as well
     * as wifi_load_driver(). ath10k has one resident firmware for nl80211
     * interface modes, so an OFF/ON firmware reload pair is unnecessary.
     * Actual interface creation, deletion and state are left to nl80211.
     */
    __android_log_print(ANDROID_LOG_INFO,"sq2-wifi","Firmware lifecycle request: %s",state?state:"(null)");
    if (!state || (strcmp(state,"ON") && strcmp(state,"OFF") && strcmp(state,"1") && strcmp(state,"0"))) {
        errno=EOPNOTSUPP; return -1;
    }
    if (!is_wifi_driver_loaded()) { errno=ENODEV; return -1; }
    return 0;
}
const char *wifi_get_fw_path(int mode) { (void)mode; return NULL; }
int wifi_change_fw_path(const char *path) {
    if (!path) return 0; /* ath10k selects mode through nl80211, no fwpath knob */
    errno=EOPNOTSUPP; return -1;
}
