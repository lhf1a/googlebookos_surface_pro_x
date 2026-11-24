#define _POSIX_C_SOURCE 200809L
#include "common_policy.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int read_small_file(const char *path, char *buf, size_t buf_sz) {
    if (!path || !buf || buf_sz == 0) {
        return -1;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    size_t total = 0;
    while (total + 1 < buf_sz) {
        ssize_t n = read(fd, buf + total, buf_sz - 1 - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }
    close(fd);
    buf[total] = '\0';
    return (int)total;
}

static void trim_inplace(char *s) {
    if (!s) {
        return;
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) {
        start++;
    }
    if (start > 0) {
        memmove(s, s + start, len - start + 1);
    }
}

static int read_trimmed_sysfs(const char *dir, const char *node, char *out, size_t out_sz) {
    char path[SQ2_MAX_PATH_LEN];
    int n = snprintf(path, sizeof(path), "%s/%s", dir, node);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
        return -1;
    }
    if (read_small_file(path, out, out_sz) < 0) {
        return -1;
    }
    trim_inplace(out);
    return out[0] ? 0 : -1;
}

static int write_sysfs_str(const char *dir, const char *node, const char *val) {
    if (!dir || !dir[0] || !node || !val || !val[0]) {
        return -1;
    }
    char path[SQ2_MAX_PATH_LEN];
    int n = snprintf(path, sizeof(path), "%s/%s", dir, node);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
        return -1;
    }
    int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    size_t len = strlen(val);
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, val + off, len - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}

static int parse_bool_val(const char *s, int fallback) {
    if (!s || !s[0]) {
        return fallback;
    }
    if (strcasecmp(s, "1") == 0 || strcasecmp(s, "true") == 0 ||
        strcasecmp(s, "yes") == 0 || strcasecmp(s, "on") == 0) {
        return 1;
    }
    if (strcasecmp(s, "0") == 0 || strcasecmp(s, "false") == 0 ||
        strcasecmp(s, "no") == 0 || strcasecmp(s, "off") == 0) {
        return 0;
    }
    return fallback;
}

static void add_pattern_list(
    const char *csv,
    char dst[SQ2_MAX_PATTERNS][SQ2_MAX_PATTERN_LEN],
    int *count_ptr) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", csv ? csv : "");
    *count_ptr = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(tmp, ", \t", &saveptr);
         tok != NULL && *count_ptr < SQ2_MAX_PATTERNS;
         tok = strtok_r(NULL, ", \t", &saveptr)) {
        trim_inplace(tok);
        if (!tok[0]) {
            continue;
        }
        snprintf(dst[*count_ptr], SQ2_MAX_PATTERN_LEN, "%s", tok);
        (*count_ptr)++;
    }
}

void sq2_opt_config_init_default(struct sq2_opt_config *cfg) {
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 1;
    cfg->boost_cpu = 1;
    cfg->boost_gpu = 1;
    cfg->pin_threads = 1;
    cfg->mailbox_present_mode = 1;
    cfg->egl_swap_interval_zero = 1;
    cfg->poll_interval_ms = 250;
}

int sq2_opt_config_parse_text(const char *text, struct sq2_opt_config *cfg) {
    if (!cfg) {
        return -1;
    }
    sq2_opt_config_init_default(cfg);
    if (!text) {
        return 0;
    }

    const char *p = text;
    char line[1024];
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        p = eol ? (eol + 1) : (p + len);

        char *hash = strchr(line, '#');
        if (hash) {
            *hash = '\0';
        }
        trim_inplace(line);
        if (!line[0]) {
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_inplace(key);
        trim_inplace(val);

        if (strcmp(key, "enabled") == 0) {
            cfg->enabled = parse_bool_val(val, cfg->enabled);
        } else if (strcmp(key, "boost_cpu") == 0) {
            cfg->boost_cpu = parse_bool_val(val, cfg->boost_cpu);
        } else if (strcmp(key, "boost_gpu") == 0) {
            cfg->boost_gpu = parse_bool_val(val, cfg->boost_gpu);
        } else if (strcmp(key, "pin_threads") == 0) {
            cfg->pin_threads = parse_bool_val(val, cfg->pin_threads);
        } else if (strcmp(key, "mailbox_present_mode") == 0) {
            cfg->mailbox_present_mode = parse_bool_val(val, cfg->mailbox_present_mode);
        } else if (strcmp(key, "egl_swap_interval_zero") == 0) {
            cfg->egl_swap_interval_zero = parse_bool_val(val, cfg->egl_swap_interval_zero);
        } else if (strcmp(key, "poll_interval_ms") == 0) {
            char *endp = NULL;
            long v = strtol(val, &endp, 10);
            if (endp && endp != val) {
                if (v < 100) v = 100;
                if (v > 2000) v = 2000;
                cfg->poll_interval_ms = (int)v;
            }
        } else if (strcmp(key, "extra_apps") == 0) {
            add_pattern_list(val, cfg->extra_apps, &cfg->extra_count);
        } else if (strcmp(key, "exclude_apps") == 0) {
            add_pattern_list(val, cfg->exclude_apps, &cfg->exclude_count);
        }
    }
    return 0;
}

int sq2_opt_config_load_file(const char *path, struct sq2_opt_config *cfg) {
    char buf[8192];
    sq2_opt_config_init_default(cfg);
    if (!path || read_small_file(path, buf, sizeof(buf)) < 0) {
        return -1;
    }
    return sq2_opt_config_parse_text(buf, cfg);
}

int sq2_match_pattern(const char *pattern, const char *name) {
    if (!pattern || !pattern[0] || !name || !name[0]) {
        return 0;
    }
    size_t plen = strlen(pattern);
    if (pattern[plen - 1] == '*') {
        return strncmp(name, pattern, plen - 1) == 0;
    }
    return strcmp(name, pattern) == 0;
}

void sq2_base_package_name(const char *proc_name, char *out, size_t out_sz) {
    if (!out || out_sz == 0) {
        return;
    }
    if (!proc_name) {
        out[0] = '\0';
        return;
    }
    const char *colon = strchr(proc_name, ':');
    size_t len = colon ? (size_t)(colon - proc_name) : strlen(proc_name);
    if (len >= out_sz) {
        len = out_sz - 1;
    }
    memcpy(out, proc_name, len);
    out[len] = '\0';
}

enum sq2_engine_type sq2_classify_soname(const char *path_or_name) {
    if (!path_or_name || !path_or_name[0]) {
        return SQ2_ENGINE_NONE;
    }
    const char *base = strrchr(path_or_name, '/');
    base = base ? (base + 1) : path_or_name;
    /* ZIP split entries or APK paths can contain '!' */
    const char *bang = strrchr(base, '!');
    if (bang) {
        base = bang + 1;
    }
    if (strncmp(base, "libUE4", 6) == 0 ||
        strncmp(base, "libUnreal", 9) == 0 ||
        strncmp(base, "libUE5", 6) == 0) {
        return SQ2_ENGINE_UNREAL;
    }
    if (strncmp(base, "libunity", 8) == 0 ||
        strncmp(base, "libil2cpp", 9) == 0) {
        return SQ2_ENGINE_UNITY;
    }
    return SQ2_ENGINE_NONE;
}

enum sq2_engine_type sq2_detect_engine_from_maps_text(const char *maps_text) {
    if (!maps_text || !maps_text[0]) {
        return SQ2_ENGINE_NONE;
    }
    const char *p = maps_text;
    char line[1024];
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        p = eol ? (eol + 1) : (p + len);

        char *slash = strchr(line, '/');
        if (!slash) {
            continue;
        }
        enum sq2_engine_type eng = sq2_classify_soname(slash);
        if (eng != SQ2_ENGINE_NONE) {
            return eng;
        }
    }
    return SQ2_ENGINE_NONE;
}

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static enum sq2_engine_type scan_central_dir_buffer(const uint8_t *buf, size_t len) {
    size_t pos = 0;
    while (pos + 46 <= len) {
        if (buf[pos] != 'P' || buf[pos + 1] != 'K' ||
            buf[pos + 2] != 1 || buf[pos + 3] != 2) {
            break;
        }
        uint16_t name_len = read_le16(buf + pos + 28);
        uint16_t extra_len = read_le16(buf + pos + 30);
        uint16_t comment_len = read_le16(buf + pos + 32);
        size_t next = pos + 46 + name_len + extra_len + comment_len;
        if (next > len) {
            break;
        }
        if (name_len > 4 && name_len < 256) {
            char name[256];
            memcpy(name, buf + pos + 46, name_len);
            name[name_len] = '\0';
            if (strncmp(name, "lib/", 4) == 0) {
                enum sq2_engine_type eng = sq2_classify_soname(name);
                if (eng != SQ2_ENGINE_NONE) {
                    return eng;
                }
            }
        }
        pos = next;
    }
    return SQ2_ENGINE_NONE;
}

enum sq2_engine_type sq2_detect_engine_in_apk_file(const char *apk_path) {
    if (!apk_path || !apk_path[0]) {
        return SQ2_ENGINE_NONE;
    }
    int fd = open(apk_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return SQ2_ENGINE_NONE;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 22) {
        close(fd);
        return SQ2_ENGINE_NONE;
    }
    off_t fsize = st.st_size;
    size_t tail_sz = (fsize > 66000) ? 66000 : (size_t)fsize;
    uint8_t *tail = (uint8_t *)malloc(tail_sz);
    if (!tail) {
        close(fd);
        return SQ2_ENGINE_NONE;
    }
    off_t tail_off = fsize - (off_t)tail_sz;
    if (pread(fd, tail, tail_sz, tail_off) != (ssize_t)tail_sz) {
        free(tail);
        close(fd);
        return SQ2_ENGINE_NONE;
    }

    int found_eocd = 0;
    uint32_t cd_size = 0;
    uint32_t cd_offset = 0;
    if (tail_sz >= 22) {
        for (ssize_t i = (ssize_t)tail_sz - 22; i >= 0; i--) {
            if (tail[i] == 'P' && tail[i + 1] == 'K' &&
                tail[i + 2] == 5 && tail[i + 3] == 6) {
                uint16_t c_len = read_le16(tail + i + 20);
                if ((size_t)i + 22 + c_len == tail_sz) {
                    cd_size = read_le32(tail + i + 12);
                    cd_offset = read_le32(tail + i + 16);
                    found_eocd = 1;
                    break;
                }
            }
        }
    }
    free(tail);

    enum sq2_engine_type res = SQ2_ENGINE_NONE;
    if (found_eocd && cd_size > 0 && cd_size <= (16u * 1024u * 1024u) &&
        (off_t)cd_offset + (off_t)cd_size <= fsize) {
        uint8_t *cd = (uint8_t *)malloc(cd_size);
        if (cd) {
            size_t got = 0;
            while (got < cd_size) {
                ssize_t r = pread(fd, cd + got, cd_size - got, (off_t)cd_offset + (off_t)got);
                if (r <= 0) {
                    if (r < 0 && errno == EINTR) continue;
                    break;
                }
                got += (size_t)r;
            }
            if (got == cd_size) {
                res = scan_central_dir_buffer(cd, cd_size);
            }
            free(cd);
        }
    }
    close(fd);
    return res;
}

enum sq2_engine_type sq2_detect_engine_in_lib_dir(const char *lib_dir) {
    if (!lib_dir || !lib_dir[0]) {
        return SQ2_ENGINE_NONE;
    }
    static const char *const abis[] = {"arm64", "arm64-v8a", "arm", "armeabi-v7a"};
    for (size_t i = 0; i < sizeof(abis) / sizeof(abis[0]); i++) {
        char sub[SQ2_MAX_PATH_LEN];
        int n = snprintf(sub, sizeof(sub), "%s/%s", lib_dir, abis[i]);
        if (n <= 0 || (size_t)n >= sizeof(sub)) continue;
        DIR *d = opendir(sub);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            enum sq2_engine_type eng = sq2_classify_soname(de->d_name);
            if (eng != SQ2_ENGINE_NONE) {
                closedir(d);
                return eng;
            }
        }
        closedir(d);
    }
    return SQ2_ENGINE_NONE;
}

enum sq2_engine_type sq2_detect_engine_for_package_root(const char *pkg_root) {
    if (!pkg_root || !pkg_root[0]) {
        return SQ2_ENGINE_NONE;
    }
    char path[SQ2_MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/lib", pkg_root);
    enum sq2_engine_type eng = sq2_detect_engine_in_lib_dir(path);
    if (eng != SQ2_ENGINE_NONE) {
        return eng;
    }
    snprintf(path, sizeof(path), "%s/base.apk", pkg_root);
    return sq2_detect_engine_in_apk_file(path);
}

static enum sq2_engine_type find_pkg_in_data_app(const char *data_app_root, const char *base_pkg) {
    if (!data_app_root || !data_app_root[0] || !base_pkg || !base_pkg[0]) {
        return SQ2_ENGINE_NONE;
    }
    size_t pkg_len = strlen(base_pkg);
    DIR *top = opendir(data_app_root);
    if (!top) {
        return SQ2_ENGINE_NONE;
    }
    struct dirent *de;
    while ((de = readdir(top)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char child[SQ2_MAX_PATH_LEN];
        int n = snprintf(child, sizeof(child), "%s/%s", data_app_root, de->d_name);
        if (n <= 0 || (size_t)n >= sizeof(child)) continue;

        /* Android <=10: /data/app/<pkg>-<rand> */
        if (strncmp(de->d_name, base_pkg, pkg_len) == 0 &&
            (de->d_name[pkg_len] == '-' || de->d_name[pkg_len] == '\0')) {
            enum sq2_engine_type eng = sq2_detect_engine_for_package_root(child);
            if (eng != SQ2_ENGINE_NONE) {
                closedir(top);
                return eng;
            }
        }

        /* Android 11+: /data/app/~~<rand>/<pkg>-<rand> */
        if (strncmp(de->d_name, "~~", 2) == 0) {
            DIR *sub = opendir(child);
            if (!sub) continue;
            struct dirent *sde;
            while ((sde = readdir(sub)) != NULL) {
                if (strncmp(sde->d_name, base_pkg, pkg_len) == 0 &&
                    (sde->d_name[pkg_len] == '-' || sde->d_name[pkg_len] == '\0')) {
                    char grand[SQ2_MAX_PATH_LEN];
                    int gn = snprintf(grand, sizeof(grand), "%s/%s", child, sde->d_name);
                    if (gn > 0 && (size_t)gn < sizeof(grand)) {
                        enum sq2_engine_type eng = sq2_detect_engine_for_package_root(grand);
                        if (eng != SQ2_ENGINE_NONE) {
                            closedir(sub);
                            closedir(top);
                            return eng;
                        }
                    }
                }
            }
            closedir(sub);
        }
    }
    closedir(top);
    return SQ2_ENGINE_NONE;
}

enum sq2_engine_type sq2_classify_app(
    const struct sq2_opt_config *cfg,
    const char *proc_or_pkg,
    const char *maps_text,
    const char *apk_path,
    const char *data_app_root) {
    if (!cfg || !cfg->enabled || !proc_or_pkg || !proc_or_pkg[0]) {
        return SQ2_ENGINE_NONE;
    }
    char pkg[SQ2_MAX_PATTERN_LEN];
    sq2_base_package_name(proc_or_pkg, pkg, sizeof(pkg));

    for (int i = 0; i < cfg->exclude_count; i++) {
        if (sq2_match_pattern(cfg->exclude_apps[i], pkg) ||
            sq2_match_pattern(cfg->exclude_apps[i], proc_or_pkg)) {
            return SQ2_ENGINE_NONE;
        }
    }

    enum sq2_engine_type eng = SQ2_ENGINE_NONE;
    if (maps_text && maps_text[0]) {
        eng = sq2_detect_engine_from_maps_text(maps_text);
        if (eng != SQ2_ENGINE_NONE) {
            return eng;
        }
    }
    if (apk_path && apk_path[0]) {
        eng = sq2_detect_engine_in_apk_file(apk_path);
        if (eng != SQ2_ENGINE_NONE) {
            return eng;
        }
    }
    if (data_app_root && data_app_root[0]) {
        eng = find_pkg_in_data_app(data_app_root, pkg);
        if (eng != SQ2_ENGINE_NONE) {
            return eng;
        }
    }
    for (int i = 0; i < cfg->extra_count; i++) {
        if (sq2_match_pattern(cfg->extra_apps[i], pkg) ||
            sq2_match_pattern(cfg->extra_apps[i], proc_or_pkg)) {
            return SQ2_ENGINE_EXTRA;
        }
    }
    return SQ2_ENGINE_NONE;
}

static int is_unreal_key_thread(const char *comm) {
    return strcmp(comm, "GameThread") == 0 ||
           strncmp(comm, "RenderThread", 12) == 0 ||
           strcmp(comm, "RHIThread") == 0 ||
           strcmp(comm, "AudioThread") == 0;
}

static int is_unity_key_thread(const char *comm) {
    /* Linux task comm max is 15 chars, truncating UnityGfxDeviceWorker -> UnityGfxDeviceW */
    return strcmp(comm, "UnityMain") == 0 ||
           strncmp(comm, "UnityGfxDevice", 14) == 0;
}

int sq2_thread_should_pin_big(enum sq2_engine_type engine, const char *comm) {
    if (!comm || !comm[0]) {
        return 0;
    }
    switch (engine) {
    case SQ2_ENGINE_UNREAL:
        return is_unreal_key_thread(comm);
    case SQ2_ENGINE_UNITY:
        return is_unity_key_thread(comm);
    case SQ2_ENGINE_EXTRA:
        return is_unreal_key_thread(comm) || is_unity_key_thread(comm);
    default:
        return 0;
    }
}

int sq2_find_highest_freq_str(const char *avail_text, char *out, size_t out_sz) {
    if (!avail_text || !out || out_sz == 0) {
        return -1;
    }
    unsigned long long best = 0;
    const char *p = avail_text;
    while (*p) {
        while (*p && !isdigit((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        char *endp = NULL;
        unsigned long long v = strtoull(p, &endp, 10);
        if (endp != p && v > best) {
            best = v;
        }
        p = endp ? endp : (p + 1);
    }
    if (best == 0) {
        return -1;
    }
    int n = snprintf(out, out_sz, "%llu", best);
    return (n > 0 && (size_t)n < out_sz) ? 0 : -1;
}

int sq2_is_screen_on(const char *sysfs_root) {
    if (!sysfs_root || !sysfs_root[0]) {
        return 1;
    }
    char dir_path[SQ2_MAX_PATH_LEN];
    int n = snprintf(dir_path, sizeof(dir_path), "%s/class/backlight", sysfs_root);
    if (n > 0 && (size_t)n < sizeof(dir_path)) {
        DIR *d = opendir(dir_path);
        if (d) {
            int saw_entry = 0;
            int any_on = 0;
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                char node_dir[SQ2_MAX_PATH_LEN];
                char val[64];
                int dn = snprintf(node_dir, sizeof(node_dir), "%s/%s", dir_path, de->d_name);
                if (dn <= 0 || (size_t)dn >= sizeof(node_dir)) continue;
                if (read_trimmed_sysfs(node_dir, "brightness", val, sizeof(val)) == 0) {
                    saw_entry = 1;
                    long b = strtol(val, NULL, 10);
                    int bl_on = 1;
                    if (read_trimmed_sysfs(node_dir, "bl_power", val, sizeof(val)) == 0) {
                        bl_on = (strtol(val, NULL, 10) == 0);
                    }
                    if (b > 0 && bl_on) {
                        any_on = 1;
                    }
                }
            }
            closedir(d);
            if (saw_entry) {
                return any_on ? 1 : 0;
            }
        }
    }

    n = snprintf(dir_path, sizeof(dir_path), "%s/class/drm", sysfs_root);
    if (n > 0 && (size_t)n < sizeof(dir_path)) {
        DIR *d = opendir(dir_path);
        if (d) {
            int saw_dpms = 0;
            int any_on = 0;
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (strncmp(de->d_name, "card", 4) != 0) continue;
                char node_dir[SQ2_MAX_PATH_LEN];
                char val[64];
                int dn = snprintf(node_dir, sizeof(node_dir), "%s/%s", dir_path, de->d_name);
                if (dn <= 0 || (size_t)dn >= sizeof(node_dir)) continue;
                if (read_trimmed_sysfs(node_dir, "dpms", val, sizeof(val)) == 0) {
                    saw_dpms = 1;
                    if (strcasecmp(val, "On") == 0) {
                        any_on = 1;
                    }
                }
            }
            closedir(d);
            if (saw_dpms) {
                return any_on ? 1 : 0;
            }
        }
    }
    return 1;
}

static int resolve_gpu_devfreq_dir(const char *sysfs_root, char *out, size_t out_sz) {
    int n = snprintf(out, out_sz, "%s/class/devfreq/2c00000.gpu", sysfs_root);
    if (n > 0 && (size_t)n < out_sz && access(out, F_OK) == 0) {
        return 0;
    }
    char base[SQ2_MAX_PATH_LEN];
    n = snprintf(base, sizeof(base), "%s/class/devfreq", sysfs_root);
    if (n <= 0 || (size_t)n >= sizeof(base)) {
        return -1;
    }
    DIR *d = opendir(base);
    if (!d) {
        return -1;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        size_t l = strlen(de->d_name);
        if (l >= 4 && strcmp(de->d_name + l - 4, ".gpu") == 0) {
            int gn = snprintf(out, out_sz, "%s/%s", base, de->d_name);
            closedir(d);
            return (gn > 0 && (size_t)gn < out_sz) ? 0 : -1;
        }
    }
    closedir(d);
    return -1;
}

static int engage_cpu_boost(struct sq2_cpu_saved_state *cpu, const char *sysfs_root) {
    if (cpu->active) {
        return 0;
    }
    int n = snprintf(cpu->policy_dir, sizeof(cpu->policy_dir),
                     "%s/devices/system/cpu/cpufreq/policy4", sysfs_root);
    if (n <= 0 || (size_t)n >= sizeof(cpu->policy_dir)) {
        return -1;
    }
    if (read_trimmed_sysfs(cpu->policy_dir, "scaling_governor",
                           cpu->orig_governor, sizeof(cpu->orig_governor)) != 0 ||
        read_trimmed_sysfs(cpu->policy_dir, "scaling_min_freq",
                           cpu->orig_min_freq, sizeof(cpu->orig_min_freq)) != 0 ||
        read_trimmed_sysfs(cpu->policy_dir, "scaling_max_freq",
                           cpu->orig_max_freq, sizeof(cpu->orig_max_freq)) != 0) {
        return -1;
    }

    char avail[512];
    if (read_trimmed_sysfs(cpu->policy_dir, "scaling_available_frequencies",
                           avail, sizeof(avail)) != 0 ||
        sq2_find_highest_freq_str(avail, cpu->max_target_freq,
                                  sizeof(cpu->max_target_freq)) != 0) {
        if (read_trimmed_sysfs(cpu->policy_dir, "cpuinfo_max_freq",
                               cpu->max_target_freq, sizeof(cpu->max_target_freq)) != 0) {
            snprintf(cpu->max_target_freq, sizeof(cpu->max_target_freq),
                     "%s", cpu->orig_max_freq);
        }
    }

    write_sysfs_str(cpu->policy_dir, "scaling_governor", "performance");
    write_sysfs_str(cpu->policy_dir, "scaling_max_freq", cpu->max_target_freq);
    write_sysfs_str(cpu->policy_dir, "scaling_min_freq", cpu->max_target_freq);
    cpu->active = 1;
    return 0;
}

static int engage_gpu_boost(struct sq2_gpu_saved_state *gpu, const char *sysfs_root) {
    if (gpu->active) {
        return 0;
    }
    if (resolve_gpu_devfreq_dir(sysfs_root, gpu->devfreq_dir,
                                sizeof(gpu->devfreq_dir)) != 0) {
        return -1;
    }
    if (read_trimmed_sysfs(gpu->devfreq_dir, "governor",
                           gpu->orig_governor, sizeof(gpu->orig_governor)) != 0 ||
        read_trimmed_sysfs(gpu->devfreq_dir, "min_freq",
                           gpu->orig_min_freq, sizeof(gpu->orig_min_freq)) != 0 ||
        read_trimmed_sysfs(gpu->devfreq_dir, "max_freq",
                           gpu->orig_max_freq, sizeof(gpu->orig_max_freq)) != 0) {
        return -1;
    }

    char avail[512];
    if (read_trimmed_sysfs(gpu->devfreq_dir, "available_frequencies",
                           avail, sizeof(avail)) != 0 ||
        sq2_find_highest_freq_str(avail, gpu->max_target_freq,
                                  sizeof(gpu->max_target_freq)) != 0) {
        snprintf(gpu->max_target_freq, sizeof(gpu->max_target_freq),
                 "%s", gpu->orig_max_freq);
    }

    write_sysfs_str(gpu->devfreq_dir, "max_freq", gpu->max_target_freq);
    write_sysfs_str(gpu->devfreq_dir, "min_freq", gpu->max_target_freq);
    write_sysfs_str(gpu->devfreq_dir, "governor", "performance");
    gpu->active = 1;
    return 0;
}


int sq2_compute_sleep_ms(const struct sq2_opt_config *cfg, int screen_on, int game_active) {
    int base = (cfg && cfg->poll_interval_ms > 0) ? cfg->poll_interval_ms : 250;
    if (base < 100) base = 100;
    if (base > 2000) base = 2000;
    if (!cfg || !cfg->enabled || !screen_on) {
        return 5000;
    }
    if (game_active) {
        return base;
    }
    int idle_ms = base * 6;
    if (idle_ms < 1500) idle_ms = 1500;
    if (idle_ms > 5000) idle_ms = 5000;
    return idle_ms;
}

int sq2_dvfs_boost_engage(
    struct sq2_dvfs_state *state,
    const char *sysfs_root,
    int boost_cpu,
    int boost_gpu) {
    if (!state || !sysfs_root || !sysfs_root[0]) {
        return -1;
    }
    int rc = 0;
    if (boost_cpu && engage_cpu_boost(&state->cpu, sysfs_root) != 0) {
        rc = -1;
    }
    if (boost_gpu && engage_gpu_boost(&state->gpu, sysfs_root) != 0) {
        rc = -1;
    }
    return rc;
}

int sq2_dvfs_boost_restore(struct sq2_dvfs_state *state) {
    if (!state) {
        return 0;
    }
    int rc = 0;
    if (state->cpu.active) {
        if (write_sysfs_str(state->cpu.policy_dir, "scaling_governor",
                            state->cpu.orig_governor) != 0) rc = -1;
        /* Lower min before adjusting max so kernel never rejects min > max. */
        if (write_sysfs_str(state->cpu.policy_dir, "scaling_min_freq",
                            state->cpu.orig_min_freq) != 0) rc = -1;
        if (write_sysfs_str(state->cpu.policy_dir, "scaling_max_freq",
                            state->cpu.orig_max_freq) != 0) rc = -1;
        memset(&state->cpu, 0, sizeof(state->cpu));
    }
    if (state->gpu.active) {
        if (write_sysfs_str(state->gpu.devfreq_dir, "governor",
                            state->gpu.orig_governor) != 0) rc = -1;
        if (write_sysfs_str(state->gpu.devfreq_dir, "min_freq",
                            state->gpu.orig_min_freq) != 0) rc = -1;
        if (write_sysfs_str(state->gpu.devfreq_dir, "max_freq",
                            state->gpu.orig_max_freq) != 0) rc = -1;
        memset(&state->gpu, 0, sizeof(state->gpu));
    }
    return rc;
}
