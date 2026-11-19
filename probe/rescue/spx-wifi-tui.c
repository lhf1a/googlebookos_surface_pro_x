/*
 * Surface Pro X Rescue Mode - Wi-Fi Selection & Login TUI / CLI
 *
 * Self-contained, zero-external-dependency static C TUI communicating directly
 * with wpa_supplicant via wpa_cli and acquiring IPv4 leases via BusyBox udhcpc.
 * Uses pure VT100/ANSI escape sequences + ASCII box art so it renders cleanly
 * on both the Linux framebuffer console (tty0) and SSH terminals.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define MAX_APS 128
#define MAX_SSID_RAW 128
#define MAX_SSID_DISP 128
#define MAX_PASS_LEN 128
#define MAX_IFACES 16
#define CMD_BUF_SIZE 65536

typedef enum {
    SEC_OPEN = 0,
    SEC_WEP,
    SEC_WPA,
    SEC_WPA2,
    SEC_WPA2_WPA3,
    SEC_WPA3,
    SEC_EAP
} sec_mode_t;

typedef struct {
    char bssid[32];
    int freq;
    int rssi;
    sec_mode_t sec;
    char flags[192];
    unsigned char ssid_raw[MAX_SSID_RAW];
    size_t ssid_len;
    char ssid_disp[MAX_SSID_DISP];
    bool connected;
} ap_entry_t;

typedef struct {
    char name[32];
    char ip[64];
} net_info_t;

typedef struct {
    const char *wpa_cli;
    const char *wpa_ctrl;
    const char *udhcpc;
    const char *dhcp_hook;
    const char *ip_bin;
    const char *run_dir;
    const char *sys_net;
    const char *esp_dir;
    const char *start_android;
    char wifi_iface[32];
} cfg_t;

static volatile sig_atomic_t g_resized = 0;
static volatile sig_atomic_t g_quit = 0;
static struct termios g_orig_termios;
static bool g_raw_active = false;

static void handle_sigwinch(int sig) {
    (void)sig;
    g_resized = 1;
}

static void handle_sigterm(int sig) {
    (void)sig;
    g_quit = 1;
}

static const char *env_or(const char *name, const char *def) {
    const char *v = getenv(name);
    return (v && v[0]) ? v : def;
}

static void leave_tui_mode(void) {
    if (g_raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_active = false;
        const char *seq = "\033[?25h\033[0m\033[?1049l";
        if (write(STDOUT_FILENO, seq, strlen(seq)) < 0) {
            /* ignore on teardown */
        }
    }
}

static bool enter_tui_mode(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return false;
    }
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0) {
        return false;
    }
    struct termios raw = g_orig_termios;
    raw.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag |= OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return false;
    }
    g_raw_active = true;
    atexit(leave_tui_mode);
    const char *seq = "\033[?1049h\033[?25l\033[2J\033[H";
    if (write(STDOUT_FILENO, seq, strlen(seq)) < 0) {
        return false;
    }
    return true;
}

static void get_term_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row >= 12 && ws.ws_col >= 40) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

static int run_cmd(char *const argv[], char *out, size_t out_cap) {
    int pipefd[2];
    if (out && out_cap > 0) {
        out[0] = '\0';
    }
    if (pipe(pipefd) != 0) {
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > 2) close(devnull);
        }
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    size_t total = 0;
    char tmp[1024];
    for (;;) {
        ssize_t n = read(pipefd[0], tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        if (out && out_cap > 1 && total < out_cap - 1) {
            size_t avail = (out_cap - 1) - total;
            size_t copy_n = (size_t)n < avail ? (size_t)n : avail;
            memcpy(out + total, tmp, copy_n);
            total += copy_n;
            out[total] = '\0';
        }
    }
    close(pipefd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static int run_wpa(const cfg_t *cfg, char *out, size_t out_cap,
                   const char *a1, const char *a2, const char *a3, const char *a4) {
    char *argv[12];
    int n = 0;
    argv[n++] = (char *)cfg->wpa_cli;
    argv[n++] = "-p";
    argv[n++] = (char *)cfg->wpa_ctrl;
    argv[n++] = "-i";
    argv[n++] = (char *)cfg->wifi_iface;
    if (a1) argv[n++] = (char *)a1;
    if (a2) argv[n++] = (char *)a2;
    if (a3) argv[n++] = (char *)a3;
    if (a4) argv[n++] = (char *)a4;
    argv[n] = NULL;
    return run_cmd(argv, out, out_cap);
}

static void detect_wifi_iface(cfg_t *cfg) {
    const char *override = getenv("SPX_WIFI_IFACE");
    if (override && override[0]) {
        snprintf(cfg->wifi_iface, sizeof(cfg->wifi_iface), "%s", override);
        return;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/sq2-rescue-wifi.iface", cfg->run_dir);
    FILE *f = fopen(path, "r");
    if (f) {
        if (fgets(cfg->wifi_iface, sizeof(cfg->wifi_iface), f)) {
            cfg->wifi_iface[strcspn(cfg->wifi_iface, "\r\n")] = '\0';
        }
        fclose(f);
        if (cfg->wifi_iface[0]) {
            snprintf(path, sizeof(path), "%s/%s", cfg->sys_net, cfg->wifi_iface);
            if (access(path, F_OK) == 0) return;
            cfg->wifi_iface[0] = '\0';
        }
    }
    DIR *dir = opendir(cfg->sys_net);
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (de->d_name[0] == '.') continue;
            snprintf(path, sizeof(path), "%s/%s/phy80211", cfg->sys_net, de->d_name);
            if (access(path, F_OK) == 0) {
                snprintf(cfg->wifi_iface, sizeof(cfg->wifi_iface), "%.31s", de->d_name);
                break;
            }
        }
        closedir(dir);
    }
    if (!cfg->wifi_iface[0]) {
        snprintf(cfg->wifi_iface, sizeof(cfg->wifi_iface), "wlan0");
    }
}

static void get_iface_ipv4(const cfg_t *cfg, const char *iface, char *ip_out, size_t ip_cap) {
    ip_out[0] = '\0';
    if (!iface || !iface[0]) return;
    char buf[1024];
    char *argv[] = {
        (char *)cfg->ip_bin, "-4", "-o", "addr", "show",
        "dev", (char *)iface, "scope", "global", NULL
    };
    if (run_cmd(argv, buf, sizeof(buf)) != 0) return;
    char *p = strstr(buf, " inet ");
    if (!p) return;
    p += 6;
    while (*p == ' ') p++;
    size_t i = 0;
    while (*p && !isspace((unsigned char)*p) && *p != '/' && i + 1 < ip_cap) {
        ip_out[i++] = *p++;
    }
    ip_out[i] = '\0';
}

static bool is_usb_eth_iface(const cfg_t *cfg, const char *name) {
    if (!name || !name[0] || strcmp(name, "lo") == 0) return false;
    if (strcmp(name, cfg->wifi_iface) == 0) return false;
    if (strncmp(name, "wlan", 4) == 0 || strncmp(name, "p2p", 3) == 0 ||
        strncmp(name, "rmnet", 5) == 0 || strncmp(name, "dummy", 5) == 0 ||
        strncmp(name, "tun", 3) == 0 || strncmp(name, "tap", 3) == 0 ||
        strncmp(name, "sit", 3) == 0 || strncmp(name, "ip6tnl", 6) == 0) {
        return false;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/phy80211", cfg->sys_net, name);
    if (access(path, F_OK) == 0) return false;
    snprintf(path, sizeof(path), "%s/%s/device", cfg->sys_net, name);
    char resolved[PATH_MAX];
    if (realpath(path, resolved) && strstr(resolved, "/usb") != NULL) {
        return true;
    }
    return strncmp(name, "eth", 3) == 0 || strncmp(name, "enx", 3) == 0 ||
           strncmp(name, "enu", 3) == 0 || strncmp(name, "usb", 3) == 0;
}

static int collect_usb_ifaces(const cfg_t *cfg, net_info_t *list, int max_items) {
    int count = 0;
    DIR *dir = opendir(cfg->sys_net);
    if (!dir) return 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && count < max_items) {
        if (de->d_name[0] == '.') continue;
        if (!is_usb_eth_iface(cfg, de->d_name)) continue;
        snprintf(list[count].name, sizeof(list[count].name), "%.31s", de->d_name);
        get_iface_ipv4(cfg, list[count].name, list[count].ip, sizeof(list[count].ip));
        count++;
    }
    closedir(dir);
    return count;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static size_t decode_wpa_ssid(const char *src, unsigned char *raw, size_t raw_cap,
                              char *disp, size_t disp_cap) {
    size_t rlen = 0;
    size_t dlen = 0;
    for (size_t i = 0; src[i] != '\0' && rlen + 1 < raw_cap; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '\\' && src[i + 1] != '\0') {
            char next = src[i + 1];
            if (next == 'x' && src[i + 2] && src[i + 3]) {
                int hi = hex_val(src[i + 2]);
                int lo = hex_val(src[i + 3]);
                if (hi >= 0 && lo >= 0) {
                    ch = (unsigned char)((hi << 4) | lo);
                    i += 3;
                }
            } else if (next == '\\' || next == '"') {
                ch = (unsigned char)next;
                i++;
            } else if (next == 't') {
                ch = '\t';
                i++;
            } else if (next == 'n') {
                ch = '\n';
                i++;
            } else if (next == 'r') {
                ch = '\r';
                i++;
            }
        }
        raw[rlen++] = ch;
        if (dlen + 1 < disp_cap) {
            /* Strip ASCII control chars so rogue SSIDs cannot inject ANSI escapes. */
            if (ch < 0x20 || ch == 0x7f) {
                disp[dlen++] = '?';
            } else {
                disp[dlen++] = (char)ch;
            }
        }
    }
    raw[rlen] = '\0';
    disp[dlen] = '\0';
    return rlen;
}

static void encode_hex(const unsigned char *raw, size_t len, char *hex_out, size_t cap) {
    static const char hx[] = "0123456789abcdef";
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2 < cap; i++) {
        hex_out[pos++] = hx[(raw[i] >> 4) & 0xf];
        hex_out[pos++] = hx[raw[i] & 0xf];
    }
    hex_out[pos] = '\0';
}

static sec_mode_t parse_security(const char *flags) {
    bool has_sae = strstr(flags, "SAE") != NULL;
    bool has_wpa2 = strstr(flags, "WPA2-PSK") != NULL;
    bool has_wpa = strstr(flags, "WPA-PSK") != NULL;
    bool has_eap = strstr(flags, "-EAP") != NULL;
    bool has_wep = strstr(flags, "[WEP]") != NULL;
    if (has_sae && (has_wpa2 || has_wpa)) return SEC_WPA2_WPA3;
    if (has_sae) return SEC_WPA3;
    if (has_wpa2) return SEC_WPA2;
    if (has_wpa) return SEC_WPA;
    if (has_eap) return SEC_EAP;
    if (has_wep) return SEC_WEP;
    return SEC_OPEN;
}

static const char *sec_label(sec_mode_t sec) {
    switch (sec) {
        case SEC_OPEN: return "OPEN";
        case SEC_WEP: return "WEP";
        case SEC_WPA: return "WPA";
        case SEC_WPA2: return "WPA2";
        case SEC_WPA2_WPA3: return "WPA2/3";
        case SEC_WPA3: return "WPA3";
        case SEC_EAP: return "EAP";
    }
    return "OPEN";
}

static const char *signal_bars(int rssi) {
    if (rssi >= -55) return "[####]";
    if (rssi >= -67) return "[### ]";
    if (rssi >= -78) return "[##  ]";
    if (rssi >= -88) return "[#   ]";
    return "[    ]";
}

static const char *freq_band(int freq) {
    if (freq >= 5900) return "6G ";
    if (freq >= 4900) return "5G ";
    if (freq >= 2400) return "2.4";
    return " ? ";
}

static void get_wifi_status(const cfg_t *cfg, char *state_out, size_t state_cap,
                            char *ssid_disp, size_t ssid_cap,
                            unsigned char *ssid_raw, size_t *ssid_raw_len) {
    state_out[0] = '\0';
    ssid_disp[0] = '\0';
    if (ssid_raw && ssid_raw_len) {
        ssid_raw[0] = '\0';
        *ssid_raw_len = 0;
    }
    char buf[4096];
    if (run_wpa(cfg, buf, sizeof(buf), "status", NULL, NULL, NULL) != 0) {
        snprintf(state_out, state_cap, "UNAVAILABLE");
        return;
    }
    char *save = NULL;
    for (char *line = strtok_r(buf, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        if (strncmp(line, "wpa_state=", 10) == 0) {
            snprintf(state_out, state_cap, "%s", line + 10);
        } else if (strncmp(line, "ssid=", 5) == 0) {
            unsigned char tmp_raw[MAX_SSID_RAW];
            size_t len = decode_wpa_ssid(line + 5, tmp_raw, sizeof(tmp_raw), ssid_disp, ssid_cap);
            if (ssid_raw && ssid_raw_len) {
                memcpy(ssid_raw, tmp_raw, len + 1);
                *ssid_raw_len = len;
            }
        }
    }
    if (!state_out[0]) snprintf(state_out, state_cap, "DISCONNECTED");
}

static int compare_aps(const void *a, const void *b) {
    const ap_entry_t *pa = (const ap_entry_t *)a;
    const ap_entry_t *pb = (const ap_entry_t *)b;
    if (pa->connected != pb->connected) return pb->connected - pa->connected;
    if (pa->rssi != pb->rssi) return pb->rssi - pa->rssi;
    return strcmp(pa->ssid_disp, pb->ssid_disp);
}

static int scan_aps(const cfg_t *cfg, bool trigger_scan, ap_entry_t *aps, int max_aps) {
    char buf[CMD_BUF_SIZE];
    if (trigger_scan) {
        run_wpa(cfg, buf, sizeof(buf), "scan", NULL, NULL, NULL);
        sleep(2);
    }
    if (run_wpa(cfg, buf, sizeof(buf), "scan_results", NULL, NULL, NULL) != 0) {
        return -1;
    }
    char cur_state[64];
    char cur_ssid_disp[MAX_SSID_DISP];
    unsigned char cur_ssid_raw[MAX_SSID_RAW];
    size_t cur_ssid_len = 0;
    get_wifi_status(cfg, cur_state, sizeof(cur_state), cur_ssid_disp, sizeof(cur_ssid_disp),
                    cur_ssid_raw, &cur_ssid_len);
    bool is_connected = strcmp(cur_state, "COMPLETED") == 0 && cur_ssid_len > 0;

    int count = 0;
    char *save = NULL;
    bool first = true;
    for (char *line = strtok_r(buf, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        if (first) {
            first = false;
            if (strncmp(line, "bssid", 5) == 0) continue;
        }
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        *t1 = '\0';
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;
        *t2 = '\0';
        char *t3 = strchr(t2 + 1, '\t');
        if (!t3) continue;
        *t3 = '\0';
        char *t4 = strchr(t3 + 1, '\t');
        if (!t4) continue;
        *t4 = '\0';

        const char *bssid = line;
        int freq = atoi(t1 + 1);
        int rssi = atoi(t2 + 1);
        const char *flags = t3 + 1;
        const char *ssid_enc = t4 + 1;

        unsigned char raw[MAX_SSID_RAW];
        char disp[MAX_SSID_DISP];
        size_t slen = decode_wpa_ssid(ssid_enc, raw, sizeof(raw), disp, sizeof(disp));
        if (slen == 0) continue; /* Skip hidden/empty SSIDs in scan list */

        /* Deduplicate by exact raw SSID, keeping the strongest signal. */
        int existing = -1;
        for (int i = 0; i < count; i++) {
            if (aps[i].ssid_len == slen && memcmp(aps[i].ssid_raw, raw, slen) == 0) {
                existing = i;
                break;
            }
        }
        sec_mode_t sec = parse_security(flags);
        if (existing >= 0) {
            if (rssi > aps[existing].rssi) {
                snprintf(aps[existing].bssid, sizeof(aps[existing].bssid), "%s", bssid);
                aps[existing].freq = freq;
                aps[existing].rssi = rssi;
                aps[existing].sec = sec;
                snprintf(aps[existing].flags, sizeof(aps[existing].flags), "%s", flags);
            }
            continue;
        }
        if (count >= max_aps) continue;
        ap_entry_t *e = &aps[count++];
        memset(e, 0, sizeof(*e));
        snprintf(e->bssid, sizeof(e->bssid), "%s", bssid);
        e->freq = freq;
        e->rssi = rssi;
        e->sec = sec;
        snprintf(e->flags, sizeof(e->flags), "%s", flags);
        memcpy(e->ssid_raw, raw, slen + 1);
        e->ssid_len = slen;
        snprintf(e->ssid_disp, sizeof(e->ssid_disp), "%s", disp);
        e->connected = is_connected && slen == cur_ssid_len &&
                       memcmp(raw, cur_ssid_raw, slen) == 0;
    }
    if (count > 1) {
        qsort(aps, (size_t)count, sizeof(ap_entry_t), compare_aps);
    }
    return count;
}

static void stop_wifi_dhcp(const cfg_t *cfg) {
    char pidpath[PATH_MAX];
    snprintf(pidpath, sizeof(pidpath), "%s/sq2-rescue-udhcpc-%s.pid", cfg->run_dir, cfg->wifi_iface);
    FILE *f = fopen(pidpath, "r");
    if (f) {
        long pid = 0;
        if (fscanf(f, "%ld", &pid) == 1 && pid > 1) {
            kill((pid_t)pid, SIGTERM);
            usleep(200000);
            kill((pid_t)pid, SIGKILL);
        }
        fclose(f);
        unlink(pidpath);
    }
}

static void remove_all_networks(const cfg_t *cfg) {
    char buf[4096];
    if (run_wpa(cfg, buf, sizeof(buf), "list_networks", NULL, NULL, NULL) != 0) return;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        if (!isdigit((unsigned char)line[0])) continue;
        char id[16];
        size_t i = 0;
        while (isdigit((unsigned char)line[i]) && i + 1 < sizeof(id)) {
            id[i] = line[i];
            i++;
        }
        id[i] = '\0';
        if (id[0]) {
            char tmp[128];
            run_wpa(cfg, tmp, sizeof(tmp), "remove_network", id, NULL, NULL);
        }
    }
}

static bool quote_wpa_string(const char *src, char *dst, size_t cap) {
    size_t pos = 0;
    if (cap < 3) return false;
    dst[pos++] = '"';
    for (size_t i = 0; src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (pos + 2 >= cap - 1) return false;
            dst[pos++] = '\\';
            dst[pos++] = (char)c;
        } else {
            if (pos + 1 >= cap - 1) return false;
            dst[pos++] = (char)c;
        }
    }
    dst[pos++] = '"';
    dst[pos] = '\0';
    return true;
}

static bool is_hex64(const char *s) {
    size_t n = strlen(s);
    if (n != 64) return false;
    for (size_t i = 0; i < n; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

typedef void (*status_cb_t)(const char *msg, void *ctx);

static bool connect_wifi(const cfg_t *cfg, const unsigned char *ssid_raw, size_t ssid_len,
                         sec_mode_t sec, const char *password, bool hidden,
                         char *err_out, size_t err_cap, status_cb_t cb, void *cb_ctx) {
    char out[2048];
    if (cb) cb("Preparing Wi-Fi connection...", cb_ctx);

    if (run_wpa(cfg, out, sizeof(out), "ping", NULL, NULL, NULL) != 0 ||
        strstr(out, "PONG") == NULL) {
        snprintf(err_out, err_cap, "wpa_supplicant is not ready on %s", cfg->wifi_iface);
        return false;
    }

    stop_wifi_dhcp(cfg);
    remove_all_networks(cfg);

    if (run_wpa(cfg, out, sizeof(out), "add_network", NULL, NULL, NULL) != 0) {
        snprintf(err_out, err_cap, "wpa_cli add_network failed");
        return false;
    }
    char net_id[16];
    char *p = out;
    while (*p && !isdigit((unsigned char)*p)) p++;
    size_t ni = 0;
    while (isdigit((unsigned char)p[ni]) && ni + 1 < sizeof(net_id)) {
        net_id[ni] = p[ni];
        ni++;
    }
    net_id[ni] = '\0';
    if (!net_id[0]) {
        snprintf(err_out, err_cap, "Invalid network ID from wpa_cli: %.120s", out);
        return false;
    }

    char ssid_hex[MAX_SSID_RAW * 2 + 1];
    encode_hex(ssid_raw, ssid_len, ssid_hex, sizeof(ssid_hex));
    if (run_wpa(cfg, out, sizeof(out), "set_network", net_id, "ssid", ssid_hex) != 0 ||
        strstr(out, "OK") == NULL) {
        snprintf(err_out, err_cap, "Failed to set SSID");
        return false;
    }

    if (hidden) {
        run_wpa(cfg, out, sizeof(out), "set_network", net_id, "scan_ssid", "1");
    }

    bool need_pass = (sec != SEC_OPEN) && (password && password[0] != '\0');
    if (sec == SEC_OPEN || !need_pass) {
        if (sec != SEC_OPEN && (!password || !password[0])) {
            snprintf(err_out, err_cap, "Password is required for %s network", sec_label(sec));
            return false;
        }
        if (run_wpa(cfg, out, sizeof(out), "set_network", net_id, "key_mgmt", "NONE") != 0 ||
            strstr(out, "OK") == NULL) {
            snprintf(err_out, err_cap, "Failed to configure open network");
            return false;
        }
    } else if (sec == SEC_WEP) {
        char qpass[MAX_PASS_LEN * 2 + 4];
        quote_wpa_string(password, qpass, sizeof(qpass));
        run_wpa(cfg, out, sizeof(out), "set_network", net_id, "key_mgmt", "NONE");
        if (run_wpa(cfg, out, sizeof(out), "set_network", net_id, "wep_key0", qpass) != 0 ||
            strstr(out, "OK") == NULL) {
            snprintf(err_out, err_cap, "Failed to set WEP key");
            return false;
        }
        run_wpa(cfg, out, sizeof(out), "set_network", net_id, "wep_tx_keyidx", "0");
    } else {
        if (!is_hex64(password) && (strlen(password) < 8 || strlen(password) > 63)) {
            snprintf(err_out, err_cap, "WPA passphrase must be 8-63 characters (or 64 hex)");
            return false;
        }
        char qpass[MAX_PASS_LEN * 2 + 4];
        const char *psk_arg = password;
        if (!is_hex64(password)) {
            if (!quote_wpa_string(password, qpass, sizeof(qpass))) {
                snprintf(err_out, err_cap, "Passphrase too long");
                return false;
            }
            psk_arg = qpass;
        }
        if (sec == SEC_WPA3) {
            run_wpa(cfg, out, sizeof(out), "set_network", net_id, "key_mgmt", "SAE");
            run_wpa(cfg, out, sizeof(out), "set_network", net_id, "ieee80211w", "2");
        } else if (sec == SEC_WPA2_WPA3) {
            run_wpa(cfg, out, sizeof(out), "set_network", net_id, "key_mgmt", "WPA-PSK SAE");
            run_wpa(cfg, out, sizeof(out), "set_network", net_id, "ieee80211w", "1");
        } else {
            run_wpa(cfg, out, sizeof(out), "set_network", net_id, "key_mgmt", "WPA-PSK");
        }
        if (run_wpa(cfg, out, sizeof(out), "set_network", net_id, "psk", psk_arg) != 0 ||
            strstr(out, "OK") == NULL) {
            snprintf(err_out, err_cap, "Failed to set Wi-Fi passphrase");
            return false;
        }
    }

    run_wpa(cfg, out, sizeof(out), "enable_network", net_id, NULL, NULL);
    run_wpa(cfg, out, sizeof(out), "select_network", net_id, NULL, NULL);

    if (cb) cb("Associating with Wi-Fi access point...", cb_ctx);
    bool associated = false;
    bool saw_handshake = false;
    int disc_after_hs = 0;
    for (int i = 0; i < 40; i++) {
        char st[64], sdisp[MAX_SSID_DISP];
        get_wifi_status(cfg, st, sizeof(st), sdisp, sizeof(sdisp), NULL, NULL);
        if (strcmp(st, "COMPLETED") == 0) {
            associated = true;
            break;
        }
        if (strcmp(st, "4WAY_HANDSHAKE") == 0 || strcmp(st, "GROUP_HANDSHAKE") == 0) {
            saw_handshake = true;
            if (cb) cb("Authenticating (4-way handshake)...", cb_ctx);
        } else if (saw_handshake && strcmp(st, "DISCONNECTED") == 0) {
            disc_after_hs++;
            if (disc_after_hs >= 3) {
                snprintf(err_out, err_cap, "Authentication failed (check Wi-Fi password)");
                return false;
            }
        }
        usleep(500000);
    }
    if (!associated) {
        snprintf(err_out, err_cap, "Wi-Fi association timed out");
        return false;
    }

    if (cb) cb("Connected! Obtaining IPv4 address via DHCP...", cb_ctx);
    char pidpath[PATH_MAX];
    snprintf(pidpath, sizeof(pidpath), "%s/sq2-rescue-udhcpc-%s.pid", cfg->run_dir, cfg->wifi_iface);
    char *dhcp_argv[] = {
        (char *)cfg->udhcpc, "-i", (char *)cfg->wifi_iface,
        "-s", (char *)cfg->dhcp_hook, "-p", pidpath,
        "-b", "-t", "8", "-T", "2", NULL
    };
    run_cmd(dhcp_argv, out, sizeof(out));

    char ip[64] = "";
    for (int i = 0; i < 20; i++) {
        get_iface_ipv4(cfg, cfg->wifi_iface, ip, sizeof(ip));
        if (ip[0]) break;
        usleep(500000);
    }
    if (ip[0]) {
        snprintf(err_out, err_cap, "Connected! IP=%s (SSH: ssh root@%s, pass: 000000)", ip, ip);
    } else {
        snprintf(err_out, err_cap, "Associated, waiting for DHCP lease...");
    }
    return true;
}

static bool disconnect_wifi(const cfg_t *cfg, char *msg_out, size_t msg_cap) {
    char out[512];
    stop_wifi_dhcp(cfg);
    run_wpa(cfg, out, sizeof(out), "disconnect", NULL, NULL, NULL);
    remove_all_networks(cfg);
    char *flush_argv[] = { (char *)cfg->ip_bin, "addr", "flush", "dev", (char *)cfg->wifi_iface, NULL };
    run_cmd(flush_argv, out, sizeof(out));
    snprintf(msg_out, msg_cap, "Disconnected Wi-Fi (%s)", cfg->wifi_iface);
    return true;
}

static bool save_wifi_to_esp(const cfg_t *cfg, char *msg_out, size_t msg_cap) {
    char out[512];
    run_wpa(cfg, out, sizeof(out), "save_config", NULL, NULL, NULL);
    char src[PATH_MAX], dst_dir[PATH_MAX], dst[PATH_MAX];
    snprintf(src, sizeof(src), "%s/sq2-rescue-wpa.conf", cfg->run_dir);
    snprintf(dst_dir, sizeof(dst_dir), "%s/rescue", cfg->esp_dir);
    snprintf(dst, sizeof(dst), "%s/rescue/wpa_supplicant.conf", cfg->esp_dir);
    mkdir(dst_dir, 0755);
    FILE *in = fopen(src, "rb");
    if (!in) {
        snprintf(msg_out, msg_cap, "No active rescue Wi-Fi config to save");
        return false;
    }
    FILE *fp = fopen(dst, "wb");
    if (!fp) {
        fclose(in);
        snprintf(msg_out, msg_cap, "Cannot write %.160s (%.60s)", dst, strerror(errno));
        return false;
    }
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, fp);
    }
    fclose(in);
    fclose(fp);
    snprintf(msg_out, msg_cap, "Saved rescue Wi-Fi profile to %.180s", dst);
    return true;
}

/* ---------------- TUI Rendering & Input ---------------- */

static void print_clipped(const char *s, int max_w) {
    if (max_w <= 0) return;
    int len = (int)strlen(s);
    if (len <= max_w) {
        printf("%s", s);
        for (int i = len; i < max_w; i++) putchar(' ');
    } else if (max_w >= 4) {
        printf("%.*s...", max_w - 3, s);
    } else {
        printf("%.*s", max_w, s);
    }
}

static void draw_hline(int width, char left, char fill, char right) {
    putchar(left);
    for (int i = 0; i < width - 2; i++) putchar(fill);
    putchar(right);
}

typedef struct {
    const cfg_t *cfg;
    ap_entry_t *aps;
    int ap_count;
    int selected;
    int scroll;
    char status_msg[256];
} tui_state_t;

static void draw_ui(tui_state_t *st) {
    int rows, cols;
    get_term_size(&rows, &cols);
    if (cols > 120) cols = 120;
    if (cols < 52) cols = 52;

    char wstate[64], wssid[MAX_SSID_DISP], wip[64];
    get_wifi_status(st->cfg, wstate, sizeof(wstate), wssid, sizeof(wssid), NULL, NULL);
    get_iface_ipv4(st->cfg, st->cfg->wifi_iface, wip, sizeof(wip));

    net_info_t usb_nics[MAX_IFACES];
    int usb_cnt = collect_usb_ifaces(st->cfg, usb_nics, MAX_IFACES);

    char usb_summary[160] = "none";
    if (usb_cnt > 0) {
        usb_summary[0] = '\0';
        for (int i = 0; i < usb_cnt; i++) {
            char part[128];
            snprintf(part, sizeof(part), "%s%.31s:%.63s",
                     i > 0 ? ", " : "",
                     usb_nics[i].name,
                     usb_nics[i].ip[0] ? usb_nics[i].ip : "no-ip");
            strncat(usb_summary, part, sizeof(usb_summary) - strlen(usb_summary) - 1);
        }
    }

    const char *ssh_ip = wip[0] ? wip : NULL;
    if (!ssh_ip) {
        for (int i = 0; i < usb_cnt; i++) {
            if (usb_nics[i].ip[0]) {
                ssh_ip = usb_nics[i].ip;
                break;
            }
        }
    }

    printf("\033[H\033[2J");
    /* Top header box */
    printf("\033[1;36m");
    draw_hline(cols, '+', '-', '+');
    printf("\r\n|");
    printf("\033[1;37m");
    print_clipped(" Surface Pro X Rescue Mode - Wi-Fi & Network TUI", cols - 2);
    printf("\033[1;36m|\r\n");
    draw_hline(cols, '+', '-', '+');
    printf("\033[0m\r\n");

    char line[512];
    if (wssid[0]) {
        snprintf(line, sizeof(line), " Wi-Fi (%.31s): %.32s [%.127s]  IP: %.63s",
                 st->cfg->wifi_iface, wstate, wssid, wip[0] ? wip : "none");
    } else {
        snprintf(line, sizeof(line), " Wi-Fi (%.31s): %.32s  IP: %.63s",
                 st->cfg->wifi_iface, wstate, wip[0] ? wip : "none");
    }
    printf("|");
    print_clipped(line, cols - 2);
    printf("|\r\n");

    snprintf(line, sizeof(line), " USB NIC: %s", usb_summary);
    printf("|");
    print_clipped(line, cols - 2);
    printf("|\r\n");

    if (ssh_ip) {
        snprintf(line, sizeof(line), " SSH: ssh root@%s  (default pass: 000000 | pubkey enabled)", ssh_ip);
    } else {
        snprintf(line, sizeof(line), " SSH: port 22 listening (default pass: 000000 | connect Wi-Fi/USB)");
    }
    printf("|\033[32m");
    print_clipped(line, cols - 2);
    printf("\033[0m|\r\n");

    draw_hline(cols, '+', '-', '+');
    printf("\r\n");

    /* Table header */
    printf("|\033[1m");
    snprintf(line, sizeof(line), " %-2s %-6s %4s %-4s %-6s %s", "ST", "SIGNAL", "dBm", "BAND", "SEC", "SSID");
    print_clipped(line, cols - 2);
    printf("\033[0m|\r\n");

    int list_height = rows - 12;
    if (list_height < 4) list_height = 4;

    if (st->selected < 0) st->selected = 0;
    if (st->ap_count > 0 && st->selected >= st->ap_count) st->selected = st->ap_count - 1;
    if (st->selected < st->scroll) st->scroll = st->selected;
    if (st->selected >= st->scroll + list_height) st->scroll = st->selected - list_height + 1;
    if (st->scroll < 0) st->scroll = 0;

    for (int i = 0; i < list_height; i++) {
        int idx = st->scroll + i;
        printf("|");
        if (st->ap_count == 0 && i == 0) {
            print_clipped(" (No Wi-Fi networks found. Press 'r' to rescan or 'h' for hidden SSID.)", cols - 2);
        } else if (idx < st->ap_count) {
            const ap_entry_t *ap = &st->aps[idx];
            snprintf(line, sizeof(line), " %-2s %-6s %4d %-4s %-6s %s",
                     ap->connected ? "*" : " ",
                     signal_bars(ap->rssi),
                     ap->rssi,
                     freq_band(ap->freq),
                     sec_label(ap->sec),
                     ap->ssid_disp);
            if (idx == st->selected) {
                printf("\033[7m");
                print_clipped(line, cols - 2);
                printf("\033[0m");
            } else if (ap->connected) {
                printf("\033[1;32m");
                print_clipped(line, cols - 2);
                printf("\033[0m");
            } else {
                print_clipped(line, cols - 2);
            }
        } else {
            print_clipped("", cols - 2);
        }
        printf("|\r\n");
    }

    draw_hline(cols, '+', '-', '+');
    printf("\r\n|\033[33m");
    snprintf(line, sizeof(line), " %.255s", st->status_msg[0] ? st->status_msg : "Ready.");
    print_clipped(line, cols - 2);
    printf("\033[0m|\r\n");

    draw_hline(cols, '+', '-', '+');
    printf("\r\n");
    print_clipped(" [Up/Dn] Select  [Enter] Connect  [r] Scan  [h] Hidden  [d] Disc  [s] Save  [b] Android  [q] Shell", cols);
    fflush(stdout);
}

static void tui_status_cb(const char *msg, void *ctx) {
    tui_state_t *st = (tui_state_t *)ctx;
    snprintf(st->status_msg, sizeof(st->status_msg), "%s", msg);
    draw_ui(st);
}

enum {
    KEY_NONE = 0,
    KEY_UP = 1000,
    KEY_DOWN,
    KEY_PGUP,
    KEY_PGDN,
    KEY_ENTER,
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_TAB
};

static int read_key(int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0) return KEY_NONE;

    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return KEY_NONE;
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == 127 || c == 8) return KEY_BACKSPACE;
    if (c == '\t') return KEY_TAB;
    if (c == 27) {
        struct pollfd epfd = { .fd = STDIN_FILENO, .events = POLLIN };
        if (poll(&epfd, 1, 35) <= 0) return KEY_ESC;
        unsigned char seq[4] = {0};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_ESC;
        if (seq[0] == '[' || seq[0] == 'O') {
            if (poll(&epfd, 1, 35) <= 0) return KEY_ESC;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESC;
            if (seq[1] == 'A') return KEY_UP;
            if (seq[1] == 'B') return KEY_DOWN;
            if (seq[1] == '5') {
                if (poll(&epfd, 1, 20) > 0 && read(STDIN_FILENO, &seq[2], 1) < 0) {
                    /* ignore trailing tilde */
                }
                return KEY_PGUP;
            }
            if (seq[1] == '6') {
                if (poll(&epfd, 1, 20) > 0 && read(STDIN_FILENO, &seq[2], 1) < 0) {
                    /* ignore trailing tilde */
                }
                return KEY_PGDN;
            }
        }
        return KEY_ESC;
    }
    return (int)c;
}

static bool prompt_input(const char *title, const char *prompt, bool secret,
                         char *buf, size_t cap) {
    int rows, cols;
    get_term_size(&rows, &cols);
    int box_w = cols - 8;
    if (box_w > 68) box_w = 68;
    if (box_w < 38) box_w = 38;
    int start_col = (cols - box_w) / 2;
    if (start_col < 1) start_col = 1;
    int start_row = rows / 2 - 3;
    if (start_row < 2) start_row = 2;

    size_t len = 0;
    buf[0] = '\0';
    bool masked = secret;

    for (;;) {
        printf("\033[%d;%dH\033[1;36m", start_row, start_col);
        draw_hline(box_w, '+', '-', '+');
        printf("\033[%d;%dH|\033[1;37m", start_row + 1, start_col);
        print_clipped(title, box_w - 2);
        printf("\033[1;36m|\033[%d;%dH|\033[0m", start_row + 2, start_col);
        print_clipped(prompt, box_w - 2);
        printf("\033[1;36m|\033[%d;%dH|\033[0m ", start_row + 3, start_col);

        int field_w = box_w - 4;
        char shown[256];
        size_t offset = 0;
        if ((int)len > field_w - 1) offset = len - (size_t)(field_w - 1);
        size_t si = 0;
        for (size_t i = offset; i < len && si + 1 < sizeof(shown); i++) {
            shown[si++] = masked ? '*' : buf[i];
        }
        shown[si] = '\0';
        printf("\033[7m");
        print_clipped(shown, field_w);
        printf("\033[0m \033[1;36m|\033[%d;%dH|\033[0m", start_row + 4, start_col);
        print_clipped(secret ? " [Enter] OK  [Tab] Show/Hide  [Esc] Cancel" : " [Enter] OK  [Esc] Cancel", box_w - 2);
        printf("\033[1;36m|\033[%d;%dH", start_row + 5, start_col);
        draw_hline(box_w, '+', '-', '+');
        printf("\033[0m");
        fflush(stdout);

        int k = read_key(200);
        if (k == KEY_NONE) {
            if (g_quit) return false;
            continue;
        }
        if (k == KEY_ESC || k == 3) return false;
        if (k == KEY_ENTER) return true;
        if (k == KEY_TAB && secret) {
            masked = !masked;
            continue;
        }
        if (k == KEY_BACKSPACE) {
            if (len > 0) buf[--len] = '\0';
            continue;
        }
        if (k >= 32 && k < 127 && len + 1 < cap) {
            buf[len++] = (char)k;
            buf[len] = '\0';
        }
    }
}

static bool prompt_confirm(const char *title, const char *question) {
    int rows, cols;
    get_term_size(&rows, &cols);
    int box_w = cols - 8;
    if (box_w > 68) box_w = 68;
    if (box_w < 40) box_w = 40;
    int start_col = (cols - box_w) / 2;
    if (start_col < 1) start_col = 1;
    int start_row = rows / 2 - 2;
    if (start_row < 2) start_row = 2;

    printf("\033[%d;%dH\033[1;33m", start_row, start_col);
    draw_hline(box_w, '+', '-', '+');
    printf("\033[%d;%dH|\033[1;37m", start_row + 1, start_col);
    print_clipped(title, box_w - 2);
    printf("\033[1;33m|\033[%d;%dH|\033[0m", start_row + 2, start_col);
    print_clipped(question, box_w - 2);
    printf("\033[1;33m|\033[%d;%dH|\033[0m", start_row + 3, start_col);
    print_clipped(" Press [y] to confirm, any other key to cancel.", box_w - 2);
    printf("\033[1;33m|\033[%d;%dH", start_row + 4, start_col);
    draw_hline(box_w, '+', '-', '+');
    printf("\033[0m");
    fflush(stdout);

    for (;;) {
        int k = read_key(200);
        if (k == KEY_NONE) {
            if (g_quit) return false;
            continue;
        }
        return (k == 'y' || k == 'Y');
    }
}

static int run_interactive_tui(const cfg_t *cfg) {
    if (!enter_tui_mode()) {
        fprintf(stderr, "spx-wifi-tui requires a TTY (or use --scan / --connect / --status)\n");
        return 1;
    }
    signal(SIGWINCH, handle_sigwinch);
    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigterm);

    ap_entry_t aps[MAX_APS];
    tui_state_t st;
    memset(&st, 0, sizeof(st));
    st.cfg = cfg;
    st.aps = aps;
    snprintf(st.status_msg, sizeof(st.status_msg), "Scanning Wi-Fi networks...");
    draw_ui(&st);

    int n = scan_aps(cfg, true, aps, MAX_APS);
    if (n < 0) {
        st.ap_count = 0;
        snprintf(st.status_msg, sizeof(st.status_msg),
                 "wpa_supplicant not ready on %s yet. Press 'r' to retry.", cfg->wifi_iface);
    } else {
        st.ap_count = n;
        snprintf(st.status_msg, sizeof(st.status_msg), "Found %d Wi-Fi network(s).", n);
    }
    draw_ui(&st);

    int tick = 0;
    while (!g_quit) {
        if (g_resized) {
            g_resized = 0;
            draw_ui(&st);
        }
        int k = read_key(500);
        if (k == KEY_NONE) {
            tick++;
            if (tick >= 10) {
                tick = 0;
                draw_ui(&st);
            }
            continue;
        }
        tick = 0;
        if (k == 'q' || k == 'Q' || k == KEY_ESC || k == 3) {
            break;
        } else if (k == KEY_UP || k == 'k') {
            if (st.selected > 0) st.selected--;
            draw_ui(&st);
        } else if (k == KEY_DOWN || k == 'j') {
            if (st.selected + 1 < st.ap_count) st.selected++;
            draw_ui(&st);
        } else if (k == KEY_PGUP) {
            st.selected = st.selected > 8 ? st.selected - 8 : 0;
            draw_ui(&st);
        } else if (k == KEY_PGDN) {
            st.selected = (st.selected + 8 < st.ap_count) ? st.selected + 8 : (st.ap_count > 0 ? st.ap_count - 1 : 0);
            draw_ui(&st);
        } else if (k == 'r' || k == 'R') {
            snprintf(st.status_msg, sizeof(st.status_msg), "Scanning Wi-Fi networks...");
            draw_ui(&st);
            n = scan_aps(cfg, true, aps, MAX_APS);
            if (n < 0) {
                snprintf(st.status_msg, sizeof(st.status_msg), "Scan failed: wpa_supplicant unavailable.");
            } else {
                st.ap_count = n;
                if (st.selected >= st.ap_count) st.selected = st.ap_count > 0 ? st.ap_count - 1 : 0;
                snprintf(st.status_msg, sizeof(st.status_msg), "Found %d Wi-Fi network(s).", n);
            }
            draw_ui(&st);
        } else if (k == KEY_ENTER && st.ap_count > 0) {
            ap_entry_t target = st.aps[st.selected];
            char pass[MAX_PASS_LEN] = "";
            bool proceed = true;
            if (target.sec != SEC_OPEN) {
                char title[160];
                snprintf(title, sizeof(title), " Connect to: %s (%s)", target.ssid_disp, sec_label(target.sec));
                proceed = prompt_input(title, " Enter Wi-Fi passphrase:", true, pass, sizeof(pass));
            }
            if (proceed) {
                char msg[256] = "";
                connect_wifi(cfg, target.ssid_raw, target.ssid_len, target.sec, pass, false,
                             msg, sizeof(msg), tui_status_cb, &st);
                memset(pass, 0, sizeof(pass));
                snprintf(st.status_msg, sizeof(st.status_msg), "%s", msg);
                n = scan_aps(cfg, false, aps, MAX_APS);
                if (n >= 0) st.ap_count = n;
            } else {
                snprintf(st.status_msg, sizeof(st.status_msg), "Connection cancelled.");
            }
            draw_ui(&st);
        } else if (k == 'h' || k == 'H') {
            char ssid_in[MAX_SSID_RAW] = "";
            char pass[MAX_PASS_LEN] = "";
            if (prompt_input(" Connect to Hidden Wi-Fi", " Enter exact SSID:", false, ssid_in, sizeof(ssid_in)) &&
                ssid_in[0] != '\0') {
                if (prompt_input(" Hidden Wi-Fi Passphrase", " Enter passphrase (leave blank for Open):", true, pass, sizeof(pass))) {
                    sec_mode_t mode = pass[0] ? SEC_WPA2_WPA3 : SEC_OPEN;
                    char msg[256] = "";
                    connect_wifi(cfg, (const unsigned char *)ssid_in, strlen(ssid_in), mode, pass, true,
                                 msg, sizeof(msg), tui_status_cb, &st);
                    memset(pass, 0, sizeof(pass));
                    snprintf(st.status_msg, sizeof(st.status_msg), "%s", msg);
                    n = scan_aps(cfg, false, aps, MAX_APS);
                    if (n >= 0) st.ap_count = n;
                }
            }
            draw_ui(&st);
        } else if (k == 'd' || k == 'D') {
            char msg[256] = "";
            disconnect_wifi(cfg, msg, sizeof(msg));
            snprintf(st.status_msg, sizeof(st.status_msg), "%s", msg);
            n = scan_aps(cfg, false, aps, MAX_APS);
            if (n >= 0) st.ap_count = n;
            draw_ui(&st);
        } else if (k == 's' || k == 'S') {
            char msg[256] = "";
            save_wifi_to_esp(cfg, msg, sizeof(msg));
            snprintf(st.status_msg, sizeof(st.status_msg), "%s", msg);
            draw_ui(&st);
        } else if (k == 'b' || k == 'B') {
            if (prompt_confirm(" Continue Booting Android",
                               " Hand off Wi-Fi/USB ownership and start Android?")) {
                leave_tui_mode();
                char out[1024];
                char *argv[] = { (char *)cfg->start_android, NULL };
                int rc = run_cmd(argv, out, sizeof(out));
                if (out[0]) fputs(out, stdout);
                return rc == 0 ? 0 : 1;
            }
            draw_ui(&st);
        }
    }

    leave_tui_mode();
    return 0;
}

static void print_usage(void) {
    printf("Usage: spx-wifi-tui [option]\n"
           "  (no args)                      Launch interactive Wi-Fi selection & login TUI\n"
           "  --scan                         Scan and print deduplicated Wi-Fi networks\n"
           "  --connect <ssid> [passphrase]  Connect to Wi-Fi and request DHCP lease\n"
           "    [--hidden] [--open] [--wpa3]\n"
           "  --disconnect                   Disconnect Wi-Fi and stop rescue DHCP\n"
           "  --status                       Print Wi-Fi, USB NIC and SSH IP status\n"
           "  -h, --help                     Show this help\n");
}

int main(int argc, char **argv) {
    cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.wpa_cli = env_or("SPX_WPA_CLI", "wpa_cli");
    cfg.wpa_ctrl = env_or("SPX_WPA_CTRL", "/run/wpa_supplicant");
    cfg.udhcpc = env_or("SPX_UDHCPC", "udhcpc");
    cfg.dhcp_hook = env_or("SPX_DHCP_HOOK", "/bin/dhcp-hook");
    cfg.ip_bin = env_or("SPX_IP_BIN", "ip");
    cfg.run_dir = env_or("SPX_RUN_DIR", "/run");
    cfg.sys_net = env_or("SPX_SYS_NET", "/sys/class/net");
    cfg.esp_dir = env_or("SPX_ESP_DIR", "/media/esp");
    cfg.start_android = env_or("SPX_START_ANDROID", "/bin/start-android");
    detect_wifi_iface(&cfg);

    if (argc <= 1) {
        return run_interactive_tui(&cfg);
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(argv[1], "--status") == 0) {
        char st[64], sdisp[MAX_SSID_DISP], wip[64];
        get_wifi_status(&cfg, st, sizeof(st), sdisp, sizeof(sdisp), NULL, NULL);
        get_iface_ipv4(&cfg, cfg.wifi_iface, wip, sizeof(wip));
        printf("wifi_iface=%s state=%s ssid=%s ip=%s\n",
               cfg.wifi_iface, st, sdisp[0] ? sdisp : "none", wip[0] ? wip : "none");
        net_info_t usb[MAX_IFACES];
        int cnt = collect_usb_ifaces(&cfg, usb, MAX_IFACES);
        for (int i = 0; i < cnt; i++) {
            printf("usb_iface=%s ip=%s\n", usb[i].name, usb[i].ip[0] ? usb[i].ip : "none");
        }
        return 0;
    }

    if (strcmp(argv[1], "--scan") == 0) {
        ap_entry_t aps[MAX_APS];
        bool trigger = !(argc > 2 && strcmp(argv[2], "--no-trigger") == 0);
        int n = scan_aps(&cfg, trigger, aps, MAX_APS);
        if (n < 0) {
            fprintf(stderr, "Scan failed on %s\n", cfg.wifi_iface);
            return 1;
        }
        printf("%-2s %-6s %4s %-4s %-6s %s\n", "ST", "SIGNAL", "dBm", "BAND", "SEC", "SSID");
        for (int i = 0; i < n; i++) {
            printf("%-2s %-6s %4d %-4s %-6s %s\n",
                   aps[i].connected ? "*" : " ",
                   signal_bars(aps[i].rssi),
                   aps[i].rssi,
                   freq_band(aps[i].freq),
                   sec_label(aps[i].sec),
                   aps[i].ssid_disp);
        }
        return 0;
    }

    if (strcmp(argv[1], "--disconnect") == 0) {
        char msg[256];
        disconnect_wifi(&cfg, msg, sizeof(msg));
        printf("%s\n", msg);
        return 0;
    }

    if (strcmp(argv[1], "--connect") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Missing SSID for --connect\n");
            return 64;
        }
        const char *ssid = argv[2];
        const char *pass = NULL;
        bool hidden = false;
        bool force_open = false;
        bool force_wpa3 = false;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--hidden") == 0) hidden = true;
            else if (strcmp(argv[i], "--open") == 0) force_open = true;
            else if (strcmp(argv[i], "--wpa3") == 0) force_wpa3 = true;
            else if (!pass) pass = argv[i];
        }
        sec_mode_t sec = SEC_WPA2;
        if (force_open || (!pass && !force_wpa3)) sec = SEC_OPEN;
        else if (force_wpa3) sec = SEC_WPA3;
        else {
            /* Check scan cache for matching SSID security mode if available. */
            ap_entry_t aps[MAX_APS];
            int n = scan_aps(&cfg, false, aps, MAX_APS);
            for (int i = 0; i < n; i++) {
                if (aps[i].ssid_len == strlen(ssid) && memcmp(aps[i].ssid_raw, ssid, aps[i].ssid_len) == 0) {
                    sec = aps[i].sec;
                    break;
                }
            }
        }
        char msg[256] = "";
        bool ok = connect_wifi(&cfg, (const unsigned char *)ssid, strlen(ssid), sec,
                               pass ? pass : "", hidden, msg, sizeof(msg), NULL, NULL);
        printf("%s\n", msg);
        return ok ? 0 : 1;
    }

    print_usage();
    return 64;
}
