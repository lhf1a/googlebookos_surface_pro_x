/* Minimal real-interface legacy HAL for SQ2 ath10k + Android wificond.
 * Interface enumeration, virtual interface create/delete, channel query, and
 * country code updates use Linux nl80211/sysfs directly. Optional QTI firmware
 * offloads remain the caller's NOT_SUPPORTED stubs.
 */
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <mutex>
#include <net/if.h>
#include <new>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>
#include <wifi_hal.h>
#include <android/log.h>

extern "C" int is_wifi_driver_loaded(void);

namespace {
constexpr const char *kLogTag = "sq2-wifi-hal";
constexpr uint32_t kContextMagic = 0x53513243u; // 'SQ2C'
constexpr uint32_t kIfaceMagic   = 0x53513249u; // 'SQ2I'
constexpr size_t kMaxIfaces = 8;

// Device android.hardware.wifi-service / libwifi-hal.so include two Android 17
// RTT v4 slots (0x0e0, 0x100) before wifi_set_country_code, shifting later
// wifi_hal_fn slots by +0x10 bytes relative to AOSP main's wifi_hal.h.
constexpr size_t kV4SlotShift =
    (offsetof(wifi_hal_fn, wifi_get_driver_version) == 0x1a0) ? 0x10 : 0;

static_assert(offsetof(wifi_hal_fn, wifi_get_valid_channels) == 0x0c8,
              "unexpected wifi_get_valid_channels offset");
static_assert(offsetof(wifi_hal_fn, wifi_get_driver_version) == 0x1a0 ||
              offsetof(wifi_hal_fn, wifi_get_driver_version) == 0x1b0,
              "unexpected wifi_get_driver_version offset");
static_assert(offsetof(wifi_hal_fn, wifi_set_country_code) + kV4SlotShift == 0x140,
              "unexpected wifi_set_country_code ABI offset");
static_assert(offsetof(wifi_hal_fn, wifi_get_driver_version) + kV4SlotShift == 0x1b0,
              "unexpected wifi_get_driver_version ABI offset");
static_assert(offsetof(wifi_hal_fn, wifi_virtual_interface_create) + kV4SlotShift == 0x328,
              "unexpected wifi_virtual_interface_create ABI offset");
static_assert(offsetof(wifi_hal_fn, wifi_virtual_interface_delete) + kV4SlotShift == 0x330,
              "unexpected wifi_virtual_interface_delete ABI offset");
static_assert(offsetof(wifi_hal_fn, wifi_get_supported_iface_concurrency_matrix) + kV4SlotShift == 0x448,
              "unexpected wifi_get_supported_iface_concurrency_matrix ABI offset");

template <size_t Offset, typename FnPtr>
inline void assign_shifted(wifi_hal_fn *table, FnPtr fn) {
    auto *slot = reinterpret_cast<FnPtr *>(reinterpret_cast<uint8_t *>(table) + Offset + kV4SlotShift);
    *slot = fn;
}

struct Context;

struct IfaceSlot {
    uint32_t magic = kIfaceMagic;
    bool in_use = false;
    Context *ctx = nullptr;
    char name[IFNAMSIZ]{};
};

struct Context {
    uint32_t magic = kContextMagic;
    std::mutex mutex;
    std::condition_variable ready;
    bool stopping = false;
    wifi_cleaned_up_handler callback = nullptr;
    IfaceSlot slots[kMaxIfaces]{};
    wifi_interface_handle active_list[kMaxIfaces]{};
    int nl_fd = -1;
    uint16_t nl80211_id = 0;
    uint32_t nl_seq = 1;
};

Context *as_context(wifi_handle handle) {
    auto *ctx = reinterpret_cast<Context *>(handle);
    return (ctx && ctx->magic == kContextMagic) ? ctx : nullptr;
}

IfaceSlot *as_iface(wifi_interface_handle handle) {
    auto *raw = reinterpret_cast<const uint32_t *>(handle);
    if (!raw) return nullptr;
    if (*raw == kIfaceMagic) {
        auto *slot = reinterpret_cast<IfaceSlot *>(handle);
        return (slot->in_use && as_context(reinterpret_cast<wifi_handle>(slot->ctx))) ? slot : nullptr;
    }
    return nullptr;
}

Context *iface_context(wifi_interface_handle handle) {
    if (auto *slot = as_iface(handle)) return slot->ctx;
    return as_context(reinterpret_cast<wifi_handle>(handle));
}

wifi_error errno_to_wifi_error(int err) {
    if (err < 0) err = -err;
    switch (err) {
    case 0: return WIFI_SUCCESS;
    case ENOTSUP:
#if EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP:
#endif
        return WIFI_ERROR_NOT_SUPPORTED;
    case EINVAL: return WIFI_ERROR_INVALID_ARGS;
    case EBUSY: return WIFI_ERROR_BUSY;
    case ENOMEM: return WIFI_ERROR_OUT_OF_MEMORY;
    case ENODEV:
    case ENOENT:
        return WIFI_ERROR_NOT_AVAILABLE;
    case ETIMEDOUT:
    case EAGAIN:
        return WIFI_ERROR_TIMED_OUT;
    default: return WIFI_ERROR_UNKNOWN;
    }
}

bool read_sysfs_line(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, size - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
        buf[--n] = '\0';
    }
    return n > 0;
}

bool read_wiphy_index_for_netdev(const char *netdev, uint32_t *out_idx) {
    char path[128], buf[32];
    snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211/index", netdev);
    if (!read_sysfs_line(path, buf, sizeof(buf))) return false;
    char *end = nullptr;
    unsigned long val = strtoul(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out_idx = static_cast<uint32_t>(val);
    return true;
}

bool discover_wiphy_index(uint32_t *out_idx) {
    if (read_wiphy_index_for_netdev("wlan0", out_idx)) return true;

    DIR *net_dir = opendir("/sys/class/net");
    if (net_dir) {
        struct dirent *de = nullptr;
        while ((de = readdir(net_dir)) != nullptr) {
            if (de->d_name[0] == '.') continue;
            if (read_wiphy_index_for_netdev(de->d_name, out_idx)) {
                closedir(net_dir);
                return true;
            }
        }
        closedir(net_dir);
    }

    DIR *phy_dir = opendir("/sys/class/ieee80211");
    if (!phy_dir) return false;
    struct dirent *de = nullptr;
    bool found = false;
    while ((de = readdir(phy_dir)) != nullptr) {
        if (de->d_name[0] == '.') continue;
        char path[160], buf[32];
        snprintf(path, sizeof(path), "/sys/class/ieee80211/%s/index", de->d_name);
        if (read_sysfs_line(path, buf, sizeof(buf))) {
            char *end = nullptr;
            unsigned long val = strtoul(buf, &end, 10);
            if (end && *end == '\0') {
                *out_idx = static_cast<uint32_t>(val);
                found = true;
                break;
            }
        }
    }
    closedir(phy_dir);
    return found;
}

std::vector<std::string> list_phy_netdevs(uint32_t phy_idx) {
    std::vector<std::string> names;
    DIR *dir = opendir("/sys/class/net");
    if (!dir) return names;
    struct dirent *de = nullptr;
    while ((de = readdir(dir)) != nullptr) {
        if (de->d_name[0] == '.') continue;
        uint32_t dev_phy = 0;
        if (read_wiphy_index_for_netdev(de->d_name, &dev_phy) && dev_phy == phy_idx) {
            names.emplace_back(de->d_name);
        }
    }
    closedir(dir);
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        if (a == "wlan0" && b != "wlan0") return true;
        if (b == "wlan0" && a != "wlan0") return false;
        return a < b;
    });
    return names;
}

int refresh_ifaces_locked(Context *ctx) {
    uint32_t phy_idx = 0;
    std::vector<std::string> names;
    if (discover_wiphy_index(&phy_idx)) {
        names = list_phy_netdevs(phy_idx);
    }
    // Keep existing slot pointers stable when an interface remains active.
    for (auto &slot : ctx->slots) {
        if (!slot.in_use) continue;
        if (std::find(names.begin(), names.end(), slot.name) == names.end()) {
            slot.in_use = false;
            slot.name[0] = '\0';
        }
    }
    for (const auto &name : names) {
        if (name.size() >= IFNAMSIZ) continue;
        bool present = false;
        for (const auto &slot : ctx->slots) {
            if (slot.in_use && name == slot.name) {
                present = true;
                break;
            }
        }
        if (present) continue;
        for (auto &slot : ctx->slots) {
            if (!slot.in_use) {
                slot.magic = kIfaceMagic;
                slot.in_use = true;
                slot.ctx = ctx;
                snprintf(slot.name, sizeof(slot.name), "%s", name.c_str());
                break;
            }
        }
    }
    int count = 0;
    for (const auto &name : names) {
        for (auto &slot : ctx->slots) {
            if (slot.in_use && name == slot.name) {
                ctx->active_list[count++] = reinterpret_cast<wifi_interface_handle>(&slot);
                break;
            }
        }
        if (count >= static_cast<int>(kMaxIfaces)) break;
    }
    return count;
}

class NlMsg {
public:
    NlMsg(uint16_t type, uint16_t flags, uint32_t seq, uint8_t genl_cmd, uint8_t version = 1) {
        buf_.resize(NLMSG_HDRLEN + GENL_HDRLEN, 0);
        auto *nlh = reinterpret_cast<nlmsghdr *>(buf_.data());
        nlh->nlmsg_type = type;
        nlh->nlmsg_flags = flags;
        nlh->nlmsg_seq = seq;
        nlh->nlmsg_pid = 0;
        auto *gh = reinterpret_cast<genlmsghdr *>(buf_.data() + NLMSG_HDRLEN);
        gh->cmd = genl_cmd;
        gh->version = version;
    }

    void put_attr(uint16_t type, const void *data, size_t len) {
        size_t offset = buf_.size();
        size_t aligned = NLA_ALIGN(NLA_HDRLEN + len);
        buf_.resize(offset + aligned, 0);
        auto *nla = reinterpret_cast<nlattr *>(buf_.data() + offset);
        nla->nla_type = type;
        nla->nla_len = static_cast<uint16_t>(NLA_HDRLEN + len);
        if (len > 0 && data != nullptr) {
            memcpy(buf_.data() + offset + NLA_HDRLEN, data, len);
        }
    }

    void put_u32(uint16_t type, uint32_t val) { put_attr(type, &val, sizeof(val)); }
    void put_str(uint16_t type, const char *s) { put_attr(type, s, strlen(s) + 1); }
    void put_flag(uint16_t type) { put_attr(type, nullptr, 0); }

    const uint8_t *finalize() {
        auto *nlh = reinterpret_cast<nlmsghdr *>(buf_.data());
        nlh->nlmsg_len = static_cast<uint32_t>(buf_.size());
        return buf_.data();
    }
    size_t size() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

template <typename Fn>
void parse_nlattrs(const uint8_t *data, size_t len, Fn &&cb) {
    size_t offset = 0;
    while (offset + sizeof(nlattr) <= len) {
        const auto *nla = reinterpret_cast<const nlattr *>(data + offset);
        if (nla->nla_len < sizeof(nlattr) || offset + nla->nla_len > len) break;
        const uint16_t attr_type = static_cast<uint16_t>(nla->nla_type & NLA_TYPE_MASK);
        const uint8_t *payload = data + offset + NLA_HDRLEN;
        const size_t payload_len = nla->nla_len - NLA_HDRLEN;
        cb(attr_type, payload, payload_len);
        const size_t step = NLA_ALIGN(nla->nla_len);
        if (step == 0 || offset + step > len) break;
        offset += step;
    }
}

bool ensure_nl_socket_locked(Context *ctx) {
    if (ctx->nl_fd >= 0 && ctx->nl80211_id != 0) return true;
    if (ctx->nl_fd < 0) {
        int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
        if (fd < 0) return false;
        timeval tv{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        sockaddr_nl local{};
        local.nl_family = AF_NETLINK;
        if (bind(fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) < 0) {
            close(fd);
            return false;
        }
        ctx->nl_fd = fd;
    }

    uint32_t seq = ctx->nl_seq++;
    NlMsg req(GENL_ID_CTRL, NLM_F_REQUEST | NLM_F_ACK, seq, CTRL_CMD_GETFAMILY);
    req.put_str(CTRL_ATTR_FAMILY_NAME, NL80211_GENL_NAME);
    const uint8_t *raw = req.finalize();
    if (send(ctx->nl_fd, raw, req.size(), 0) < 0) return false;

    std::vector<uint8_t> rx(8192);
    uint16_t family_id = 0;
    bool done = false;
    while (!done) {
        ssize_t n = recv(ctx->nl_fd, rx.data(), rx.size(), 0);
        if (n <= 0) return false;
        size_t rem = static_cast<size_t>(n);
        for (const auto *nlh = reinterpret_cast<const nlmsghdr *>(rx.data());
             NLMSG_OK(nlh, rem); nlh = NLMSG_NEXT(nlh, rem)) {
            if (nlh->nlmsg_seq != seq) continue;
            if (nlh->nlmsg_type == NLMSG_DONE) {
                done = true;
                break;
            }
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                const auto *err = reinterpret_cast<const nlmsgerr *>(NLMSG_DATA(nlh));
                if (err->error == 0) {
                    done = true;
                    break;
                }
                return false;
            }
            if (nlh->nlmsg_len >= NLMSG_HDRLEN + GENL_HDRLEN) {
                const uint8_t *attrs = reinterpret_cast<const uint8_t *>(nlh) + NLMSG_HDRLEN + GENL_HDRLEN;
                size_t attr_len = nlh->nlmsg_len - (NLMSG_HDRLEN + GENL_HDRLEN);
                parse_nlattrs(attrs, attr_len, [&](uint16_t t, const uint8_t *p, size_t plen) {
                    if (t == CTRL_ATTR_FAMILY_ID && plen >= sizeof(uint16_t)) {
                        memcpy(&family_id, p, sizeof(family_id));
                    }
                });
            }
        }
        if (family_id != 0 && !(reinterpret_cast<const nlmsghdr *>(rx.data())->nlmsg_flags & NLM_F_MULTI)) {
            break;
        }
    }
    ctx->nl80211_id = family_id;
    return family_id != 0;
}

template <typename MsgCb>
int send_nl80211_locked(Context *ctx, uint16_t nl_flags, uint8_t cmd,
                        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> &&attrs,
                        MsgCb &&on_msg) {
    if (!ensure_nl_socket_locked(ctx)) return -ENODEV;
    uint32_t seq = ctx->nl_seq++;
    NlMsg req(ctx->nl80211_id, NLM_F_REQUEST | nl_flags, seq, cmd);
    for (const auto &kv : attrs) {
        req.put_attr(kv.first, kv.second.empty() ? nullptr : kv.second.data(), kv.second.size());
    }
    const uint8_t *raw = req.finalize();
    if (send(ctx->nl_fd, raw, req.size(), 0) < 0) return -errno;

    std::vector<uint8_t> rx(65536);
    while (true) {
        ssize_t n = recv(ctx->nl_fd, rx.data(), rx.size(), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        if (n == 0) return -EIO;
        size_t rem = static_cast<size_t>(n);
        bool finished = false;
        int ret = 0;
        for (const auto *nlh = reinterpret_cast<const nlmsghdr *>(rx.data());
             NLMSG_OK(nlh, rem); nlh = NLMSG_NEXT(nlh, rem)) {
            if (nlh->nlmsg_seq != seq) continue;
            if (nlh->nlmsg_type == NLMSG_DONE) {
                finished = true;
                break;
            }
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                const auto *err = reinterpret_cast<const nlmsgerr *>(NLMSG_DATA(nlh));
                ret = err->error;
                finished = true;
                break;
            }
            if (nlh->nlmsg_len >= NLMSG_HDRLEN + GENL_HDRLEN) {
                const uint8_t *body = reinterpret_cast<const uint8_t *>(nlh) + NLMSG_HDRLEN + GENL_HDRLEN;
                const size_t body_len = nlh->nlmsg_len - (NLMSG_HDRLEN + GENL_HDRLEN);
                on_msg(body, body_len);
                if (!(nlh->nlmsg_flags & NLM_F_MULTI) && !(nl_flags & NLM_F_ACK)) {
                    finished = true;
                }
            }
        }
        if (finished) return ret;
    }
}

std::vector<uint8_t> u32_bytes(uint32_t v) {
    std::vector<uint8_t> b(sizeof(v));
    memcpy(b.data(), &v, sizeof(v));
    return b;
}

std::vector<uint8_t> str_bytes(const char *s) {
    size_t len = strlen(s) + 1;
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(s),
                                reinterpret_cast<const uint8_t *>(s) + len);
}

struct WiphyCaps {
    bool has_ap = false;
    bool has_p2p = false;
    bool has_2g = false;
    bool has_5g = false;
    struct ChanEntry {
        wifi_channel freq;
        bool is_dfs;
    };
    std::vector<ChanEntry> channels;
};

int query_wiphy_caps_locked(Context *ctx, uint32_t phy_idx, WiphyCaps *caps) {
    *caps = WiphyCaps{};
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> attrs;
    attrs.emplace_back(NL80211_ATTR_WIPHY, u32_bytes(phy_idx));
    attrs.emplace_back(NL80211_ATTR_SPLIT_WIPHY_DUMP, std::vector<uint8_t>{});

    return send_nl80211_locked(
        ctx, NLM_F_DUMP, NL80211_CMD_GET_WIPHY, std::move(attrs),
        [caps, phy_idx](const uint8_t *body, size_t body_len) {
            uint32_t msg_phy = UINT32_MAX;
            const uint8_t *iftypes_p = nullptr;
            size_t iftypes_len = 0;
            const uint8_t *bands_p = nullptr;
            size_t bands_len = 0;

            parse_nlattrs(body, body_len, [&](uint16_t t, const uint8_t *p, size_t plen) {
                if (t == NL80211_ATTR_WIPHY && plen >= sizeof(uint32_t)) {
                    memcpy(&msg_phy, p, sizeof(msg_phy));
                } else if (t == NL80211_ATTR_SUPPORTED_IFTYPES) {
                    iftypes_p = p;
                    iftypes_len = plen;
                } else if (t == NL80211_ATTR_WIPHY_BANDS) {
                    bands_p = p;
                    bands_len = plen;
                }
            });
            if (msg_phy != UINT32_MAX && msg_phy != phy_idx) return;

            if (iftypes_p) {
                parse_nlattrs(iftypes_p, iftypes_len, [&](uint16_t iftype, const uint8_t *, size_t) {
                    if (iftype == NL80211_IFTYPE_AP) caps->has_ap = true;
                    if (iftype == NL80211_IFTYPE_P2P_CLIENT ||
                        iftype == NL80211_IFTYPE_P2P_GO ||
                        iftype == NL80211_IFTYPE_P2P_DEVICE) {
                        caps->has_p2p = true;
                    }
                });
            }

            if (bands_p) {
                parse_nlattrs(bands_p, bands_len, [&](uint16_t band_id, const uint8_t *bp, size_t blen) {
                    if (band_id == NL80211_BAND_2GHZ) caps->has_2g = true;
                    if (band_id == NL80211_BAND_5GHZ) caps->has_5g = true;
                    parse_nlattrs(bp, blen, [&](uint16_t bt, const uint8_t *fp, size_t flen) {
                        if (bt != NL80211_BAND_ATTR_FREQS) return;
                        parse_nlattrs(fp, flen, [&](uint16_t, const uint8_t *cp, size_t clen) {
                            uint32_t freq = 0;
                            bool disabled = false;
                            bool radar = false;
                            parse_nlattrs(cp, clen, [&](uint16_t ft, const uint8_t *vp, size_t vlen) {
                                if (ft == NL80211_FREQUENCY_ATTR_FREQ && vlen >= sizeof(uint32_t)) {
                                    memcpy(&freq, vp, sizeof(freq));
                                } else if (ft == NL80211_FREQUENCY_ATTR_DISABLED) {
                                    disabled = true;
                                } else if (ft == NL80211_FREQUENCY_ATTR_RADAR) {
                                    radar = true;
                                }
                            });
                            if (freq == 0 || disabled) return;
                            auto it = std::find_if(caps->channels.begin(), caps->channels.end(),
                                                   [freq](const WiphyCaps::ChanEntry &e) {
                                                       return e.freq == static_cast<wifi_channel>(freq);
                                                   });
                            if (it == caps->channels.end()) {
                                caps->channels.push_back({static_cast<wifi_channel>(freq), radar});
                            } else if (radar) {
                                it->is_dfs = true;
                            }
                        });
                    });
                });
            }
        });
}

int query_iface_type_locked(Context *ctx, unsigned int ifindex, uint32_t *out_type) {
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> attrs;
    attrs.emplace_back(NL80211_ATTR_IFINDEX, u32_bytes(ifindex));
    bool got = false;
    int err = send_nl80211_locked(
        ctx, 0, NL80211_CMD_GET_INTERFACE, std::move(attrs),
        [&](const uint8_t *body, size_t body_len) {
            parse_nlattrs(body, body_len, [&](uint16_t t, const uint8_t *p, size_t plen) {
                if (t == NL80211_ATTR_IFTYPE && plen >= sizeof(uint32_t)) {
                    memcpy(out_type, p, sizeof(*out_type));
                    got = true;
                }
            });
        });
    if (err != 0) return err;
    return got ? 0 : -EIO;
}

feature_set compute_feature_set_locked(Context *ctx) {
    feature_set feat = WIFI_FEATURE_INFRA;
    uint32_t phy_idx = 0;
    WiphyCaps caps;
    if (discover_wiphy_index(&phy_idx) && query_wiphy_caps_locked(ctx, phy_idx, &caps) == 0) {
        if (caps.has_5g) feat |= WIFI_FEATURE_INFRA_5G;
        if (caps.has_p2p) feat |= WIFI_FEATURE_P2P;
        if (caps.has_ap) feat |= WIFI_FEATURE_SOFT_AP | WIFI_FEATURE_AP_STA;
        return feat;
    }
    return WIFI_FEATURE_INFRA | WIFI_FEATURE_INFRA_5G | WIFI_FEATURE_P2P |
           WIFI_FEATURE_SOFT_AP | WIFI_FEATURE_AP_STA;
}

wifi_error initialize(wifi_handle *handle) {
    if (!handle) return WIFI_ERROR_INVALID_ARGS;
    if (!is_wifi_driver_loaded()) return WIFI_ERROR_NOT_AVAILABLE;
    auto *ctx = new (std::nothrow) Context();
    if (!ctx) return WIFI_ERROR_OUT_OF_MEMORY;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        refresh_ifaces_locked(ctx);
    }
    *handle = reinterpret_cast<wifi_handle>(ctx);
    return WIFI_SUCCESS;
}

void cleanup(wifi_handle handle, wifi_cleaned_up_handler callback) {
    auto *ctx = as_context(handle);
    if (!ctx) return;
    int fd_to_close = -1;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->stopping = true;
        ctx->callback = callback;
        fd_to_close = ctx->nl_fd;
        ctx->nl_fd = -1;
    }
    if (fd_to_close >= 0) close(fd_to_close);
    ctx->ready.notify_all();
    if (callback) callback(handle);
}

void eventloop(wifi_handle handle) {
    auto *ctx = as_context(handle);
    if (!ctx) return;
    std::unique_lock<std::mutex> lock(ctx->mutex);
    ctx->ready.wait(lock, [ctx] { return ctx->stopping; });
}

wifi_error ifaces(wifi_handle handle, int *count, wifi_interface_handle **list) {
    auto *ctx = as_context(handle);
    if (!ctx || !count || !list) return WIFI_ERROR_INVALID_ARGS;
    if (!is_wifi_driver_loaded()) return WIFI_ERROR_NOT_AVAILABLE;
    std::lock_guard<std::mutex> lock(ctx->mutex);
    int n = refresh_ifaces_locked(ctx);
    if (n <= 0) return WIFI_ERROR_NOT_AVAILABLE;
    *count = n;
    *list = ctx->active_list;
    return WIFI_SUCCESS;
}

wifi_error ifname(wifi_interface_handle handle, char *name, size_t size) {
    auto *slot = as_iface(handle);
    if (!slot) {
        auto *ctx = as_context(reinterpret_cast<wifi_handle>(handle));
        if (!ctx || !name || size < sizeof("wlan0")) return WIFI_ERROR_INVALID_ARGS;
        memcpy(name, "wlan0", sizeof("wlan0"));
        return WIFI_SUCCESS;
    }
    size_t need = strlen(slot->name) + 1;
    if (!name || size < need) return WIFI_ERROR_INVALID_ARGS;
    memcpy(name, slot->name, need);
    return WIFI_SUCCESS;
}

wifi_error features(wifi_interface_handle handle, feature_set *set) {
    auto *ctx = iface_context(handle);
    if (!ctx || !set) return WIFI_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(ctx->mutex);
    *set = compute_feature_set_locked(ctx);
    return WIFI_SUCCESS;
}

wifi_error concurrency(wifi_interface_handle handle, int size, feature_set *matrix, int *count) {
    auto *ctx = iface_context(handle);
    if (!ctx || size < 1 || !matrix || !count) return WIFI_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(ctx->mutex);
    matrix[0] = compute_feature_set_locked(ctx);
    *count = 1;
    return WIFI_SUCCESS;
}

wifi_error driver_version(wifi_interface_handle handle, char *buffer, int buffer_size) {
    auto *ctx = iface_context(handle);
    if (!ctx || !buffer || buffer_size < 16) return WIFI_ERROR_INVALID_ARGS;

    char driver_name[64] = "nl80211";
    uint32_t phy_idx = 0;
    if (discover_wiphy_index(&phy_idx)) {
        char link_path[128], resolved[256];
        snprintf(link_path, sizeof(link_path), "/sys/class/ieee80211/phy%u/device/driver", phy_idx);
        ssize_t rlen = readlink(link_path, resolved, sizeof(resolved) - 1);
        if (rlen > 0) {
            resolved[rlen] = '\0';
            const char *base = strrchr(resolved, '/');
            base = base ? base + 1 : resolved;
            snprintf(driver_name, sizeof(driver_name), "%s", base);
        }
    }

    utsname u{};
    if (uname(&u) == 0 && u.release[0] != '\0') {
        snprintf(buffer, static_cast<size_t>(buffer_size), "%s %s", driver_name, u.release);
    } else {
        snprintf(buffer, static_cast<size_t>(buffer_size), "%s", driver_name);
    }
    return WIFI_SUCCESS;
}

wifi_error set_country_code(wifi_interface_handle handle, const char *country_code) {
    auto *ctx = iface_context(handle);
    if (!ctx || !country_code || strlen(country_code) != 2) return WIFI_ERROR_INVALID_ARGS;
    char alpha2[3] = {
        static_cast<char>(toupper(static_cast<unsigned char>(country_code[0]))),
        static_cast<char>(toupper(static_cast<unsigned char>(country_code[1]))),
        '\0'
    };
    if (!((isalpha(alpha2[0]) && isalpha(alpha2[1])) || (alpha2[0] == '0' && alpha2[1] == '0'))) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    std::lock_guard<std::mutex> lock(ctx->mutex);
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> attrs;
    attrs.emplace_back(NL80211_ATTR_REG_ALPHA2, str_bytes(alpha2));
    int err = send_nl80211_locked(ctx, NLM_F_ACK, NL80211_CMD_REQ_SET_REG, std::move(attrs),
                                  [](const uint8_t *, size_t) {});
    if (err == -EALREADY) return WIFI_SUCCESS;
    if (err != 0) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "NL80211_CMD_REQ_SET_REG(%s) failed: %d", alpha2, err);
        return errno_to_wifi_error(err);
    }
    return WIFI_SUCCESS;
}

wifi_error valid_channels(wifi_interface_handle handle, int band, int max_channels,
                          wifi_channel *channels, int *num_channels) {
    auto *ctx = iface_context(handle);
    if (!ctx || max_channels <= 0 || !channels || !num_channels) return WIFI_ERROR_INVALID_ARGS;
    *num_channels = 0;
    if (!is_wifi_driver_loaded()) return WIFI_ERROR_NOT_AVAILABLE;

    uint32_t phy_idx = 0;
    if (!discover_wiphy_index(&phy_idx)) return WIFI_ERROR_NOT_AVAILABLE;

    std::lock_guard<std::mutex> lock(ctx->mutex);
    WiphyCaps caps;
    int err = query_wiphy_caps_locked(ctx, phy_idx, &caps);
    if (err != 0) return errno_to_wifi_error(err);

    const bool want_2g = (band == WIFI_BAND_UNSPECIFIED) || ((band & WIFI_BAND_BG) != 0);
    const bool want_5g_no_dfs = (band == WIFI_BAND_UNSPECIFIED) || ((band & WIFI_BAND_A) != 0);
    const bool want_5g_dfs = (band & WIFI_BAND_A_DFS) != 0;

    int count = 0;
    for (const auto &ch : caps.channels) {
        if (count >= max_channels) break;
        const int f = ch.freq;
        bool match = false;
        if (f >= 2400 && f <= 2500) {
            match = want_2g;
        } else if (f >= 4900 && f < 5900) {
            match = ch.is_dfs ? want_5g_dfs : want_5g_no_dfs;
        }
        if (match) {
            channels[count++] = f;
        }
    }
    *num_channels = count;
    return WIFI_SUCCESS;
}

wifi_error virtual_iface_create(wifi_handle handle, const char *ifname, wifi_interface_type iface_type) {
    auto *ctx = as_context(handle);
    if (!ctx || !ifname || ifname[0] == '\0' || strlen(ifname) >= IFNAMSIZ) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    if (!is_wifi_driver_loaded()) return WIFI_ERROR_NOT_AVAILABLE;

    uint32_t target_iftype = 0;
    switch (iface_type) {
    case WIFI_INTERFACE_TYPE_STA: target_iftype = NL80211_IFTYPE_STATION; break;
    case WIFI_INTERFACE_TYPE_AP: target_iftype = NL80211_IFTYPE_AP; break;
    case WIFI_INTERFACE_TYPE_P2P: target_iftype = NL80211_IFTYPE_P2P_CLIENT; break;
    default: return WIFI_ERROR_NOT_SUPPORTED;
    }

    uint32_t phy_idx = 0;
    if (!discover_wiphy_index(&phy_idx)) return WIFI_ERROR_NOT_AVAILABLE;

    std::lock_guard<std::mutex> lock(ctx->mutex);
    unsigned int existing = if_nametoindex(ifname);
    if (existing != 0) {
        uint32_t cur_iftype = 0;
        if (query_iface_type_locked(ctx, existing, &cur_iftype) == 0 && cur_iftype == target_iftype) {
            refresh_ifaces_locked(ctx);
            return WIFI_SUCCESS;
        }
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> attrs;
        attrs.emplace_back(NL80211_ATTR_IFINDEX, u32_bytes(existing));
        attrs.emplace_back(NL80211_ATTR_IFTYPE, u32_bytes(target_iftype));
        int err = send_nl80211_locked(ctx, NLM_F_ACK, NL80211_CMD_SET_INTERFACE, std::move(attrs),
                                      [](const uint8_t *, size_t) {});
        if (err != 0) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                                "NL80211_CMD_SET_INTERFACE(%s, %u) failed: %d",
                                ifname, target_iftype, err);
            return errno_to_wifi_error(err);
        }
        refresh_ifaces_locked(ctx);
        return WIFI_SUCCESS;
    }

    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> attrs;
    attrs.emplace_back(NL80211_ATTR_WIPHY, u32_bytes(phy_idx));
    attrs.emplace_back(NL80211_ATTR_IFNAME, str_bytes(ifname));
    attrs.emplace_back(NL80211_ATTR_IFTYPE, u32_bytes(target_iftype));
    int err = send_nl80211_locked(ctx, NLM_F_ACK, NL80211_CMD_NEW_INTERFACE, std::move(attrs),
                                  [](const uint8_t *, size_t) {});
    if (err != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "NL80211_CMD_NEW_INTERFACE(phy%u, %s, %u) failed: %d",
                            phy_idx, ifname, target_iftype, err);
        return errno_to_wifi_error(err);
    }
    refresh_ifaces_locked(ctx);
    return WIFI_SUCCESS;
}

wifi_error virtual_iface_delete(wifi_handle handle, const char *ifname) {
    auto *ctx = as_context(handle);
    if (!ctx || !ifname || ifname[0] == '\0') return WIFI_ERROR_INVALID_ARGS;
    unsigned int idx = if_nametoindex(ifname);
    if (idx == 0) return WIFI_SUCCESS;

    std::lock_guard<std::mutex> lock(ctx->mutex);
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> attrs;
    attrs.emplace_back(NL80211_ATTR_IFINDEX, u32_bytes(idx));
    int err = send_nl80211_locked(ctx, NLM_F_ACK, NL80211_CMD_DEL_INTERFACE, std::move(attrs),
                                  [](const uint8_t *, size_t) {});
    if (err != 0 && err != -ENODEV) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "NL80211_CMD_DEL_INTERFACE(%s) failed: %d", ifname, err);
        return errno_to_wifi_error(err);
    }
    refresh_ifaces_locked(ctx);
    return WIFI_SUCCESS;
}

wifi_error supported_iface_concurrency_matrix(wifi_handle handle, wifi_iface_concurrency_matrix *matrix) {
    auto *ctx = as_context(handle);
    if (!ctx || !matrix) return WIFI_ERROR_INVALID_ARGS;
    if (!is_wifi_driver_loaded()) return WIFI_ERROR_NOT_AVAILABLE;

    uint32_t phy_idx = 0;
    if (!discover_wiphy_index(&phy_idx)) return WIFI_ERROR_NOT_AVAILABLE;

    std::lock_guard<std::mutex> lock(ctx->mutex);
    WiphyCaps caps;
    int err = query_wiphy_caps_locked(ctx, phy_idx, &caps);
    if (err != 0) return errno_to_wifi_error(err);

    memset(matrix, 0, sizeof(*matrix));
    u32 n = 0;
    if (caps.has_ap) {
        auto &c = matrix->iface_combinations[n++];
        c.max_ifaces = 2;
        c.num_iface_limits = 2;
        c.iface_limits[0] = {1, 1u << WIFI_INTERFACE_TYPE_STA};
        c.iface_limits[1] = {1, 1u << WIFI_INTERFACE_TYPE_AP};
    }
    if (caps.has_p2p) {
        auto &c = matrix->iface_combinations[n++];
        c.max_ifaces = 2;
        c.num_iface_limits = 2;
        c.iface_limits[0] = {1, 1u << WIFI_INTERFACE_TYPE_STA};
        c.iface_limits[1] = {1, 1u << WIFI_INTERFACE_TYPE_P2P};
    }
    if (n == 0) {
        auto &c = matrix->iface_combinations[n++];
        c.max_ifaces = 1;
        c.num_iface_limits = 1;
        c.iface_limits[0] = {1, 1u << WIFI_INTERFACE_TYPE_STA};
    }
    matrix->num_iface_combinations = n;
    return WIFI_SUCCESS;
}
} // namespace

extern "C" wifi_error init_wifi_vendor_hal_func_table(wifi_hal_fn *fn) {
    if (!fn) return WIFI_ERROR_INVALID_ARGS;
    fn->wifi_initialize = initialize;
    fn->wifi_cleanup = cleanup;
    fn->wifi_event_loop = eventloop;
    fn->wifi_get_ifaces = ifaces;
    fn->wifi_get_iface_name = ifname;
    fn->wifi_get_supported_feature_set = features;
    fn->wifi_get_concurrency_matrix = concurrency;
    fn->wifi_get_valid_channels = valid_channels;

    assign_shifted<offsetof(wifi_hal_fn, wifi_set_country_code)>(fn, set_country_code);
    assign_shifted<offsetof(wifi_hal_fn, wifi_get_driver_version)>(fn, driver_version);
    assign_shifted<offsetof(wifi_hal_fn, wifi_virtual_interface_create)>(fn, virtual_iface_create);
    assign_shifted<offsetof(wifi_hal_fn, wifi_virtual_interface_delete)>(fn, virtual_iface_delete);
    assign_shifted<offsetof(wifi_hal_fn, wifi_get_supported_iface_concurrency_matrix)>(
        fn, supported_iface_concurrency_matrix);
    return WIFI_SUCCESS;
}
