/* SPDX-License-Identifier: MIT
 * Zero-dependency OTA helper for Surface Pro X Googlebook OS:
 *   - sha256 <path-or-minus>
 *   - detect <path-or-minus>
 *   - inspect-gpt <path-or-minus>
 *   - extract-stream <super_dst|-> <super_off> <super_len> <esp_dst|-> <esp_off> <esp_len>
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: spx-ota-tool <sha256|detect|inspect-gpt|extract-stream> ...\n");
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
    fprintf(stderr, "unknown command or bad arguments: %s\n", argv[1]);
    return 2;
}
