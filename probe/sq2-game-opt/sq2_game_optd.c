#define _GNU_SOURCE
#include "common_policy.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SQ2_STATE_DIR "/data/adb/sq2_game_opt"
#define SQ2_CONFIG_PATH SQ2_STATE_DIR "/config.conf"
#define SQ2_PID_PATH SQ2_STATE_DIR "/daemon.pid"
#define SQ2_LOG_PATH SQ2_STATE_DIR "/daemon.log"
#define SQ2_LOG_MAX_BYTES (64 * 1024)
#define SQ2_MAX_PINNED_TIDS 64

static volatile sig_atomic_t g_stop = 0;

struct pinned_tid_list {
    pid_t pid;
    int count;
    pid_t tids[SQ2_MAX_PINNED_TIDS];
};

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static void log_msg(const char *fmt, ...) {
    struct stat st;
    if (stat(SQ2_LOG_PATH, &st) == 0 && st.st_size > SQ2_LOG_MAX_BYTES) {
        (void)unlink(SQ2_LOG_PATH);
    }
    FILE *fp = fopen(SQ2_LOG_PATH, "a");
    if (!fp) {
        return;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_now;
    localtime_r(&ts.tv_sec, &tm_now);
    fprintf(fp, "%02d:%02d:%02d ", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

static int read_proc_file(const char *path, char *buf, size_t buf_sz) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    size_t total = 0;
    while (total + 1 < buf_sz) {
        ssize_t n = read(fd, buf + total, buf_sz - 1 - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (n == 0) break;
        total += (size_t)n;
    }
    close(fd);
    buf[total] = '\0';
    return (int)total;
}

static int read_proc_cmdline(pid_t pid, char *out, size_t out_sz) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);
    int n = read_proc_file(path, out, out_sz);
    if (n <= 0) {
        out[0] = '\0';
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (out[i] == '\0') {
            break;
        }
    }
    return 0;
}

static uid_t read_proc_uid(pid_t pid) {
    char path[64];
    struct stat st;
    snprintf(path, sizeof(path), "/proc/%d", (int)pid);
    if (stat(path, &st) != 0) {
        return 0;
    }
    return st.st_uid;
}

static int is_pinned_tid(const struct pinned_tid_list *list, pid_t tid) {
    for (int i = 0; i < list->count; i++) {
        if (list->tids[i] == tid) {
            return 1;
        }
    }
    return 0;
}

static void restore_pinned_threads(struct pinned_tid_list *list) {
    if (list->count == 0) {
        list->pid = 0;
        return;
    }
    long ncpu = sysconf(_SC_NPROCESSORS_CONF);
    if (ncpu <= 0 || ncpu > CPU_SETSIZE) {
        ncpu = 8;
    }
    cpu_set_t full_mask;
    CPU_ZERO(&full_mask);
    for (long c = 0; c < ncpu; c++) {
        CPU_SET((int)c, &full_mask);
    }
    for (int i = 0; i < list->count; i++) {
        (void)sched_setaffinity(list->tids[i], sizeof(full_mask), &full_mask);
    }
    list->count = 0;
    list->pid = 0;
}

static void pin_game_threads(
    pid_t pid,
    enum sq2_engine_type engine,
    struct pinned_tid_list *list) {
    if (list->pid != pid) {
        restore_pinned_threads(list);
        list->pid = pid;
    }

    long ncpu = sysconf(_SC_NPROCESSORS_CONF);
    if (ncpu < 5 || ncpu > CPU_SETSIZE) {
        ncpu = 8;
    }
    cpu_set_t big_mask;
    CPU_ZERO(&big_mask);
    for (long c = 4; c < ncpu && c < 8; c++) {
        CPU_SET((int)c, &big_mask);
    }

    char task_dir[64];
    snprintf(task_dir, sizeof(task_dir), "/proc/%d/task", (int)pid);
    DIR *d = opendir(task_dir);
    if (!d) {
        return;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)de->d_name[0])) continue;
        pid_t tid = (pid_t)atoi(de->d_name);
        if (tid <= 0 || is_pinned_tid(list, tid)) continue;

        char comm_path[96];
        char comm[32];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/task/%d/comm", (int)pid, (int)tid);
        if (read_proc_file(comm_path, comm, sizeof(comm)) <= 0) continue;
        size_t clen = strlen(comm);
        while (clen > 0 && (comm[clen - 1] == '\n' || comm[clen - 1] == '\r')) {
            comm[--clen] = '\0';
        }
        if (sq2_thread_should_pin_big(engine, comm)) {
            if (sched_setaffinity(tid, sizeof(big_mask), &big_mask) == 0 &&
                list->count < SQ2_MAX_PINNED_TIDS) {
                list->tids[list->count++] = tid;
            }
        }
    }
    closedir(d);
}

#define SQ2_PID_CACHE_SLOTS 64
#define SQ2_MAX_MAPS_SCANS 3

struct pid_cls_cache {
    pid_t pid;
    char cmd[128];
    enum sq2_engine_type apk_eng;
    int scanned_apk;
    int maps_scans;
    uint64_t last_seen_gen;
};

static struct pid_cls_cache g_cls_cache[SQ2_PID_CACHE_SLOTS];
static uint64_t g_cache_gen = 0;

static struct pid_cls_cache *lookup_or_init_cache(pid_t pid, const char *cmd) {
    size_t victim = 0;
    uint64_t oldest_gen = UINT64_MAX;
    for (size_t i = 0; i < SQ2_PID_CACHE_SLOTS; i++) {
        if (g_cls_cache[i].pid == pid && strcmp(g_cls_cache[i].cmd, cmd) == 0) {
            g_cls_cache[i].last_seen_gen = g_cache_gen;
            return &g_cls_cache[i];
        }
        if (g_cls_cache[i].pid == 0 && oldest_gen != 0) {
            oldest_gen = 0;
            victim = i;
        } else if (g_cls_cache[i].last_seen_gen < oldest_gen) {
            oldest_gen = g_cls_cache[i].last_seen_gen;
            victim = i;
        }
    }
    struct pid_cls_cache *slot = &g_cls_cache[victim];
    memset(slot, 0, sizeof(*slot));
    slot->pid = pid;
    slot->last_seen_gen = g_cache_gen;
    snprintf(slot->cmd, sizeof(slot->cmd), "%s", cmd);
    return slot;
}

static int find_foreground_game(
    const struct sq2_opt_config *cfg,
    pid_t *out_pid,
    char *out_pkg,
    size_t out_pkg_sz,
    enum sq2_engine_type *out_engine) {
    static const char *const cgroup_paths[] = {
        "/dev/cpuset/top-app/cgroup.procs",
        "/dev/stune/top-app/cgroup.procs",
    };
    char buf[8192];
    int n = -1;
    for (size_t i = 0; i < sizeof(cgroup_paths) / sizeof(cgroup_paths[0]); i++) {
        n = read_proc_file(cgroup_paths[i], buf, sizeof(buf));
        if (n > 0) break;
    }
    if (n <= 0) {
        return 0;
    }

    g_cache_gen++;
    static char maps_buf[512 * 1024];
    char *saveptr = NULL;
    for (char *tok = strtok_r(buf, "\r\n \t", &saveptr);
         tok != NULL;
         tok = strtok_r(NULL, "\r\n \t", &saveptr)) {
        pid_t pid = (pid_t)atoi(tok);
        if (pid <= 100) continue;
        uid_t uid = read_proc_uid(pid);
        if (uid < 10000) continue;

        char cmd[128];
        if (read_proc_cmdline(pid, cmd, sizeof(cmd)) != 0 || !cmd[0]) continue;
        if (strncmp(cmd, "com.android.launcher", 20) == 0 ||
            strcmp(cmd, "com.google.android.apps.nexuslauncher") == 0 ||
            strcmp(cmd, "com.android.systemui") == 0) {
            continue;
        }

        struct pid_cls_cache *c = lookup_or_init_cache(pid, cmd);
        enum sq2_engine_type eng = c->apk_eng;
        if (!c->scanned_apk || (eng == SQ2_ENGINE_NONE && c->maps_scans < SQ2_MAX_MAPS_SCANS)) {
            char maps_path[64];
            snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", (int)pid);
            maps_buf[0] = '\0';
            (void)read_proc_file(maps_path, maps_buf, sizeof(maps_buf));
            eng = sq2_classify_app(
                cfg, cmd, maps_buf, NULL, c->scanned_apk ? NULL : "/data/app");
            c->apk_eng = eng;
            c->scanned_apk = 1;
            c->maps_scans++;
        }

        if (eng != SQ2_ENGINE_NONE) {
            *out_pid = pid;
            snprintf(out_pkg, out_pkg_sz, "%s", cmd);
            *out_engine = eng;
            return 1;
        }
    }
    return 0;
}

static void write_pid_file(void) {
    (void)mkdir(SQ2_STATE_DIR, 0755);
    FILE *fp = fopen(SQ2_PID_PATH, "w");
    if (fp) {
        fprintf(fp, "%d\n", (int)getpid());
        fclose(fp);
    }
}

static void sleep_ms(int ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    (void)nanosleep(&req, NULL);
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    write_pid_file();

    struct sq2_opt_config cfg;
    sq2_opt_config_init_default(&cfg);
    (void)sq2_opt_config_load_file(SQ2_CONFIG_PATH, &cfg);
    time_t last_cfg_mtime = 0;
    struct stat cfg_st;
    if (stat(SQ2_CONFIG_PATH, &cfg_st) == 0) {
        last_cfg_mtime = cfg_st.st_mtime;
    }

    struct sq2_dvfs_state dvfs;
    memset(&dvfs, 0, sizeof(dvfs));
    struct pinned_tid_list pinned;
    memset(&pinned, 0, sizeof(pinned));

    pid_t active_pid = 0;
    enum sq2_engine_type active_engine = SQ2_ENGINE_NONE;
    char active_pkg[128] = {0};
    int tick = 0;
    uint64_t last_cfg_check_ms = 0;

    log_msg("sq2-game-optd started (pid=%d)", (int)getpid());

    while (!g_stop) {
        struct timespec mono_now;
        uint64_t now_ms = 0;
        if (clock_gettime(CLOCK_MONOTONIC, &mono_now) == 0) {
            now_ms = (uint64_t)mono_now.tv_sec * 1000ULL +
                     (uint64_t)(mono_now.tv_nsec / 1000000L);
        }
        if (last_cfg_check_ms == 0 || now_ms - last_cfg_check_ms >= 2000ULL) {
            last_cfg_check_ms = now_ms;
            if (stat(SQ2_CONFIG_PATH, &cfg_st) == 0 &&
                cfg_st.st_mtime != last_cfg_mtime) {
                last_cfg_mtime = cfg_st.st_mtime;
                (void)sq2_opt_config_load_file(SQ2_CONFIG_PATH, &cfg);
                log_msg("reloaded config (enabled=%d cpu=%d gpu=%d pin=%d)",
                        cfg.enabled, cfg.boost_cpu, cfg.boost_gpu, cfg.pin_threads);
            }
        }

        pid_t fg_pid = 0;
        enum sq2_engine_type fg_eng = SQ2_ENGINE_NONE;
        char fg_pkg[128] = {0};
        int screen_on = sq2_is_screen_on("/sys");
        int found = (cfg.enabled && screen_on)
                        ? find_foreground_game(&cfg, &fg_pid, fg_pkg, sizeof(fg_pkg), &fg_eng)
                        : 0;

        if (found) {
            if (fg_pid != active_pid || fg_eng != active_engine) {
                log_msg("ENGAGE pid=%d pkg=%s engine=%d", (int)fg_pid, fg_pkg, (int)fg_eng);
                active_pid = fg_pid;
                active_engine = fg_eng;
                snprintf(active_pkg, sizeof(active_pkg), "%s", fg_pkg);
            }
            (void)sq2_dvfs_boost_engage(&dvfs, "/sys", cfg.boost_cpu, cfg.boost_gpu);
            if (!cfg.boost_cpu && dvfs.cpu.active) {
                struct sq2_dvfs_state tmp = {0};
                tmp.cpu = dvfs.cpu;
                memset(&dvfs.cpu, 0, sizeof(dvfs.cpu));
                (void)sq2_dvfs_boost_restore(&tmp);
            }
            if (!cfg.boost_gpu && dvfs.gpu.active) {
                struct sq2_dvfs_state tmp = {0};
                tmp.gpu = dvfs.gpu;
                memset(&dvfs.gpu, 0, sizeof(dvfs.gpu));
                (void)sq2_dvfs_boost_restore(&tmp);
            }
            if (cfg.pin_threads) {
                if ((tick % 4) == 0 || pinned.pid != fg_pid) {
                    pin_game_threads(fg_pid, fg_eng, &pinned);
                }
            } else if (pinned.count > 0) {
                restore_pinned_threads(&pinned);
            }
        } else {
            if (active_pid != 0) {
                log_msg("RESTORE prev_pid=%d prev_pkg=%s reason=%s",
                        (int)active_pid, active_pkg,
                        !cfg.enabled ? "disabled" : (!screen_on ? "screen_off" : "background"));
                active_pid = 0;
                active_engine = SQ2_ENGINE_NONE;
                active_pkg[0] = '\0';
            }
            (void)sq2_dvfs_boost_restore(&dvfs);
            restore_pinned_threads(&pinned);
        }

        tick++;
        sleep_ms(sq2_compute_sleep_ms(&cfg, screen_on, found));
    }

    (void)sq2_dvfs_boost_restore(&dvfs);
    restore_pinned_threads(&pinned);
    (void)unlink(SQ2_PID_PATH);
    log_msg("sq2-game-optd exiting cleanly");
    return 0;
}
