#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <linux/netlink.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum spx_lower_role {
    SPX_ROLE_UNKNOWN = 0,
    SPX_ROLE_DEVICE = 1,
    SPX_ROLE_HOST = 2,
};

struct spx_watch_config {
    const char *helper_path;
    const char *pidfile;
    const char *sysfs_root;
    const char *ucsi_port;
    int host_to_adb_debounce_ms;
    int retry_ms;
    int max_retries;
};

static int has_prefix(const char *s, const char *p) {
    size_t n;
    if (!s || !p) {
        return 0;
    }
    n = strlen(p);
    return strncmp(s, p, n) == 0;
}

static int path_has_component(const char *devpath, const char *component) {
    size_t clen;
    const char *pos;

    if (!devpath || !component || !component[0]) {
        return 0;
    }
    clen = strlen(component);
    pos = devpath;
    while ((pos = strstr(pos, component)) != NULL) {
        int left_ok = (pos == devpath) || (pos[-1] == '/');
        int right_ok = (pos[clen] == '\0') || (pos[clen] == '/');
        if (left_ok && right_ok) {
            return 1;
        }
        pos += clen;
    }
    return 0;
}

static int parse_port_index(const char *name, unsigned long *out_idx) {
    const char *digits;
    char *end = NULL;
    unsigned long val;

    if (!name || !has_prefix(name, "port")) {
        return -1;
    }
    digits = name + 4;
    if (digits[0] < '0' || digits[0] > '9') {
        return -1;
    }
    errno = 0;
    val = strtoul(digits, &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        return -1;
    }
    if (out_idx) {
        *out_idx = val;
    }
    return 0;
}

static int is_valid_port_name(const char *name) {
    return parse_port_index(name, NULL) == 0;
}

static int read_trimmed_file(const char *path, char *buf, size_t buf_sz) {
    FILE *fp;
    size_t n;

    if (!path || !buf || buf_sz == 0) {
        return -1;
    }
    fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    n = fread(buf, 1, buf_sz - 1, fp);
    if (ferror(fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }
    return 0;
}

static int discover_ucsi_port(const char *sysfs_root, char *out, size_t out_sz) {
    char typec_dir[PATH_MAX];
    DIR *dir;
    struct dirent *ent;
    int found_ucsi = 0;
    unsigned long best_ucsi_idx = ULONG_MAX;
    char best_ucsi_name[64] = "";
    int found_port0 = 0;

    if (!sysfs_root || !out || out_sz == 0) {
        return -1;
    }
    if (snprintf(typec_dir, sizeof(typec_dir), "%s/class/typec", sysfs_root) >= (int)sizeof(typec_dir)) {
        return -1;
    }
    dir = opendir(typec_dir);
    if (!dir) {
        return -1;
    }
    while ((ent = readdir(dir)) != NULL) {
        unsigned long idx = 0;
        char entry_path[PATH_MAX];
        char link_target[PATH_MAX];
        ssize_t rlen;

        if (parse_port_index(ent->d_name, &idx) != 0) {
            continue;
        }
        if (strcmp(ent->d_name, "port0") == 0) {
            found_port0 = 1;
        }
        if (snprintf(entry_path, sizeof(entry_path), "%s/%s", typec_dir, ent->d_name) >= (int)sizeof(entry_path)) {
            continue;
        }
        rlen = readlink(entry_path, link_target, sizeof(link_target) - 1);
        if (rlen > 0) {
            link_target[rlen] = '\0';
            if (strstr(link_target, "spx-ucsi-transport") != NULL) {
                size_t namelen = strlen(ent->d_name);
                if ((!found_ucsi || idx < best_ucsi_idx) && namelen < sizeof(best_ucsi_name)) {
                    found_ucsi = 1;
                    best_ucsi_idx = idx;
                    memcpy(best_ucsi_name, ent->d_name, namelen + 1);
                }
            }
        }
    }
    closedir(dir);

    if (found_ucsi) {
        if (snprintf(out, out_sz, "%s", best_ucsi_name) >= (int)out_sz) {
            return -1;
        }
        return 0;
    }
    if (found_port0) {
        if (snprintf(out, out_sz, "port0") >= (int)out_sz) {
            return -1;
        }
        return 0;
    }
    return -1;
}

static int parse_storage_boot_locked(const char *sysfs_root) {
    char path[PATH_MAX];
    char buf[64];
    char *end = NULL;
    unsigned long mask;

    if (snprintf(path, sizeof(path), "%s/bus/platform/devices/surface-typec/storage_ports", sysfs_root) >= (int)sizeof(path)) {
        return 0;
    }
    if (read_trimmed_file(path, buf, sizeof(buf)) != 0 || buf[0] == '\0') {
        return 0;
    }
    errno = 0;
    mask = strtoul(buf, &end, 0);
    if (errno != 0 || !end || *end != '\0') {
        return 0;
    }
    return (mask & 1UL) != 0 ? 1 : 0;
}

static int read_dp_state_needs_host(const char *sysfs_root) {
    char path[PATH_MAX];
    char buf[64];

    if (snprintf(path, sizeof(path), "%s/bus/platform/devices/surface-typec/lower_dp_state", sysfs_root) >= (int)sizeof(path)) {
        return 0;
    }
    if (read_trimmed_file(path, buf, sizeof(buf)) != 0) {
        return 0;
    }
    return strcmp(buf, "pending") == 0 || strcmp(buf, "active") == 0;
}

static int read_ucsi_partner_is_host(const char *sysfs_root, const char *port_name) {
    char partner_path[PATH_MAX];
    char role_path[PATH_MAX];
    char role_buf[128];
    struct stat st;

    if (!port_name || !port_name[0]) {
        return 0;
    }
    if (snprintf(partner_path, sizeof(partner_path), "%s/class/typec/%s/%s-partner",
                 sysfs_root, port_name, port_name) >= (int)sizeof(partner_path)) {
        return 0;
    }
    if (stat(partner_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return 0;
    }
    if (snprintf(role_path, sizeof(role_path), "%s/class/typec/%s/data_role",
                 sysfs_root, port_name) >= (int)sizeof(role_path)) {
        return 0;
    }
    if (read_trimmed_file(role_path, role_buf, sizeof(role_buf)) != 0) {
        return 0;
    }
    return strstr(role_buf, "[host]") != NULL ? 1 : 0;
}

static int has_lower_xhci_devices(const char *sysfs_root) {
    char base[PATH_MAX];
    DIR *base_dir;
    struct dirent *hcd_ent;

    if (snprintf(base, sizeof(base), "%s/devices/platform/soc@0/a6f8800.usb/a600000.usb",
                 sysfs_root) >= (int)sizeof(base)) {
        return 0;
    }
    base_dir = opendir(base);
    if (!base_dir) {
        return 0;
    }
    while ((hcd_ent = readdir(base_dir)) != NULL) {
        char hcd_path[PATH_MAX];
        DIR *hcd_dir;
        struct dirent *bus_ent;

        if (!has_prefix(hcd_ent->d_name, "xhci-hcd.")) {
            continue;
        }
        if (snprintf(hcd_path, sizeof(hcd_path), "%s/%s", base, hcd_ent->d_name) >= (int)sizeof(hcd_path)) {
            continue;
        }
        hcd_dir = opendir(hcd_path);
        if (!hcd_dir) {
            continue;
        }
        while ((bus_ent = readdir(hcd_dir)) != NULL) {
            char bus_path[PATH_MAX];
            DIR *bus_dir;
            struct dirent *dev_ent;

            if (!has_prefix(bus_ent->d_name, "usb")) {
                continue;
            }
            if (snprintf(bus_path, sizeof(bus_path), "%s/%s", hcd_path, bus_ent->d_name) >= (int)sizeof(bus_path)) {
                continue;
            }
            bus_dir = opendir(bus_path);
            if (!bus_dir) {
                continue;
            }
            while ((dev_ent = readdir(bus_dir)) != NULL) {
                char vendor_path[PATH_MAX];
                struct stat st;

                if (dev_ent->d_name[0] == '.') {
                    continue;
                }
                if (snprintf(vendor_path, sizeof(vendor_path), "%s/%s/idVendor",
                             bus_path, dev_ent->d_name) >= (int)sizeof(vendor_path)) {
                    continue;
                }
                if (stat(vendor_path, &st) == 0 && S_ISREG(st.st_mode)) {
                    closedir(bus_dir);
                    closedir(hcd_dir);
                    closedir(base_dir);
                    return 1;
                }
            }
            closedir(bus_dir);
        }
        closedir(hcd_dir);
    }
    closedir(base_dir);
    return 0;
}

static enum spx_lower_role read_current_role(const char *sysfs_root) {
    char path[PATH_MAX];
    char buf[64];

    if (snprintf(path, sizeof(path), "%s/bus/platform/devices/surface-typec/lower_data_role",
                 sysfs_root) >= (int)sizeof(path)) {
        return SPX_ROLE_UNKNOWN;
    }
    if (read_trimmed_file(path, buf, sizeof(buf)) != 0) {
        return SPX_ROLE_UNKNOWN;
    }
    if (strcmp(buf, "host") == 0) {
        return SPX_ROLE_HOST;
    }
    if (strcmp(buf, "device") == 0) {
        return SPX_ROLE_DEVICE;
    }
    return SPX_ROLE_UNKNOWN;
}

static enum spx_lower_role evaluate_desired_role(const struct spx_watch_config *cfg) {
    char port_buf[64];
    const char *port_name = NULL;

    if (parse_storage_boot_locked(cfg->sysfs_root)) {
        return SPX_ROLE_HOST;
    }
    if (read_dp_state_needs_host(cfg->sysfs_root)) {
        return SPX_ROLE_HOST;
    }
    if (cfg->ucsi_port && cfg->ucsi_port[0]) {
        port_name = cfg->ucsi_port;
    } else if (discover_ucsi_port(cfg->sysfs_root, port_buf, sizeof(port_buf)) == 0) {
        port_name = port_buf;
    }
    if (port_name && read_ucsi_partner_is_host(cfg->sysfs_root, port_name)) {
        return SPX_ROLE_HOST;
    }
    if (read_current_role(cfg->sysfs_root) == SPX_ROLE_HOST &&
        has_lower_xhci_devices(cfg->sysfs_root)) {
        return SPX_ROLE_HOST;
    }
    return SPX_ROLE_DEVICE;
}

static int uevent_record_is_relevant(const char *buf, size_t len, const char *ucsi_port) {
    const char *action = NULL;
    const char *devpath = NULL;
    const char *subsystem = NULL;
    const char *typec_port = NULL;
    int has_spx_dp = 0;
    size_t pos = 0;

    if (!buf || len == 0) {
        return 0;
    }
    while (pos < len) {
        const char *field = buf + pos;
        size_t flen = strnlen(field, len - pos);
        if (flen > 0) {
            if (has_prefix(field, "ACTION=")) {
                action = field + 7;
            } else if (has_prefix(field, "DEVPATH=")) {
                devpath = field + 8;
            } else if (has_prefix(field, "SUBSYSTEM=")) {
                subsystem = field + 10;
            } else if (has_prefix(field, "TYPEC_PORT=")) {
                typec_port = field + 11;
            } else if (has_prefix(field, "SPX_LOWER_DP=")) {
                has_spx_dp = 1;
            }
        }
        pos += flen + 1;
    }

    if (has_spx_dp) {
        return 1;
    }
    if (!subsystem) {
        return 0;
    }
    if (strcmp(subsystem, "platform") == 0) {
        return path_has_component(devpath, "surface-typec");
    }
    if (strcmp(subsystem, "typec") == 0) {
        if (ucsi_port && ucsi_port[0]) {
            if (typec_port && strcmp(typec_port, ucsi_port) == 0) {
                return 1;
            }
            if (path_has_component(devpath, ucsi_port)) {
                return 1;
            }
            return 0;
        }
        return (typec_port && typec_port[0]) || (devpath && strstr(devpath, "/typec/port") != NULL);
    }
    if (strcmp(subsystem, "usb") == 0) {
        if (action && strcmp(action, "remove") == 0 && path_has_component(devpath, "a600000.usb")) {
            return 1;
        }
        return 0;
    }
    return 0;
}

static volatile sig_atomic_t g_stop = 0;

static void on_stop_signal(int signo) {
    (void)signo;
    g_stop = 1;
}

static int64_t now_monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000L);
}

static int run_helper_apply_role(const struct spx_watch_config *cfg, enum spx_lower_role target) {
    const char *mode_arg = target == SPX_ROLE_HOST ? "host" : "adb";
    pid_t pid = fork();
    int status = 0;

    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        char *const argv[] = {
            (char *)cfg->helper_path,
            (char *)"apply-role",
            (char *)mode_arg,
            NULL
        };
        if (cfg->sysfs_root && strcmp(cfg->sysfs_root, "/sys") != 0) {
            setenv("SPX_LOWER_USB_SYSFS", cfg->sysfs_root, 1);
        }
        execv(cfg->helper_path, argv);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
}

static int write_pidfile(const char *pidfile) {
    char tmp[PATH_MAX];
    FILE *fp;
    int n;

    if (!pidfile || !pidfile[0]) {
        return 0;
    }
    n = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", pidfile, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    fp = fopen(tmp, "w");
    if (!fp) {
        return -1;
    }
    if (fprintf(fp, "%ld\n", (long)getpid()) < 0 || fclose(fp) != 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, pidfile) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int open_uevent_socket(void) {
    int fd;
    int rcvbuf = 262144;
    struct sockaddr_nl addr;

    fd = socket(PF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) {
        perror("socket(NETLINK_KOBJECT_UEVENT)");
        return -1;
    }
#ifdef SO_RCVBUFFORCE
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf, sizeof(rcvbuf)) < 0) {
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    }
#else
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
#endif

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = (uint32_t)getpid();
    addr.nl_groups = 1;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind(NETLINK_KOBJECT_UEVENT)");
        close(fd);
        return -1;
    }
    return fd;
}

static int drain_uevents(int fd, const struct spx_watch_config *cfg) {
    char buf[8192];
    char port_buf[64];
    const char *port_name = cfg->ucsi_port;
    int relevant = 0;

    if (!port_name || !port_name[0]) {
        if (discover_ucsi_port(cfg->sysfs_root, port_buf, sizeof(port_buf)) == 0) {
            port_name = port_buf;
        }
    }

    for (;;) {
        ssize_t len = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                if (g_stop) {
                    break;
                }
                continue;
            }
            if (errno == ENOBUFS) {
                relevant = 1;
                continue;
            }
            break;
        }
        if (len == 0) {
            break;
        }
        buf[len] = '\0';
        if (uevent_record_is_relevant(buf, (size_t)len, port_name)) {
            relevant = 1;
        }
    }
    return relevant;
}

struct spx_watch_state {
    int adb_debounce_pending;
    int64_t adb_deadline_ms;
    int retry_pending;
    int retry_count;
    enum spx_lower_role retry_target;
    int64_t retry_deadline_ms;
};

static void arm_retry(struct spx_watch_state *st, const struct spx_watch_config *cfg,
                      enum spx_lower_role target, int64_t now) {
    if (st->retry_target != target) {
        st->retry_target = target;
        st->retry_count = 0;
    }
    if (st->retry_count < cfg->max_retries && cfg->retry_ms > 0) {
        st->retry_count++;
        st->retry_pending = 1;
        st->retry_deadline_ms = now + (int64_t)cfg->retry_ms;
    } else {
        st->retry_pending = 0;
    }
}

static int reconcile_role(const struct spx_watch_config *cfg, struct spx_watch_state *st,
                          int allow_debounce, int refresh_debounce) {
    enum spx_lower_role cur = read_current_role(cfg->sysfs_root);
    enum spx_lower_role want = evaluate_desired_role(cfg);
    int64_t now = now_monotonic_ms();
    int rc;

    if (cur == SPX_ROLE_UNKNOWN || want == SPX_ROLE_UNKNOWN || want == cur) {
        st->adb_debounce_pending = 0;
        st->retry_pending = 0;
        st->retry_count = 0;
        st->retry_target = SPX_ROLE_UNKNOWN;
        return 0;
    }

    if (want == SPX_ROLE_HOST) {
        st->adb_debounce_pending = 0;
        rc = run_helper_apply_role(cfg, SPX_ROLE_HOST);
        if (rc == 0) {
            st->retry_pending = 0;
            st->retry_count = 0;
            st->retry_target = SPX_ROLE_UNKNOWN;
        } else {
            arm_retry(st, cfg, SPX_ROLE_HOST, now);
        }
        return rc;
    }

    if (allow_debounce && cfg->host_to_adb_debounce_ms > 0) {
        st->retry_pending = 0;
        if (!st->adb_debounce_pending || refresh_debounce) {
            st->adb_debounce_pending = 1;
            st->adb_deadline_ms = now + (int64_t)cfg->host_to_adb_debounce_ms;
        }
        return 0;
    }

    st->adb_debounce_pending = 0;
    rc = run_helper_apply_role(cfg, SPX_ROLE_DEVICE);
    if (rc == 0) {
        st->retry_pending = 0;
        st->retry_count = 0;
        st->retry_target = SPX_ROLE_UNKNOWN;
    } else {
        arm_retry(st, cfg, SPX_ROLE_DEVICE, now);
    }
    return rc;
}

static int run_test_stdin(const struct spx_watch_config *cfg, struct spx_watch_state *st) {
    char rec[8192];
    size_t rec_len = 0;
    char line[1024];
    char port_buf[64];
    const char *port_name = cfg->ucsi_port;

    if (!port_name || !port_name[0]) {
        if (discover_ucsi_port(cfg->sysfs_root, port_buf, sizeof(port_buf)) == 0) {
            port_name = port_buf;
        }
    }

    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t llen = strlen(line);
        while (llen > 0 && (line[llen - 1] == '\n' || line[llen - 1] == '\r')) {
            line[--llen] = '\0';
        }
        if (llen == 0) {
            if (rec_len > 0) {
                if (uevent_record_is_relevant(rec, rec_len, port_name)) {
                    (void)reconcile_role(cfg, st, 1, 1);
                }
                rec_len = 0;
            }
            continue;
        }
        if (rec_len + llen + 1 < sizeof(rec)) {
            memcpy(rec + rec_len, line, llen);
            rec_len += llen;
            rec[rec_len++] = '\0';
        }
    }
    if (rec_len > 0 && uevent_record_is_relevant(rec, rec_len, port_name)) {
        (void)reconcile_role(cfg, st, 1, 1);
    }
    if (st->adb_debounce_pending) {
        if (cfg->host_to_adb_debounce_ms > 0) {
            struct timespec ts;
            ts.tv_sec = cfg->host_to_adb_debounce_ms / 1000;
            ts.tv_nsec = (long)(cfg->host_to_adb_debounce_ms % 1000) * 1000000L;
            nanosleep(&ts, NULL);
        }
        st->adb_debounce_pending = 0;
        (void)reconcile_role(cfg, st, 0, 0);
    }
    return 0;
}

int main(int argc, char **argv) {
    struct spx_watch_config cfg = {
        .helper_path = "/bin/spx-lower-usb",
        .pidfile = NULL,
        .sysfs_root = "/sys",
        .ucsi_port = NULL,
        .host_to_adb_debounce_ms = 500,
        .retry_ms = 1000,
        .max_retries = 3,
    };
    struct spx_watch_state st;
    int oneshot = 0;
    int test_stdin = 0;
    int pidfile_set = 0;
    int fd = -1;
    int i;
    struct sigaction sa;
    struct sigaction ign;

    memset(&st, 0, sizeof(st));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--helper") == 0 && i + 1 < argc) {
            cfg.helper_path = argv[++i];
        } else if (strcmp(argv[i], "--pidfile") == 0 && i + 1 < argc) {
            cfg.pidfile = argv[++i];
            pidfile_set = 1;
        } else if (strcmp(argv[i], "--sysfs") == 0 && i + 1 < argc) {
            cfg.sysfs_root = argv[++i];
        } else if (strcmp(argv[i], "--ucsi-port") == 0 && i + 1 < argc) {
            cfg.ucsi_port = argv[++i];
            if (!is_valid_port_name(cfg.ucsi_port)) {
                fprintf(stderr, "Invalid UCSI port name: %s\n", cfg.ucsi_port);
                return 64;
            }
        } else if (strcmp(argv[i], "--host-to-adb-debounce-ms") == 0 && i + 1 < argc) {
            long val = strtol(argv[++i], NULL, 10);
            if (val < 0) {
                val = 0;
            } else if (val > 10000) {
                val = 10000;
            }
            cfg.host_to_adb_debounce_ms = (int)val;
        } else if (strcmp(argv[i], "--retry-ms") == 0 && i + 1 < argc) {
            long val = strtol(argv[++i], NULL, 10);
            if (val < 50) {
                val = 50;
            } else if (val > 30000) {
                val = 30000;
            }
            cfg.retry_ms = (int)val;
        } else if (strcmp(argv[i], "--once") == 0) {
            oneshot = 1;
        } else if (strcmp(argv[i], "--test-stdin") == 0) {
            test_stdin = 1;
        } else {
            fprintf(stderr,
                    "Usage: %s [--helper PATH] [--pidfile PATH] [--sysfs PATH] "
                    "[--ucsi-port PORT] [--host-to-adb-debounce-ms N] [--retry-ms N] "
                    "[--once] [--test-stdin]\n",
                    argv[0]);
            return 64;
        }
    }

    if (!pidfile_set && !oneshot && !test_stdin) {
        cfg.pidfile = "/run/spx-lower-usb-watch.pid";
    }
    if (cfg.pidfile && cfg.pidfile[0] == '\0') {
        cfg.pidfile = NULL;
    }

    memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGPIPE, &ign, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    if (oneshot) {
        int rc = reconcile_role(&cfg, &st, 0, 0);
        return rc < 0 ? 1 : rc;
    }

    if (test_stdin) {
        return run_test_stdin(&cfg, &st);
    }

    fd = open_uevent_socket();
    if (fd < 0) {
        return 1;
    }

    if (write_pidfile(cfg.pidfile) != 0) {
        perror("write_pidfile");
        close(fd);
        return 1;
    }

    (void)reconcile_role(&cfg, &st, 0, 0);

    while (!g_stop) {
        struct pollfd pfd;
        int timeout_ms = -1;
        int64_t now = now_monotonic_ms();
        int64_t next_deadline = 0;
        int have_deadline = 0;
        int prc;

        if (st.adb_debounce_pending) {
            next_deadline = st.adb_deadline_ms;
            have_deadline = 1;
        }
        if (st.retry_pending && (!have_deadline || st.retry_deadline_ms < next_deadline)) {
            next_deadline = st.retry_deadline_ms;
            have_deadline = 1;
        }
        if (have_deadline) {
            int64_t rem = next_deadline - now;
            if (rem <= 0) {
                if (st.adb_debounce_pending && now >= st.adb_deadline_ms) {
                    st.adb_debounce_pending = 0;
                    (void)reconcile_role(&cfg, &st, 0, 0);
                } else if (st.retry_pending && now >= st.retry_deadline_ms) {
                    st.retry_pending = 0;
                    (void)reconcile_role(&cfg, &st, 0, 0);
                }
                continue;
            }
            timeout_ms = rem > (int64_t)INT_MAX ? INT_MAX : (int)rem;
        }

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        prc = poll(&pfd, 1, timeout_ms);
        if (prc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }
        if (prc == 0) {
            now = now_monotonic_ms();
            if (st.adb_debounce_pending && now >= st.adb_deadline_ms) {
                st.adb_debounce_pending = 0;
                (void)reconcile_role(&cfg, &st, 0, 0);
            } else if (st.retry_pending && now >= st.retry_deadline_ms) {
                st.retry_pending = 0;
                (void)reconcile_role(&cfg, &st, 0, 0);
            }
            continue;
        }
        if (pfd.revents & POLLIN) {
            if (drain_uevents(fd, &cfg)) {
                (void)reconcile_role(&cfg, &st, 1, 1);
            }
        }
    }

    close(fd);
    if (cfg.pidfile) {
        unlink(cfg.pidfile);
    }
    return 0;
}
