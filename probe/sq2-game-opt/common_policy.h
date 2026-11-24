#ifndef SQ2_GAME_OPT_COMMON_POLICY_H
#define SQ2_GAME_OPT_COMMON_POLICY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum sq2_engine_type {
    SQ2_ENGINE_NONE = 0,
    SQ2_ENGINE_UNREAL = 1,
    SQ2_ENGINE_UNITY = 2,
    SQ2_ENGINE_EXTRA = 3,
};

#define SQ2_MAX_PATTERNS 32
#define SQ2_MAX_PATTERN_LEN 96
#define SQ2_MAX_PATH_LEN 256

struct sq2_opt_config {
    int enabled;
    int boost_cpu;
    int boost_gpu;
    int pin_threads;
    int mailbox_present_mode;
    int egl_swap_interval_zero;
    int poll_interval_ms;
    int extra_count;
    char extra_apps[SQ2_MAX_PATTERNS][SQ2_MAX_PATTERN_LEN];
    int exclude_count;
    char exclude_apps[SQ2_MAX_PATTERNS][SQ2_MAX_PATTERN_LEN];
};

struct sq2_cpu_saved_state {
    int active;
    char policy_dir[SQ2_MAX_PATH_LEN];
    char orig_governor[64];
    char orig_min_freq[32];
    char orig_max_freq[32];
    char max_target_freq[32];
};

struct sq2_gpu_saved_state {
    int active;
    char devfreq_dir[SQ2_MAX_PATH_LEN];
    char orig_governor[64];
    char orig_min_freq[32];
    char orig_max_freq[32];
    char max_target_freq[32];
};

struct sq2_dvfs_state {
    struct sq2_cpu_saved_state cpu;
    struct sq2_gpu_saved_state gpu;
};

struct sq2_companion_reply {
    uint32_t magic;
    uint8_t active;
    uint8_t engine;
    uint8_t mailbox_present_mode;
    uint8_t egl_swap_interval_zero;
};

#define SQ2_COMPANION_MAGIC 0x53513247u /* "SQ2G" */

void sq2_opt_config_init_default(struct sq2_opt_config *cfg);
int sq2_opt_config_parse_text(const char *text, struct sq2_opt_config *cfg);
int sq2_opt_config_load_file(const char *path, struct sq2_opt_config *cfg);

int sq2_match_pattern(const char *pattern, const char *name);
void sq2_base_package_name(const char *proc_name, char *out, size_t out_sz);

enum sq2_engine_type sq2_classify_soname(const char *path_or_name);
enum sq2_engine_type sq2_detect_engine_from_maps_text(const char *maps_text);
enum sq2_engine_type sq2_detect_engine_in_apk_file(const char *apk_path);
enum sq2_engine_type sq2_detect_engine_in_lib_dir(const char *lib_dir);
enum sq2_engine_type sq2_detect_engine_for_package_root(const char *pkg_root);
enum sq2_engine_type sq2_classify_app(
    const struct sq2_opt_config *cfg,
    const char *proc_or_pkg,
    const char *maps_text,
    const char *apk_path,
    const char *data_app_root);

int sq2_thread_should_pin_big(enum sq2_engine_type engine, const char *comm);

int sq2_find_highest_freq_str(const char *avail_text, char *out, size_t out_sz);
int sq2_is_screen_on(const char *sysfs_root);
int sq2_compute_sleep_ms(const struct sq2_opt_config *cfg, int screen_on, int game_active);

int sq2_dvfs_boost_engage(
    struct sq2_dvfs_state *state,
    const char *sysfs_root,
    int boost_cpu,
    int boost_gpu);
int sq2_dvfs_boost_restore(struct sq2_dvfs_state *state);

#ifdef __cplusplus
}
#endif

#endif /* SQ2_GAME_OPT_COMMON_POLICY_H */
