#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <linux/fib_rules.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

static int spx_has_prefix(const char *s, const char *p) {
    size_t n;
    if (!s || !p) {
        return 0;
    }
    n = strlen(p);
    return strncmp(s, p, n) == 0;
}

static int spx_is_vm_iface(const char *name) {
    if (!name || !name[0]) {
        return 0;
    }
    return spx_has_prefix(name, "avf_tap") ||
           spx_has_prefix(name, "vmnic") ||
           spx_has_prefix(name, "crosvm");
}

static int spx_is_vpn_iface(const char *name) {
    if (!name || !name[0]) {
        return 0;
    }
    return spx_has_prefix(name, "tun") ||
           spx_has_prefix(name, "wg") ||
           spx_has_prefix(name, "ipsec") ||
           spx_has_prefix(name, "ppp") ||
           spx_has_prefix(name, "utun") ||
           spx_has_prefix(name, "tailscale") ||
           spx_has_prefix(name, "zt");
}

static int spx_is_owned_rule_priority(uint32_t prio) {
    return prio == 12100U || prio == 12400U || prio == 12500U;
}

static int spx_iface_interesting(const char *name) {
    return spx_is_vm_iface(name) || spx_is_vpn_iface(name);
}

static void spx_copy_rta_str(const struct rtattr *rta, char *dst, size_t dst_sz) {
    size_t copy_len;
    if (!dst || dst_sz == 0) {
        return;
    }
    dst[0] = '\0';
    if (!rta) {
        return;
    }
    copy_len = RTA_PAYLOAD(rta);
    if (copy_len >= dst_sz) {
        copy_len = dst_sz - 1U;
    }
    if (copy_len > 0) {
        memcpy(dst, RTA_DATA(rta), copy_len);
    }
    dst[copy_len] = '\0';
}

static int spx_nlmsg_is_relevant(const struct nlmsghdr *nlh,
                                 int (*idx_to_name)(unsigned int, char *)) {
    char ifname[IF_NAMESIZE];
    if (!nlh || nlh->nlmsg_len < NLMSG_HDRLEN) {
        return 0;
    }

    switch (nlh->nlmsg_type) {
    case RTM_NEWLINK:
    case RTM_DELLINK: {
        const struct ifinfomsg *ifi;
        const struct rtattr *rta;
        int attrlen;
        int has_name = 0;

        if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifi))) {
            return 0;
        }
        ifi = (const struct ifinfomsg *)NLMSG_DATA(nlh);
        rta = (const struct rtattr *)((const char *)ifi + NLMSG_ALIGN(sizeof(*ifi)));
        attrlen = (int)(nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifi)));
        ifname[0] = '\0';
        for (; RTA_OK(rta, attrlen); rta = RTA_NEXT(rta, attrlen)) {
            if (rta->rta_type == IFLA_IFNAME) {
                spx_copy_rta_str(rta, ifname, sizeof(ifname));
                has_name = 1;
                break;
            }
        }
        if (has_name) {
            return spx_iface_interesting(ifname);
        }
        if (idx_to_name && ifi->ifi_index > 0 &&
            idx_to_name((unsigned int)ifi->ifi_index, ifname) == 0) {
            return spx_iface_interesting(ifname);
        }
        return 1;
    }

    case RTM_NEWADDR:
    case RTM_DELADDR: {
        const struct ifaddrmsg *ifa;
        const struct rtattr *rta;
        int attrlen;
        char label[IF_NAMESIZE];

        if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifa))) {
            return 0;
        }
        ifa = (const struct ifaddrmsg *)NLMSG_DATA(nlh);
        if (ifa->ifa_index == 0) {
            return 0;
        }
        if (idx_to_name && idx_to_name((unsigned int)ifa->ifa_index, ifname) == 0) {
            return spx_iface_interesting(ifname);
        }
        label[0] = '\0';
        rta = (const struct rtattr *)((const char *)ifa + NLMSG_ALIGN(sizeof(*ifa)));
        attrlen = (int)(nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifa)));
        for (; RTA_OK(rta, attrlen); rta = RTA_NEXT(rta, attrlen)) {
            if (rta->rta_type == IFA_LABEL) {
                spx_copy_rta_str(rta, label, sizeof(label));
                break;
            }
        }
        if (label[0] != '\0') {
            return spx_iface_interesting(label);
        }
        return 1;
    }

    case RTM_NEWRULE:
    case RTM_DELRULE: {
        const struct fib_rule_hdr *frh;
        const struct rtattr *rta;
        int attrlen;

        if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*frh))) {
            return 0;
        }
        frh = (const struct fib_rule_hdr *)NLMSG_DATA(nlh);
        rta = (const struct rtattr *)((const char *)frh + NLMSG_ALIGN(sizeof(*frh)));
        attrlen = (int)(nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*frh)));
        for (; RTA_OK(rta, attrlen); rta = RTA_NEXT(rta, attrlen)) {
            if (rta->rta_type == FRA_PRIORITY && RTA_PAYLOAD(rta) >= sizeof(uint32_t)) {
                uint32_t prio = 0;
                memcpy(&prio, RTA_DATA(rta), sizeof(prio));
                if (spx_is_owned_rule_priority(prio)) {
                    return 0;
                }
                return 1;
            }
        }
        return 1;
    }

    case RTM_NEWROUTE:
    case RTM_DELROUTE: {
        const struct rtmsg *rtm;
        const struct rtattr *rta;
        int attrlen;
        uint32_t table;
        uint32_t oif = 0;
        int has_oif = 0;

        if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*rtm))) {
            return 0;
        }
        rtm = (const struct rtmsg *)NLMSG_DATA(nlh);
        table = rtm->rtm_table;
        rta = (const struct rtattr *)((const char *)rtm + NLMSG_ALIGN(sizeof(*rtm)));
        attrlen = (int)(nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*rtm)));
        for (; RTA_OK(rta, attrlen); rta = RTA_NEXT(rta, attrlen)) {
            if (rta->rta_type == RTA_OIF && RTA_PAYLOAD(rta) >= sizeof(uint32_t)) {
                memcpy(&oif, RTA_DATA(rta), sizeof(oif));
                has_oif = 1;
            } else if (rta->rta_type == RTA_TABLE && RTA_PAYLOAD(rta) >= sizeof(uint32_t)) {
                memcpy(&table, RTA_DATA(rta), sizeof(table));
            }
        }
        if (has_oif) {
            if (oif == 0) {
                return 0;
            }
            if (idx_to_name && idx_to_name((unsigned int)oif, ifname) == 0) {
                return spx_iface_interesting(ifname);
            }
            return 1;
        }
        return table != RT_TABLE_LOCAL;
    }

    default:
        return 0;
    }
}

#ifndef SPX_AVF_VPN_WATCH_TEST

static volatile sig_atomic_t g_stop = 0;

static void on_stop_signal(int signo) {
    (void)signo;
    g_stop = 1;
}

static int default_idx_to_name(unsigned int ifindex, char *buf) {
    return if_indextoname(ifindex, buf) != NULL ? 0 : -1;
}

static int64_t now_monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000L);
}

static int run_reconcile(const char *reconcile_path, const char *action) {
    pid_t pid = fork();
    int status = 0;

    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        char *const argv[] = { (char *)reconcile_path, (char *)action, NULL };
        execv(reconcile_path, argv);
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

static int open_rtnetlink_socket(void) {
    int fd;
    int rcvbuf = 262144;
    struct sockaddr_nl addr;
    static const int groups[] = {
        RTNLGRP_LINK,
        RTNLGRP_IPV4_IFADDR,
        RTNLGRP_IPV4_ROUTE,
        RTNLGRP_IPV4_RULE,
        RTNLGRP_IPV6_IFADDR,
        RTNLGRP_IPV6_ROUTE,
        RTNLGRP_IPV6_RULE,
    };
    size_t i;

    fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) {
        perror("socket(AF_NETLINK)");
        return -1;
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind(AF_NETLINK)");
        close(fd);
        return -1;
    }

    for (i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
        int grp = groups[i];
        if (setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &grp, sizeof(grp)) < 0) {
            if (grp == RTNLGRP_IPV6_IFADDR || grp == RTNLGRP_IPV6_ROUTE || grp == RTNLGRP_IPV6_RULE) {
                continue;
            }
            perror("setsockopt(NETLINK_ADD_MEMBERSHIP)");
            close(fd);
            return -1;
        }
    }
    return fd;
}

static int drain_netlink_events(int fd) {
    char buf[32768];
    int relevant = 0;

    for (;;) {
        ssize_t len = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        const struct nlmsghdr *nlh;
        int rem;

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
        nlh = (const struct nlmsghdr *)buf;
        rem = (int)len;
        for (; NLMSG_OK(nlh, (unsigned int)rem); nlh = NLMSG_NEXT(nlh, rem)) {
            if (nlh->nlmsg_type == NLMSG_DONE || nlh->nlmsg_type == NLMSG_ERROR) {
                continue;
            }
            if (spx_nlmsg_is_relevant(nlh, default_idx_to_name)) {
                relevant = 1;
            }
        }
    }
    return relevant;
}

int main(int argc, char **argv) {
    const char *reconcile_path = "/bin/spx-avf-vpn-reconcile";
    const char *pidfile = NULL;
    int debounce_ms = 250;
    int oneshot = 0;
    int fd = -1;
    int pending = 0;
    int64_t deadline_ms = 0;
    int i;
    struct sigaction sa;
    struct sigaction ign;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--reconcile") == 0 && i + 1 < argc) {
            reconcile_path = argv[++i];
        } else if (strcmp(argv[i], "--pidfile") == 0 && i + 1 < argc) {
            pidfile = argv[++i];
        } else if (strcmp(argv[i], "--debounce-ms") == 0 && i + 1 < argc) {
            long val = strtol(argv[++i], NULL, 10);
            if (val < 50) {
                val = 50;
            } else if (val > 5000) {
                val = 5000;
            }
            debounce_ms = (int)val;
        } else if (strcmp(argv[i], "--oneshot") == 0) {
            oneshot = 1;
        } else {
            fprintf(stderr, "Usage: %s [--reconcile PATH] [--pidfile PATH] [--debounce-ms N] [--oneshot]\n", argv[0]);
            return 64;
        }
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

    if (write_pidfile(pidfile) != 0) {
        perror("write_pidfile");
        return 1;
    }

    if (oneshot) {
        int rc = run_reconcile(reconcile_path, "apply");
        if (pidfile) {
            unlink(pidfile);
        }
        return rc < 0 ? 1 : rc;
    }

    fd = open_rtnetlink_socket();
    if (fd < 0) {
        if (pidfile) {
            unlink(pidfile);
        }
        return 1;
    }

    (void)run_reconcile(reconcile_path, "apply");

    while (!g_stop) {
        struct pollfd pfd;
        int timeout_ms = -1;
        int prc;

        if (pending) {
            int64_t now = now_monotonic_ms();
            int64_t rem = deadline_ms - now;
            if (rem <= 0) {
                pending = 0;
                (void)run_reconcile(reconcile_path, "apply");
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
            if (pending) {
                pending = 0;
                (void)run_reconcile(reconcile_path, "apply");
            }
            continue;
        }
        if (pfd.revents & POLLIN) {
            if (drain_netlink_events(fd)) {
                pending = 1;
                deadline_ms = now_monotonic_ms() + (int64_t)debounce_ms;
            }
        }
    }

    (void)run_reconcile(reconcile_path, "off");
    close(fd);
    if (pidfile) {
        unlink(pidfile);
    }
    return 0;
}

#endif /* SPX_AVF_VPN_WATCH_TEST */
