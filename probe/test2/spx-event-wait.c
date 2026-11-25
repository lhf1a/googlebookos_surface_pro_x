#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/netlink.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_EVDEV_FDS 16
#define MAX_INOTIFY_DIRS 8

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

static int csv_contains(const char *csv, const char *needle) {
    if (!csv || !csv[0]) {
        return 1;
    }
    if (!needle || !needle[0]) {
        return 0;
    }
    size_t nlen = strlen(needle);
    const char *p = csv;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t tok_len = comma ? (size_t)(comma - p) : strlen(p);
        if (tok_len == 1 && p[0] == '*') {
            return 1;
        }
        if (tok_len == nlen && memcmp(p, needle, nlen) == 0) {
            return 1;
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
    return 0;
}

static int emit_line(const char *line) {
    if (fputs(line, stdout) == EOF || fputc('\n', stdout) == EOF || fflush(stdout) != 0) {
        return -1;
    }
    return 0;
}

static int check_any_exists_csv(const char *csv) {
    if (!csv || !csv[0]) {
        return 0;
    }
    const char *p = csv;
    char path[PATH_MAX];
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len > 0 && len < sizeof(path)) {
            memcpy(path, p, len);
            path[len] = '\0';
            if (access(path, F_OK) == 0) {
                return 1;
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
    return 0;
}

static int read_trimmed_file(const char *path, char *out, size_t out_sz) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = read(fd, out, out_sz - 1);
    close(fd);
    if (n < 0) {
        return -1;
    }
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ' || out[n - 1] == '\t')) {
        out[--n] = '\0';
    }
    return 0;
}

/* spec format: "<path>:<expected>" (splits on last ':') */
static int check_file_diff(const char *spec) {
    if (!spec || !spec[0]) {
        return 0;
    }
    const char *colon = strrchr(spec, ':');
    if (!colon || colon == spec) {
        return 0;
    }
    char path[PATH_MAX];
    size_t plen = (size_t)(colon - spec);
    if (plen >= sizeof(path)) {
        return 0;
    }
    memcpy(path, spec, plen);
    path[plen] = '\0';
    const char *expected = colon + 1;
    char actual[256];
    if (read_trimmed_file(path, actual, sizeof(actual)) != 0) {
        return strcmp(expected, "unavailable") != 0 ? 1 : 0;
    }
    return strcmp(actual, expected) != 0 ? 1 : 0;
}

static int open_uevent_socket(void) {
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) {
        return -1;
    }
    int rcvbuf = 256 * 1024;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = 0;
    sa.nl_groups = 1;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int drain_uevent_fd(int fd, const char *subsys_csv) {
    char buf[8192];
    int matched = 0;
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == ENOBUFS) {
                /* Overrun means events arrived; trigger a rescan. */
                matched = 1;
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        buf[n] = '\0';
        const char *p = buf;
        const char *end = buf + n;
        while (p < end) {
            if (strncmp(p, "SUBSYSTEM=", 10) == 0) {
                if (csv_contains(subsys_csv, p + 10)) {
                    matched = 1;
                }
                break;
            }
            p += strlen(p) + 1;
        }
    }
    return matched;
}

static int compute_poll_timeout(int timeout_ms, uint64_t deadline_ms) {
    if (timeout_ms < 0) {
        return -1;
    }
    uint64_t now = monotonic_ms();
    if (now >= deadline_ms) {
        return 0;
    }
    uint64_t rem = deadline_ms - now;
    return rem > (uint64_t)INT_MAX ? INT_MAX : (int)rem;
}

static int run_uevent(const char *subsys_csv, int stream, int debounce_ms, int timeout_ms,
                      const char *check_exists, const char *check_diff) {
    int fd = open_uevent_socket();
    if (fd < 0) {
        return 1;
    }
    if (stream) {
        if (emit_line("init") != 0) {
            close(fd);
            return 0;
        }
    } else {
        if (check_any_exists_csv(check_exists)) {
            (void)emit_line("exists");
            close(fd);
            return 0;
        }
        if (check_file_diff(check_diff)) {
            (void)emit_line("changed");
            close(fd);
            return 0;
        }
    }

    uint64_t deadline = timeout_ms >= 0 ? monotonic_ms() + (uint64_t)timeout_ms : 0;
    while (!g_stop) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int wait_ms = compute_poll_timeout(timeout_ms, deadline);
        int rc = poll(&pfd, 1, wait_ms);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc == 0) {
            if (emit_line("timeout") != 0 || !stream) {
                break;
            }
            deadline = monotonic_ms() + (uint64_t)timeout_ms;
            continue;
        }
        if (pfd.revents & POLLIN) {
            if (drain_uevent_fd(fd, subsys_csv)) {
                if (debounce_ms > 0) {
                    uint64_t db_end = monotonic_ms() + (uint64_t)debounce_ms;
                    while (!g_stop) {
                        int rem = compute_poll_timeout(debounce_ms, db_end);
                        if (rem <= 0) {
                            break;
                        }
                        pfd.revents = 0;
                        int drc = poll(&pfd, 1, rem);
                        if (drc > 0 && (pfd.revents & POLLIN)) {
                            (void)drain_uevent_fd(fd, subsys_csv);
                        } else if (drc <= 0 && errno != EINTR) {
                            break;
                        }
                    }
                }
                if (emit_line("event") != 0 || !stream) {
                    break;
                }
                if (timeout_ms >= 0) {
                    deadline = monotonic_ms() + (uint64_t)timeout_ms;
                }
            }
        }
    }

    close(fd);
    return 0;
}

static int drain_inotify_fd(int fd, int primary_wd, const char *match_csv, int *dir_removed) {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    int matched = 0;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        const char *ptr = buf;
        while (ptr < buf + n) {
            const struct inotify_event *ev = (const struct inotify_event *)ptr;
            if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) {
                if (dir_removed && ev->wd == primary_wd) {
                    *dir_removed = 1;
                }
            } else if (ev->mask & IN_Q_OVERFLOW) {
                matched = 1;
            } else if (!match_csv || !match_csv[0]) {
                matched = 1;
            } else if (ev->len > 0 && csv_contains(match_csv, ev->name)) {
                matched = 1;
            }
            ptr += sizeof(struct inotify_event) + ev->len;
        }
    }
    return matched;
}

static int run_inotify(const char *const *dirs, size_t dir_count, const char *match_csv, int stream,
                       int debounce_ms, int timeout_ms, const char *check_exists,
                       const char *check_diff) {
    if (dir_count == 0) {
        return 1;
    }
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return 1;
    }
    uint32_t mask = IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO | IN_ATTRIB |
                    IN_DELETE_SELF | IN_MOVE_SELF;
    int primary_wd = -1;
    for (size_t i = 0; i < dir_count; i++) {
        int wd = inotify_add_watch(fd, dirs[i], mask);
        if (wd < 0) {
            if (i == 0) {
                close(fd);
                return 1;
            }
            continue;
        }
        if (i == 0) {
            primary_wd = wd;
        }
    }

    if (stream) {
        if (emit_line("init") != 0) {
            close(fd);
            return 0;
        }
    } else {
        if (check_any_exists_csv(check_exists)) {
            (void)emit_line("exists");
            close(fd);
            return 0;
        }
        if (check_file_diff(check_diff)) {
            (void)emit_line("changed");
            close(fd);
            return 0;
        }
    }

    uint64_t deadline = timeout_ms >= 0 ? monotonic_ms() + (uint64_t)timeout_ms : 0;
    int exit_code = 0;
    while (!g_stop) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int wait_ms = compute_poll_timeout(timeout_ms, deadline);
        int rc = poll(&pfd, 1, wait_ms);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc == 0) {
            if (emit_line("timeout") != 0 || !stream) {
                break;
            }
            deadline = monotonic_ms() + (uint64_t)timeout_ms;
            continue;
        }
        if (pfd.revents & POLLIN) {
            int dir_removed = 0;
            int matched = drain_inotify_fd(fd, primary_wd, match_csv, &dir_removed);
            if (dir_removed) {
                (void)emit_line("removed");
                exit_code = 2;
                break;
            }
            if (matched) {
                if (debounce_ms > 0) {
                    uint64_t db_end = monotonic_ms() + (uint64_t)debounce_ms;
                    while (!g_stop) {
                        int rem = compute_poll_timeout(debounce_ms, db_end);
                        if (rem <= 0) {
                            break;
                        }
                        pfd.revents = 0;
                        int drc = poll(&pfd, 1, rem);
                        if (drc > 0 && (pfd.revents & POLLIN)) {
                            (void)drain_inotify_fd(fd, primary_wd, match_csv, &dir_removed);
                        } else if (drc <= 0 && errno != EINTR) {
                            break;
                        }
                    }
                }
                if (emit_line("event") != 0 || !stream) {
                    break;
                }
                if (timeout_ms >= 0) {
                    deadline = monotonic_ms() + (uint64_t)timeout_ms;
                }
            }
        }
    }

    close(fd);
    return exit_code;
}

static void clear_sysfs_pollpri(int fd) {
    char dummy[128];
    ssize_t n = pread(fd, dummy, sizeof(dummy), 0);
    (void)n;
}

static int run_sysfs(const char *sysfs_path, int stream, int debounce_ms,
                     int timeout_ms, const char *check_diff) {
    int fd = open(sysfs_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 1;
    }
    clear_sysfs_pollpri(fd);

    if (stream) {
        if (emit_line("init") != 0) {
            close(fd);
            return 0;
        }
    } else if (check_file_diff(check_diff)) {
        (void)emit_line("changed");
        close(fd);
        return 0;
    }

    uint64_t deadline = timeout_ms >= 0 ? monotonic_ms() + (uint64_t)timeout_ms : 0;
    while (!g_stop) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLPRI | POLLERR;
        pfd.revents = 0;

        int wait_ms = compute_poll_timeout(timeout_ms, deadline);
        int rc = poll(&pfd, 1, wait_ms);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc == 0) {
            if (emit_line("timeout") != 0 || !stream) {
                break;
            }
            deadline = monotonic_ms() + (uint64_t)timeout_ms;
            continue;
        }
        if (pfd.revents & (POLLPRI | POLLERR)) {
            clear_sysfs_pollpri(fd);
            if (debounce_ms > 0) {
                uint64_t db_end = monotonic_ms() + (uint64_t)debounce_ms;
                while (!g_stop) {
                    int rem = compute_poll_timeout(debounce_ms, db_end);
                    if (rem <= 0) {
                        break;
                    }
                    pfd.revents = 0;
                    int drc = poll(&pfd, 1, rem);
                    if (drc > 0 && (pfd.revents & (POLLPRI | POLLERR))) {
                        clear_sysfs_pollpri(fd);
                    } else if (drc <= 0 && errno != EINTR) {
                        break;
                    }
                }
            }
            if (emit_line("event") != 0 || !stream) {
                break;
            }
            if (timeout_ms >= 0) {
                deadline = monotonic_ms() + (uint64_t)timeout_ms;
            }
        }
    }

    close(fd);
    return 0;
}

static size_t open_matching_evdevs(const char *dev_dir, const char *names_csv, int *fds, size_t max_fds) {
    size_t count = 0;
    DIR *d = opendir(dev_dir);
    if (!d) {
        return 0;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && count < max_fds) {
        if (strncmp(de->d_name, "event", 5) != 0 || !isdigit((unsigned char)de->d_name[5])) {
            continue;
        }
        char path[PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s/%s", dev_dir, de->d_name);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            continue;
        }
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        char name[256];
        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 && csv_contains(names_csv, name)) {
            fds[count++] = fd;
        } else {
            close(fd);
        }
    }
    closedir(d);
    return count;
}

static void close_evdevs(int *fds, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (fds[i] >= 0) {
            close(fds[i]);
            fds[i] = -1;
        }
    }
}

static int drain_evdev_fd(int fd) {
    struct input_event ev[16];
    int saw_event = 0;
    for (;;) {
        ssize_t n = read(fd, ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        size_t cnt = (size_t)n / sizeof(struct input_event);
        for (size_t i = 0; i < cnt; i++) {
            if (ev[i].type == EV_SW || ev[i].type == EV_KEY) {
                saw_event = 1;
            }
        }
    }
    return saw_event;
}

static int run_evdev_watch(const char *names_csv, const char *dev_dir, int stream,
                           int debounce_ms, int timeout_ms, const char *check_diff) {
    int uev_fd = open_uevent_socket();
    int ev_fds[MAX_EVDEV_FDS];
    size_t ev_cnt = open_matching_evdevs(dev_dir, names_csv, ev_fds, MAX_EVDEV_FDS);

    if (uev_fd < 0 && ev_cnt == 0) {
        return 1;
    }

    if (stream) {
        if (emit_line("init") != 0) {
            close_evdevs(ev_fds, ev_cnt);
            if (uev_fd >= 0) close(uev_fd);
            return 0;
        }
    } else if (check_file_diff(check_diff)) {
        (void)emit_line("changed");
        close_evdevs(ev_fds, ev_cnt);
        if (uev_fd >= 0) close(uev_fd);
        return 0;
    }

    uint64_t deadline = timeout_ms >= 0 ? monotonic_ms() + (uint64_t)timeout_ms : 0;
    while (!g_stop) {
        struct pollfd pfds[MAX_EVDEV_FDS + 1];
        nfds_t nfds = 0;
        int uev_idx = -1;
        if (uev_fd >= 0) {
            uev_idx = (int)nfds;
            pfds[nfds].fd = uev_fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
        for (size_t i = 0; i < ev_cnt; i++) {
            pfds[nfds].fd = ev_fds[i];
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        int wait_ms = compute_poll_timeout(timeout_ms, deadline);
        int rc = poll(pfds, nfds, wait_ms);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc == 0) {
            if (emit_line("timeout") != 0 || !stream) {
                break;
            }
            deadline = monotonic_ms() + (uint64_t)timeout_ms;
            continue;
        }

        int triggered = 0;
        int need_reopen = 0;
        if (uev_idx >= 0 && (pfds[uev_idx].revents & POLLIN)) {
            if (drain_uevent_fd(uev_fd, "input,surface_aggregator")) {
                triggered = 1;
                need_reopen = 1;
            }
        }
        size_t base = (uev_idx >= 0) ? 1 : 0;
        for (size_t i = 0; i < ev_cnt; i++) {
            short rev = pfds[base + i].revents;
            if (rev & (POLLERR | POLLHUP | POLLNVAL)) {
                triggered = 1;
                need_reopen = 1;
            } else if (rev & POLLIN) {
                int r = drain_evdev_fd(ev_fds[i]);
                if (r < 0) {
                    triggered = 1;
                    need_reopen = 1;
                } else if (r > 0) {
                    triggered = 1;
                }
            }
        }

        if (triggered) {
            if (debounce_ms > 0) {
                struct timespec req;
                req.tv_sec = debounce_ms / 1000;
                req.tv_nsec = (long)(debounce_ms % 1000) * 1000000L;
                (void)nanosleep(&req, NULL);
                if (uev_fd >= 0) {
                    (void)drain_uevent_fd(uev_fd, "input,surface_aggregator");
                }
                for (size_t i = 0; i < ev_cnt; i++) {
                    (void)drain_evdev_fd(ev_fds[i]);
                }
            }
            if (emit_line("event") != 0 || !stream) {
                break;
            }
            if (need_reopen) {
                close_evdevs(ev_fds, ev_cnt);
                ev_cnt = open_matching_evdevs(dev_dir, names_csv, ev_fds, MAX_EVDEV_FDS);
            }
            if (timeout_ms >= 0) {
                deadline = monotonic_ms() + (uint64_t)timeout_ms;
            }
        }
    }

    close_evdevs(ev_fds, ev_cnt);
    if (uev_fd >= 0) close(uev_fd);
    return 0;
}

int main(int argc, char **argv) {
    enum { MODE_NONE = 0, MODE_UEVENT, MODE_INOTIFY, MODE_SYSFS, MODE_EVDEV } mode = MODE_NONE;
    const char *target = NULL;
    const char *inotify_dirs[MAX_INOTIFY_DIRS];
    size_t inotify_dir_count = 0;
    const char *match_csv = NULL;
    const char *check_exists = NULL;
    const char *check_diff = NULL;
    const char *dev_dir = "/dev/input";
    int stream = 0;
    int debounce_ms = 50;
    int timeout_ms = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--uevent") == 0 && i + 1 < argc) {
            mode = MODE_UEVENT;
            target = argv[++i];
        } else if ((strcmp(argv[i], "--inotify") == 0 || strcmp(argv[i], "--inotify-extra") == 0) && i + 1 < argc) {
            mode = MODE_INOTIFY;
            const char *dir = argv[++i];
            if (!target) {
                target = dir;
            }
            if (inotify_dir_count < MAX_INOTIFY_DIRS) {
                inotify_dirs[inotify_dir_count++] = dir;
            }
        } else if (strcmp(argv[i], "--sysfs") == 0 && i + 1 < argc) {
            mode = MODE_SYSFS;
            target = argv[++i];
        } else if (strcmp(argv[i], "--evdev-watch") == 0 && i + 1 < argc) {
            mode = MODE_EVDEV;
            target = argv[++i];
        } else if (strcmp(argv[i], "--match") == 0 && i + 1 < argc) {
            match_csv = argv[++i];
        } else if (strcmp(argv[i], "--check-exists") == 0 && i + 1 < argc) {
            check_exists = argv[++i];
        } else if (strcmp(argv[i], "--check-file-diff") == 0 && i + 1 < argc) {
            check_diff = argv[++i];
        } else if (strcmp(argv[i], "--dev-dir") == 0 && i + 1 < argc) {
            dev_dir = argv[++i];
        } else if (strcmp(argv[i], "--stream") == 0) {
            stream = 1;
        } else if (strcmp(argv[i], "--oneshot") == 0) {
            stream = 0;
        } else if (strcmp(argv[i], "--debounce-ms") == 0 && i + 1 < argc) {
            debounce_ms = atoi(argv[++i]);
            if (debounce_ms < 0) debounce_ms = 0;
            if (debounce_ms > 5000) debounce_ms = 5000;
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            timeout_ms = atoi(argv[++i]);
            if (timeout_ms < 0) timeout_ms = -1;
        } else {
            fprintf(stderr, "Usage: spx-event-wait (--uevent SUBSYS|--inotify DIR|--sysfs PATH|--evdev-watch NAMES) [--stream|--oneshot] [--match CSV] [--check-exists CSV] [--check-file-diff PATH:VAL] [--debounce-ms MS] [--timeout-ms MS]\n");
            return 2;
        }
    }

    if (mode == MODE_NONE || !target) {
        return 2;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    switch (mode) {
        case MODE_UEVENT:
            return run_uevent(target, stream, debounce_ms, timeout_ms, check_exists, check_diff);
        case MODE_INOTIFY:
            return run_inotify(inotify_dirs, inotify_dir_count, match_csv, stream, debounce_ms, timeout_ms, check_exists, check_diff);
        case MODE_SYSFS:
            return run_sysfs(target, stream, debounce_ms, timeout_ms, check_diff);
        case MODE_EVDEV:
            return run_evdev_watch(target, dev_dir, stream, debounce_ms, timeout_ms, check_diff);
        default:
            return 2;
    }
}
