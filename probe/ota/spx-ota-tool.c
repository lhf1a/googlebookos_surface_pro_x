/* SPDX-License-Identifier: MIT
 * Zero-dependency OTA & internal-disk helper for Surface Pro X Googlebook OS:
 *   - sha256 <path-or-minus>
 *   - detect <path-or-minus>
 *   - inspect-gpt <path-or-minus>
 *   - extract-stream <super_dst|-> <super_off> <super_len> <esp_dst|-> <esp_off> <esp_len>
 *   - write-gpt-5part <target> <super_bytes> <meta_bytes> <misc_bytes> <esp_bytes> [total_bytes]
 *   - copy-verify <src> <dst> <bytes> [progress_file]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define IO_CHUNK (4 * 1024 * 1024)

/* ---------- SHA-256 (FIPS 180-4) ---------- */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
} sha256_ctx;

static const uint32_t K256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static void sha256_transform(sha256_ctx *ctx, const uint8_t block[64]) {
    uint32_t m[64];
    for (int i = 0; i < 16; ++i) {
        m[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(m[i - 15], 7) ^ rotr32(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr32(m[i - 2], 17) ^ rotr32(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + K256[i] + m[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *buf, size_t len) {
    size_t i = 0;
    if (ctx->datalen > 0) {
        size_t need = 64 - ctx->datalen;
        if (len < need) {
            memcpy(ctx->data + ctx->datalen, buf, len);
            ctx->datalen += len;
            return;
        }
        memcpy(ctx->data + ctx->datalen, buf, need);
        sha256_transform(ctx, ctx->data);
        ctx->bitlen += 512;
        ctx->datalen = 0;
        i += need;
    }
    while (i + 64 <= len) {
        sha256_transform(ctx, buf + i);
        ctx->bitlen += 512;
        i += 64;
    }
    if (i < len) {
        ctx->datalen = len - i;
        memcpy(ctx->data, buf + i, ctx->datalen);
    }
}

static void sha256_final_hex(sha256_ctx *ctx, char hex[65]) {
    size_t i = ctx->datalen;
    ctx->bitlen += (uint64_t)ctx->datalen * 8u;
    ctx->data[i++] = 0x80u;
    if (i > 56) {
        while (i < 64) ctx->data[i++] = 0;
        sha256_transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56) ctx->data[i++] = 0;
    for (int k = 0; k < 8; ++k) {
        ctx->data[56 + k] = (uint8_t)(ctx->bitlen >> (56 - k * 8));
    }
    sha256_transform(ctx, ctx->data);
    static const char digits[] = "0123456789abcdef";
    for (int k = 0; k < 8; ++k) {
        uint32_t w = ctx->state[k];
        hex[k * 8 + 0] = digits[(w >> 28) & 0xf];
        hex[k * 8 + 1] = digits[(w >> 24) & 0xf];
        hex[k * 8 + 2] = digits[(w >> 20) & 0xf];
        hex[k * 8 + 3] = digits[(w >> 16) & 0xf];
        hex[k * 8 + 4] = digits[(w >> 12) & 0xf];
        hex[k * 8 + 5] = digits[(w >> 8) & 0xf];
        hex[k * 8 + 6] = digits[(w >> 4) & 0xf];
        hex[k * 8 + 7] = digits[w & 0xf];
    }
    hex[64] = '\0';
}

/* ---------- CRC32 (IEEE 802.3) ---------- */

static uint32_t crc32_calc(const uint8_t *buf, size_t len) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1u) crc = (crc >> 1) ^ 0xedb88320u;
            else crc >>= 1;
        }
    }
    return ~crc;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static ssize_t read_exact(int fd, uint8_t *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static int write_all(int fd, const uint8_t *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int parse_u64(const char *s, uint64_t *out) {
    if (!s || !*s || *s == '-') return -1;
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || !end || *end != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static int open_input(const char *path, bool *should_close) {
    if (strcmp(path, "-") == 0) {
        *should_close = false;
        return STDIN_FILENO;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return -1;
    }
    *should_close = true;
    return fd;
}

/* ---------- Subcommands ---------- */

static int cmd_sha256(const char *path) {
    bool close_fd = false;
    int fd = open_input(path, &close_fd);
    if (fd < 0) return 1;
    uint8_t *buf = malloc(IO_CHUNK);
    if (!buf) {
        if (close_fd) close(fd);
        return 1;
    }
    sha256_ctx ctx;
    sha256_init(&ctx);
    for (;;) {
        ssize_t n = read(fd, buf, IO_CHUNK);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "read error: %s\n", strerror(errno));
            free(buf);
            if (close_fd) close(fd);
            return 1;
        }
        if (n == 0) break;
        sha256_update(&ctx, buf, (size_t)n);
    }
    free(buf);
    if (close_fd) close(fd);
    char hex[65];
    sha256_final_hex(&ctx, hex);
    puts(hex);
    return 0;
}

static int cmd_detect(const char *path) {
    bool close_fd = false;
    int fd = open_input(path, &close_fd);
    if (fd < 0) return 1;
    uint8_t hdr[1024];
    ssize_t n = read_exact(fd, hdr, sizeof(hdr));
    if (close_fd) close(fd);
    if (n < 2) {
        puts("unknown");
        return 0;
    }
    if (hdr[0] == 0x1fu && hdr[1] == 0x8bu) {
        puts("gzip");
        return 0;
    }
    if (n >= 520 && hdr[510] == 0x55u && hdr[511] == 0xaau &&
        memcmp(hdr + 512, "EFI PART", 8) == 0) {
        puts("full-img");
        return 0;
    }
    if (n >= 263 && memcmp(hdr + 257, "ustar", 5) == 0) {
        puts("tar");
        return 0;
    }
    puts("unknown");
    return 0;
}

static int cmd_inspect_gpt(const char *path) {
    bool close_fd = false;
    int fd = open_input(path, &close_fd);
    if (fd < 0) return 1;

    uint8_t head[1024];
    if (read_exact(fd, head, sizeof(head)) != (ssize_t)sizeof(head)) {
        fprintf(stderr, "short GPT header\n");
        if (close_fd) close(fd);
        return 1;
    }
    if (head[510] != 0x55u || head[511] != 0xaau) {
        fprintf(stderr, "missing protective MBR signature\n");
        if (close_fd) close(fd);
        return 1;
    }
    const uint8_t *gpt = head + 512;
    if (memcmp(gpt, "EFI PART", 8) != 0) {
        fprintf(stderr, "missing EFI PART signature\n");
        if (close_fd) close(fd);
        return 1;
    }
    uint32_t hdr_size = le32(gpt + 12);
    uint32_t hdr_crc = le32(gpt + 16);
    if (hdr_size < 92 || hdr_size > 512) {
        fprintf(stderr, "invalid GPT header size: %u\n", hdr_size);
        if (close_fd) close(fd);
        return 1;
    }
    uint8_t hdr_copy[512];
    memcpy(hdr_copy, gpt, hdr_size);
    memset(hdr_copy + 16, 0, 4);
    if (crc32_calc(hdr_copy, hdr_size) != hdr_crc) {
        fprintf(stderr, "GPT header CRC mismatch\n");
        if (close_fd) close(fd);
        return 1;
    }
    uint64_t entries_lba = le64(gpt + 72);
    uint32_t num_entries = le32(gpt + 80);
    uint32_t entry_size = le32(gpt + 84);
    uint32_t entries_crc = le32(gpt + 88);
    if (entries_lba < 2 || entry_size < 128 || entry_size > 512 ||
        num_entries == 0 || num_entries > 4096) {
        fprintf(stderr, "unsupported GPT entry table\n");
        if (close_fd) close(fd);
        return 1;
    }
    uint64_t skip = entries_lba * 512u - 1024u;
    uint8_t scratch[4096];
    while (skip > 0) {
        size_t chunk = skip > sizeof(scratch) ? sizeof(scratch) : (size_t)skip;
        if (read_exact(fd, scratch, chunk) != (ssize_t)chunk) {
            fprintf(stderr, "unexpected EOF before GPT entries\n");
            if (close_fd) close(fd);
            return 1;
        }
        skip -= chunk;
    }
    size_t table_bytes = (size_t)num_entries * (size_t)entry_size;
    uint8_t *table = malloc(table_bytes);
    if (!table) {
        if (close_fd) close(fd);
        return 1;
    }
    if (read_exact(fd, table, table_bytes) != (ssize_t)table_bytes) {
        fprintf(stderr, "short GPT entry table\n");
        free(table);
        if (close_fd) close(fd);
        return 1;
    }
    if (close_fd) close(fd);
    if (crc32_calc(table, table_bytes) != entries_crc) {
        fprintf(stderr, "GPT entries CRC mismatch\n");
        free(table);
        return 1;
    }
    for (uint32_t i = 0; i < num_entries; ++i) {
        const uint8_t *e = table + (size_t)i * entry_size;
        bool zero_type = true;
        for (int b = 0; b < 16; ++b) {
            if (e[b] != 0) { zero_type = false; break; }
        }
        if (zero_type) continue;
        uint64_t first_lba = le64(e + 32);
        uint64_t last_lba = le64(e + 40);
        if (last_lba < first_lba) continue;
        char name[40];
        size_t nlen = 0;
        for (int k = 0; k < 36; ++k) {
            uint16_t ch = (uint16_t)e[56 + k * 2] | ((uint16_t)e[56 + k * 2 + 1] << 8);
            if (ch == 0) break;
            if (ch < 32 || ch > 126 || ch == '\t') name[nlen++] = '_';
            else name[nlen++] = (char)ch;
        }
        name[nlen] = '\0';
        uint64_t offset = first_lba * 512u;
        uint64_t size = (last_lba - first_lba + 1u) * 512u;
        printf("%s\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n",
               name, offset, size, first_lba, last_lba);
    }
    free(table);
    return 0;
}

typedef struct {
    const char *label;
    const char *dst;
    uint64_t offset;
    uint64_t length;
    int fd;
    sha256_ctx sha;
    bool active;
    bool done;
} region_target;

static int skip_bytes(int in_fd, uint8_t *buf, uint64_t count) {
    while (count > 0) {
        size_t want = count > IO_CHUNK ? IO_CHUNK : (size_t)count;
        ssize_t n = read(in_fd, buf, want);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "stream read error: %s\n", strerror(errno));
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "unexpected EOF while skipping stream\n");
            return -1;
        }
        count -= (uint64_t)n;
    }
    return 0;
}

static int copy_region_stream(int in_fd, uint8_t *buf, region_target *t) {
    uint64_t left = t->length;
    uint64_t done = 0;
    int last_pct = -1;
    while (left > 0) {
        size_t want = left > IO_CHUNK ? IO_CHUNK : (size_t)left;
        ssize_t n = read(in_fd, buf, want);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "read error for %s: %s\n", t->label, strerror(errno));
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "unexpected EOF while writing %s\n", t->label);
            return -1;
        }
        if (write_all(t->fd, buf, (size_t)n) != 0) {
            fprintf(stderr, "write error for %s (%s): %s\n", t->label, t->dst, strerror(errno));
            return -1;
        }
        sha256_update(&t->sha, buf, (size_t)n);
        left -= (uint64_t)n;
        done += (uint64_t)n;
        int pct = (int)((done * 100u) / t->length);
        if (pct / 10 != last_pct / 10 || pct == 100) {
            last_pct = pct;
            fprintf(stderr, "OTA_STREAM %s %d%% (%" PRIu64 "/%" PRIu64 ")\n",
                    t->label, pct, done, t->length);
        }
    }
    if (fsync(t->fd) != 0) {
        fprintf(stderr, "fsync error for %s: %s\n", t->label, strerror(errno));
        return -1;
    }
    t->done = true;
    return 0;
}

static int open_output_target(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISBLK(st.st_mode)) {
        return open(path, O_WRONLY | O_CLOEXEC);
    }
    return open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
}

static int cmd_extract_stream(int argc, char **argv) {
    if (argc != 8) {
        fprintf(stderr, "usage: spx-ota-tool extract-stream <super_dst|-> <super_off> <super_len> <esp_dst|-> <esp_off> <esp_len>\n");
        return 2;
    }
    region_target targets[2];
    memset(targets, 0, sizeof(targets));
    targets[0].label = "SUPER";
    targets[0].dst = argv[2];
    targets[1].label = "ESP";
    targets[1].dst = argv[5];

    if (parse_u64(argv[3], &targets[0].offset) != 0 ||
        parse_u64(argv[4], &targets[0].length) != 0 ||
        parse_u64(argv[6], &targets[1].offset) != 0 ||
        parse_u64(argv[7], &targets[1].length) != 0) {
        fprintf(stderr, "invalid numeric offset/length\n");
        return 2;
    }
    targets[0].active = strcmp(targets[0].dst, "-") != 0 && targets[0].length > 0;
    targets[1].active = strcmp(targets[1].dst, "-") != 0 && targets[1].length > 0;
    if (!targets[0].active && !targets[1].active) {
        fprintf(stderr, "at least one active target required\n");
        return 2;
    }
    if (targets[0].active && targets[1].active) {
        uint64_t a0 = targets[0].offset, a1 = a0 + targets[0].length;
        uint64_t b0 = targets[1].offset, b1 = b0 + targets[1].length;
        if (a1 < a0 || b1 < b0 || (a0 < b1 && b0 < a1)) {
            fprintf(stderr, "overlapping target regions\n");
            return 2;
        }
    }

    int order[2] = {0, 1};
    if (targets[1].active && (!targets[0].active || targets[1].offset < targets[0].offset)) {
        order[0] = 1;
        order[1] = 0;
    }

    targets[0].fd = -1;
    targets[1].fd = -1;
    for (int i = 0; i < 2; ++i) {
        if (!targets[i].active) continue;
        targets[i].fd = open_output_target(targets[i].dst);
        if (targets[i].fd < 0) {
            fprintf(stderr, "open %s: %s\n", targets[i].dst, strerror(errno));
            if (targets[0].fd >= 0) close(targets[0].fd);
            if (targets[1].fd >= 0) close(targets[1].fd);
            return 1;
        }
        sha256_init(&targets[i].sha);
    }

    uint8_t *buf = malloc(IO_CHUNK);
    if (!buf) {
        if (targets[0].fd >= 0) close(targets[0].fd);
        if (targets[1].fd >= 0) close(targets[1].fd);
        return 1;
    }

    uint64_t pos = 0;
    int rc = 0;
    for (int k = 0; k < 2; ++k) {
        region_target *t = &targets[order[k]];
        if (!t->active) continue;
        if (t->offset < pos) {
            fprintf(stderr, "non-monotonic stream offset\n");
            rc = 1;
            break;
        }
        if (t->offset > pos) {
            if (skip_bytes(STDIN_FILENO, buf, t->offset - pos) != 0) {
                rc = 1;
                break;
            }
            pos = t->offset;
        }
        if (copy_region_stream(STDIN_FILENO, buf, t) != 0) {
            rc = 1;
            break;
        }
        pos += t->length;
    }

    free(buf);
    for (int i = 0; i < 2; ++i) {
        if (targets[i].fd >= 0 && close(targets[i].fd) != 0) rc = 1;
    }
    if (rc != 0) return 1;

    for (int i = 0; i < 2; ++i) {
        if (!targets[i].active) continue;
        char hex[65];
        sha256_final_hex(&targets[i].sha, hex);
        printf("%s_SHA256=%s\n", targets[i].label, hex);
        printf("%s_BYTES=%" PRIu64 "\n", targets[i].label, targets[i].length);
    }
    return 0;
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void put_le64(uint8_t *p, uint64_t v) {
    put_le32(p, (uint32_t)(v & 0xffffffffu));
    put_le32(p + 4, (uint32_t)(v >> 32));
}

static int pwrite_all(int fd, const uint8_t *buf, size_t len, uint64_t offset) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = pwrite(fd, buf + done, len - done, (off_t)(offset + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static int zero_region(int fd, uint64_t offset, uint64_t len) {
    uint8_t *zeros = calloc(1, 1024 * 1024);
    if (!zeros) return -1;
    uint64_t done = 0;
    while (done < len) {
        size_t chunk = (len - done) > (1024u * 1024u) ? (1024u * 1024u) : (size_t)(len - done);
        if (pwrite_all(fd, zeros, chunk, offset + done) != 0) {
            free(zeros);
            return -1;
        }
        done += chunk;
    }
    free(zeros);
    return 0;
}

static int random_uuid_le(uint8_t out[16]) {
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open /dev/urandom: %s\n", strerror(errno));
        return -1;
    }
    ssize_t n = read_exact(fd, out, 16);
    close(fd);
    if (n != 16) {
        fprintf(stderr, "short read from /dev/urandom\n");
        return -1;
    }
    /* RFC 4122 version 4 UUID in GPT mixed-endian layout */
    out[7] = (uint8_t)((out[7] & 0x0fu) | 0x40u);
    out[8] = (uint8_t)((out[8] & 0x3fu) | 0x80u);
    return 0;
}

static uint64_t align_up_u64(uint64_t val, uint64_t align) {
    return ((val + align - 1u) / align) * align;
}

static void write_progress_atomic(const char *path, const char *stage, int pct, const char *detail) {
    if (!path || !*path || strcmp(path, "-") == 0) return;
    size_t plen = strlen(path);
    char *tmp = malloc(plen + 16);
    if (!tmp) return;
    snprintf(tmp, plen + 16, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        free(tmp);
        return;
    }
    (void)fchmod(fd, 0644);
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "stage=%s\npercent=%d\ndetail=%s\n",
                       stage, pct, detail ? detail : "");
    if (len > 0 && (size_t)len < sizeof(buf)) {
        (void)write_all(fd, (const uint8_t *)buf, (size_t)len);
    }
    close(fd);
    (void)rename(tmp, path);
    free(tmp);
}

static int cmd_write_gpt_5part(int argc, char **argv) {
    if (argc != 7 && argc != 8) {
        fprintf(stderr, "usage: spx-ota-tool write-gpt-5part <target> <super_bytes> <meta_bytes> <misc_bytes> <esp_bytes> [total_bytes]\n");
        return 2;
    }
    const char *target = argv[2];
    uint64_t super_bytes = 0, meta_bytes = 0, misc_bytes = 0, esp_bytes = 0, total_override = 0;
    if (parse_u64(argv[3], &super_bytes) != 0 ||
        parse_u64(argv[4], &meta_bytes) != 0 ||
        parse_u64(argv[5], &misc_bytes) != 0 ||
        parse_u64(argv[6], &esp_bytes) != 0) {
        fprintf(stderr, "invalid partition sizes\n");
        return 2;
    }
    if (argc == 8 && parse_u64(argv[7], &total_override) != 0) {
        fprintf(stderr, "invalid total_bytes\n");
        return 2;
    }

    const uint64_t MiB = 1048576u;
    const uint64_t GiB = 1073741824u;
    if (super_bytes == 0 || meta_bytes == 0 || misc_bytes == 0 || esp_bytes == 0 ||
        (super_bytes % MiB) != 0 || (meta_bytes % MiB) != 0 ||
        (misc_bytes % MiB) != 0 || (esp_bytes % MiB) != 0) {
        fprintf(stderr, "all fixed partition sizes must be non-zero and 1 MiB aligned\n");
        return 2;
    }
    if (esp_bytes < 768u * MiB) {
        fprintf(stderr, "SQ2PROBE ESP must be at least 768 MiB\n");
        return 2;
    }

    int fd = open(target, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", target, strerror(errno));
        return 1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        fprintf(stderr, "fstat %s: %s\n", target, strerror(errno));
        close(fd);
        return 1;
    }

    bool is_blk = S_ISBLK(st.st_mode);
    uint64_t actual_bytes = 0;
    if (is_blk) {
        int ssz = 0;
        if (ioctl(fd, BLKSSZGET, &ssz) != 0 || ssz != 512) {
            fprintf(stderr, "unsupported logical sector size (%d) on %s\n", ssz, target);
            close(fd);
            return 1;
        }
        if (ioctl(fd, BLKGETSIZE64, &actual_bytes) != 0) {
            fprintf(stderr, "BLKGETSIZE64 failed on %s: %s\n", target, strerror(errno));
            close(fd);
            return 1;
        }
    } else if (S_ISREG(st.st_mode)) {
        actual_bytes = (uint64_t)st.st_size;
    } else {
        fprintf(stderr, "unsupported target type: %s\n", target);
        close(fd);
        return 1;
    }

    uint64_t total_bytes = (total_override > 0) ? total_override : actual_bytes;
    if (total_bytes > actual_bytes || total_bytes < 2u * GiB || (total_bytes % 512u) != 0) {
        fprintf(stderr, "invalid target size %" PRIu64 " (actual %" PRIu64 ")\n", total_bytes, actual_bytes);
        close(fd);
        return 1;
    }

    uint64_t total_sectors = total_bytes / 512u;
    uint64_t last_usable_lba = total_sectors - 34u;
    uint64_t usable_end_bytes = (total_sectors - 33u) * 512u;

    typedef struct {
        const char *name;
        uint32_t index;
        uint64_t offset;
        uint64_t bytes;
        bool is_esp;
    } part_spec;

    part_spec parts[5];
    uint64_t cursor = MiB;
    parts[0] = (part_spec){"super", 1, cursor, super_bytes, false};
    cursor = align_up_u64(cursor + super_bytes, MiB);
    parts[1] = (part_spec){"metadata", 2, cursor, meta_bytes, false};
    cursor = align_up_u64(cursor + meta_bytes, MiB);
    parts[2] = (part_spec){"misc", 3, cursor, misc_bytes, false};
    cursor = align_up_u64(cursor + misc_bytes, MiB);
    parts[3] = (part_spec){"SQ2PROBE", 4, cursor, esp_bytes, true};
    cursor = align_up_u64(cursor + esp_bytes, MiB);

    if (cursor >= usable_end_bytes) {
        fprintf(stderr, "target disk too small for fixed partitions\n");
        close(fd);
        return 1;
    }
    uint64_t data_bytes = ((usable_end_bytes - cursor) / 4096u) * 4096u;
    if (data_bytes < GiB) {
        fprintf(stderr, "target disk too small for fresh userdata (%" PRIu64 " bytes < 1 GiB)\n", data_bytes);
        close(fd);
        return 1;
    }
    parts[4] = (part_spec){"userdata", 5, cursor, data_bytes, false};

    /* GUIDs in GPT little-endian layout (matches disk_image.py) */
    static const uint8_t esp_guid[16] = {
        0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
        0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
    };
    static const uint8_t linux_guid[16] = {
        0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
        0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4
    };

    const uint32_t entry_count = 128u;
    const uint32_t entry_size = 128u;
    uint8_t *table = calloc(entry_count, entry_size);
    if (!table) {
        close(fd);
        return 1;
    }

    for (int i = 0; i < 5; ++i) {
        uint8_t *e = table + (size_t)(parts[i].index - 1u) * entry_size;
        memcpy(e, parts[i].is_esp ? esp_guid : linux_guid, 16);
        if (random_uuid_le(e + 16) != 0) {
            free(table);
            close(fd);
            return 1;
        }
        uint64_t first_lba = parts[i].offset / 512u;
        uint64_t last_lba = (parts[i].offset + parts[i].bytes) / 512u - 1u;
        put_le64(e + 32, first_lba);
        put_le64(e + 40, last_lba);
        size_t nlen = strlen(parts[i].name);
        for (size_t k = 0; k < nlen && k < 36; ++k) {
            e[56 + k * 2] = (uint8_t)parts[i].name[k];
            e[56 + k * 2 + 1] = 0;
        }
    }

    uint8_t disk_guid[16];
    if (random_uuid_le(disk_guid) != 0) {
        free(table);
        close(fd);
        return 1;
    }
    uint32_t table_crc = crc32_calc(table, (size_t)entry_count * entry_size);

    uint8_t hdr_primary[512], hdr_backup[512];
    memset(hdr_primary, 0, sizeof(hdr_primary));
    memcpy(hdr_primary, "EFI PART", 8);
    put_le32(hdr_primary + 8, 0x00010000u);
    put_le32(hdr_primary + 12, 92u);
    put_le64(hdr_primary + 24, 1u);
    put_le64(hdr_primary + 32, total_sectors - 1u);
    put_le64(hdr_primary + 40, 34u);
    put_le64(hdr_primary + 48, last_usable_lba);
    memcpy(hdr_primary + 56, disk_guid, 16);
    put_le64(hdr_primary + 72, 2u);
    put_le32(hdr_primary + 80, entry_count);
    put_le32(hdr_primary + 84, entry_size);
    put_le32(hdr_primary + 88, table_crc);
    put_le32(hdr_primary + 16, crc32_calc(hdr_primary, 92u));

    memset(hdr_backup, 0, sizeof(hdr_backup));
    memcpy(hdr_backup, "EFI PART", 8);
    put_le32(hdr_backup + 8, 0x00010000u);
    put_le32(hdr_backup + 12, 92u);
    put_le64(hdr_backup + 24, total_sectors - 1u);
    put_le64(hdr_backup + 32, 1u);
    put_le64(hdr_backup + 40, 34u);
    put_le64(hdr_backup + 48, last_usable_lba);
    memcpy(hdr_backup + 56, disk_guid, 16);
    put_le64(hdr_backup + 72, total_sectors - 33u);
    put_le32(hdr_backup + 80, entry_count);
    put_le32(hdr_backup + 84, entry_size);
    put_le32(hdr_backup + 88, table_crc);
    put_le32(hdr_backup + 16, crc32_calc(hdr_backup, 92u));

    uint8_t mbr[512];
    memset(mbr, 0, sizeof(mbr));
    mbr[446 + 1] = 0x00u;
    mbr[446 + 2] = 0x02u;
    mbr[446 + 3] = 0x00u;
    mbr[446 + 4] = 0xeeu;
    mbr[446 + 5] = 0xffu;
    mbr[446 + 6] = 0xffu;
    mbr[446 + 7] = 0xffu;
    put_le32(mbr + 446 + 8, 1u);
    uint64_t mbr_secs = total_sectors - 1u;
    put_le32(mbr + 446 + 12, mbr_secs > 0xffffffffu ? 0xffffffffu : (uint32_t)mbr_secs);
    mbr[510] = 0x55u;
    mbr[511] = 0xaau;

    int rc = 0;
    /* Wipe stale signatures in metadata, misc (full), SQ2PROBE, and userdata heads */
    if (zero_region(fd, parts[1].offset, parts[1].bytes < MiB ? parts[1].bytes : MiB) != 0 ||
        zero_region(fd, parts[2].offset, parts[2].bytes) != 0 ||
        zero_region(fd, parts[3].offset, parts[3].bytes < MiB ? parts[3].bytes : MiB) != 0 ||
        zero_region(fd, parts[4].offset, parts[4].bytes < MiB ? parts[4].bytes : MiB) != 0) {
        fprintf(stderr, "failed to zero fresh partition headers on %s: %s\n", target, strerror(errno));
        rc = 1;
    }

    if (rc == 0) {
        size_t tbytes = (size_t)entry_count * entry_size;
        if (pwrite_all(fd, mbr, sizeof(mbr), 0) != 0 ||
            pwrite_all(fd, hdr_primary, sizeof(hdr_primary), 512u) != 0 ||
            pwrite_all(fd, table, tbytes, 1024u) != 0 ||
            pwrite_all(fd, table, tbytes, (total_sectors - 33u) * 512u) != 0 ||
            pwrite_all(fd, hdr_backup, sizeof(hdr_backup), (total_sectors - 1u) * 512u) != 0) {
            fprintf(stderr, "failed writing GPT to %s: %s\n", target, strerror(errno));
            rc = 1;
        }
    }

    free(table);
    if (rc == 0 && fsync(fd) != 0) {
        fprintf(stderr, "fsync %s: %s\n", target, strerror(errno));
        rc = 1;
    }
    if (rc == 0 && is_blk) {
        (void)ioctl(fd, BLKRRPART, 0);
    }
    if (close(fd) != 0) rc = 1;
    if (rc != 0) return 1;

    for (int i = 0; i < 5; ++i) {
        uint64_t first_lba = parts[i].offset / 512u;
        uint64_t last_lba = (parts[i].offset + parts[i].bytes) / 512u - 1u;
        printf("%s\t%u\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n",
               parts[i].name, parts[i].index, parts[i].offset, parts[i].bytes, first_lba, last_lba);
    }
    return 0;
}

static int cmd_copy_verify(int argc, char **argv) {
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "usage: spx-ota-tool copy-verify <src> <dst> <bytes> [progress_file]\n");
        return 2;
    }
    const char *src_path = argv[2];
    const char *dst_path = argv[3];
    uint64_t total_bytes = 0;
    if (parse_u64(argv[4], &total_bytes) != 0 || total_bytes == 0) {
        fprintf(stderr, "invalid byte length\n");
        return 2;
    }
    const char *prog_path = (argc == 6) ? argv[5] : NULL;

    int src_fd = open(src_path, O_RDONLY | O_CLOEXEC);
    if (src_fd < 0) {
        fprintf(stderr, "open %s: %s\n", src_path, strerror(errno));
        return 1;
    }
    struct stat dst_st;
    int dst_fd;
    bool dst_is_blk = false;
    if (stat(dst_path, &dst_st) == 0) {
        dst_is_blk = S_ISBLK(dst_st.st_mode);
        dst_fd = open(dst_path, O_RDWR | O_CLOEXEC);
    } else {
        dst_fd = open(dst_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    }
    if (dst_fd < 0) {
        fprintf(stderr, "open %s: %s\n", dst_path, strerror(errno));
        close(src_fd);
        return 1;
    }

    uint8_t *buf = malloc(IO_CHUNK);
    if (!buf) {
        close(src_fd);
        close(dst_fd);
        return 1;
    }

    sha256_ctx src_sha;
    sha256_init(&src_sha);
    uint64_t done = 0;
    int last_pct = -1;
    uint64_t total_mb = (total_bytes + 1048575u) / 1048576u;

    while (done < total_bytes) {
        size_t want = (total_bytes - done) > IO_CHUNK ? IO_CHUNK : (size_t)(total_bytes - done);
        ssize_t n = read_exact(src_fd, buf, want);
        if (n != (ssize_t)want) {
            fprintf(stderr, "short read on %s at %" PRIu64 ": %s\n", src_path, done,
                    n < 0 ? strerror(errno) : "EOF");
            free(buf);
            close(src_fd);
            close(dst_fd);
            return 1;
        }
        if (write_all(dst_fd, buf, (size_t)n) != 0) {
            fprintf(stderr, "write error on %s at %" PRIu64 ": %s\n", dst_path, done, strerror(errno));
            free(buf);
            close(src_fd);
            close(dst_fd);
            return 1;
        }
        sha256_update(&src_sha, buf, (size_t)n);
        done += (uint64_t)n;
        int pct = 15 + (int)((done * 49u) / total_bytes);
        if (pct != last_pct) {
            last_pct = pct;
            char detail[64];
            snprintf(detail, sizeof(detail), "%" PRIu64 "/%" PRIu64 " MiB",
                     done / 1048576u, total_mb);
            write_progress_atomic(prog_path, "copying_super", pct, detail);
        }
    }
    close(src_fd);

    if (fsync(dst_fd) != 0) {
        fprintf(stderr, "fsync error on %s: %s\n", dst_path, strerror(errno));
        free(buf);
        close(dst_fd);
        return 1;
    }

    /* Drop caches so readback verifies actual persisted blocks */
    if (dst_is_blk) {
        (void)ioctl(dst_fd, BLKFLSBUF, 0);
    }
    (void)posix_fadvise(dst_fd, 0, (off_t)total_bytes, POSIX_FADV_DONTNEED);
    if (lseek(dst_fd, 0, SEEK_SET) != 0) {
        fprintf(stderr, "lseek error on %s: %s\n", dst_path, strerror(errno));
        free(buf);
        close(dst_fd);
        return 1;
    }

    sha256_ctx dst_sha;
    sha256_init(&dst_sha);
    done = 0;
    last_pct = -1;
    while (done < total_bytes) {
        size_t want = (total_bytes - done) > IO_CHUNK ? IO_CHUNK : (size_t)(total_bytes - done);
        ssize_t n = read_exact(dst_fd, buf, want);
        if (n != (ssize_t)want) {
            fprintf(stderr, "short readback on %s at %" PRIu64 ": %s\n", dst_path, done,
                    n < 0 ? strerror(errno) : "EOF");
            free(buf);
            close(dst_fd);
            return 1;
        }
        sha256_update(&dst_sha, buf, (size_t)n);
        done += (uint64_t)n;
        int pct = 65 + (int)((done * 17u) / total_bytes);
        if (pct != last_pct) {
            last_pct = pct;
            char detail[64];
            snprintf(detail, sizeof(detail), "%" PRIu64 "/%" PRIu64 " MiB",
                     done / 1048576u, total_mb);
            write_progress_atomic(prog_path, "verifying_super", pct, detail);
        }
    }

    free(buf);
    if (close(dst_fd) != 0) {
        fprintf(stderr, "close error on %s: %s\n", dst_path, strerror(errno));
        return 1;
    }

    char src_hex[65], dst_hex[65];
    sha256_final_hex(&src_sha, src_hex);
    sha256_final_hex(&dst_sha, dst_hex);
    if (strcmp(src_hex, dst_hex) != 0) {
        fprintf(stderr, "SUPER_VERIFY_MISMATCH src=%s dst=%s\n", src_hex, dst_hex);
        return 1;
    }

    printf("SUPER_SHA256=%s\n", dst_hex);
    printf("SUPER_BYTES=%" PRIu64 "\n", total_bytes);
    printf("SUPER_VERIFIED=1\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: spx-ota-tool <sha256|detect|inspect-gpt|extract-stream|write-gpt-5part|copy-verify> ...\n");
        return 2;
    }
    if (strcmp(argv[1], "sha256") == 0 && argc == 3) {
        return cmd_sha256(argv[2]);
    }
    if (strcmp(argv[1], "detect") == 0 && argc == 3) {
        return cmd_detect(argv[2]);
    }
    if (strcmp(argv[1], "inspect-gpt") == 0 && argc == 3) {
        return cmd_inspect_gpt(argv[2]);
    }
    if (strcmp(argv[1], "extract-stream") == 0) {
        return cmd_extract_stream(argc, argv);
    }
    if (strcmp(argv[1], "write-gpt-5part") == 0) {
        return cmd_write_gpt_5part(argc, argv);
    }
    if (strcmp(argv[1], "copy-verify") == 0) {
        return cmd_copy_verify(argc, argv);
    }
    fprintf(stderr, "unknown command or bad arguments: %s\n", argv[1]);
    return 2;
}
