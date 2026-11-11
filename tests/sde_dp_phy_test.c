/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual parser/catalog/caller bodies. AUX, callbacks and MMIO are models. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define BIT(n) (1U << (n))
#include "actual-definitions.h"
static int scenarios;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL scenario %d line %d: %s\n", scenarios, __LINE__, #c); exit(1); } } while (0)
struct drm_dp_aux { int unused; };
struct dp_aux { struct drm_dp_aux *drm_aux; };
struct dp_link_phy_params { u32 phy_test_pattern_sel; u8 v_level, p_level; };
struct dp_link { struct dp_link_phy_params phy_params; void (*send_test_response)(struct dp_link *); };
struct dp_link_private { struct dp_aux *aux; struct dp_link dp_link; };
struct dp_io_data { int unused; };
struct dp_catalog_ctrl {
    void (*send_phy_pattern)(struct dp_catalog_ctrl *, u32);
    u32 (*read_phy_pattern)(struct dp_catalog_ctrl *);
};
struct dp_catalog_private {
    struct { struct dp_io_data *dp_link; } io;
    u32 (*read)(struct dp_catalog_private *, struct dp_io_data *, u32);
    void (*write)(struct dp_catalog_private *, struct dp_io_data *, u32, u32);
};
struct dp_ctrl_private { struct dp_link *link; struct dp_catalog_ctrl *catalog; };
static struct dp_io_data modeled_io;
static struct dp_catalog_private catalog_private;
static int aux_result, aux_reads, debug_success;
static u8 aux_byte;
static u32 mainlink, report_pattern;
static char callbacks[8];
static size_t callback_count;
struct transaction { char op; u32 reg, value; };
static struct transaction trace[24];
static size_t trace_count;
static FILE *semantic;
static void append(char op, u32 reg, u32 value)
{
    CHECK(trace_count < sizeof(trace) / sizeof(trace[0]));
    trace[trace_count++] = (struct transaction){op, reg, value};
}
static void callback(char op)
{
    CHECK(callback_count < sizeof(callbacks)); callbacks[callback_count++] = op;
}
static int drm_dp_dpcd_read(struct drm_dp_aux *aux, unsigned int address, void *data, size_t size)
{
    CHECK(aux && address == 0x248 && size == 1); aux_reads++;
    if (aux_result == 1) *(u8 *)data = aux_byte;
    return aux_result;
}
static void debug(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (!strcmp(fmt, "%s: %s\n")) {
        const char *state = va_arg(args, const char *);
        (void)va_arg(args, const char *);
        CHECK(!strcmp(state, "success") || !strcmp(state, "failed"));
        debug_success = !strcmp(state, "success");
    }
    va_end(args);
}
#define DP_DEBUG(...) debug(__VA_ARGS__)
#define DP_ERR(...) debug(__VA_ARGS__)
#define dp_catalog_get_priv(x) ((void)(x), &catalog_private)
#define dp_read(x) catalog->read(catalog, io_data, (x))
#define dp_write(x, y) catalog->write(catalog, io_data, (x), (y))
static void wmb(void) { append('B', 0, 0); }
static void dp_ctrl_update_hw_vx_px(struct dp_ctrl_private *ctrl) { CHECK(ctrl); callback('H'); }
static void dp_ctrl_update_sink_vx_px(struct dp_ctrl_private *ctrl) { CHECK(ctrl); callback('K'); }
#include "actual-functions.h"
static u32 mmio_read(struct dp_catalog_private *catalog, struct dp_io_data *io, u32 reg)
{
    CHECK(catalog == &catalog_private && io == &modeled_io && reg == 0);
    append('R', reg, mainlink); return mainlink;
}
static void mmio_write(struct dp_catalog_private *catalog, struct dp_io_data *io, u32 reg, u32 value)
{
    CHECK(catalog == &catalog_private && io == &modeled_io);
    append('W', reg, value); if (reg == 0) mainlink = value;
}
static void send_pattern(struct dp_catalog_ctrl *ctrl, u32 pattern)
{
    callback('S'); dp_catalog_ctrl_send_phy_pattern(ctrl, pattern);
}
static u32 read_pattern(struct dp_catalog_ctrl *ctrl) { CHECK(ctrl); callback('R'); return report_pattern; }
static void send_response(struct dp_link *link) { CHECK(link); callback('T'); }
static void reset(void)
{
    scenarios++; trace_count = callback_count = aux_reads = 0;
    memset(trace, 0, sizeof(trace)); memset(callbacks, 0, sizeof(callbacks));
    mainlink = 0xbe; aux_result = 1; debug_success = -1;
}
static bool supported(unsigned int p) { return p <= 5 || p == 7; }
static void expect_trace(unsigned int pattern)
{
    struct transaction expected[24]; size_t count = 0, i;
#define E(o, r, v) do { expected[count++] = (struct transaction){o, r, v}; } while (0)
    E('W', 4, 0);
    switch (pattern) {
    case 1: E('W', 4, 1); break;
    case 2: E('W', 0x54, 0); E('W', 0x54, 0xfc); E('W', 0x44, 2); E('W', 4, 0x10); break;
    case 3: E('W', 4, 0x20); break;
    case 4: E('W', 4, 0x40); E('W', 0xc0, 0x3e0f83e0); E('W', 0xc4, 0x0f83e0f8); E('W', 0xc8, 0xf83e); break;
    case 5:
        E('R', 0, 0xbe); E('W', 0, 0xae); E('W', 0x54, 0x10000); E('W', 0x54, 0x100fc);
        E('W', 0x44, 2); E('W', 4, 0x10); E('R', 0, 0xae); E('W', 0, 0xaf); break;
    case 7: E('W', 0, 1); E('W', 4, 8); break;
    default: break;
    }
    if (pattern >= 1 && pattern <= 7 && pattern != 6) E('B', 0, 0);
    CHECK(trace_count == count);
    for (i = 0; i < count; i++) {
        CHECK(trace[i].op == expected[i].op && trace[i].reg == expected[i].reg && trace[i].value == expected[i].value);
        fprintf(semantic, "%c:%x:%x,", trace[i].op, trace[i].reg, trace[i].value);
    }
#undef E
}
int main(int argc, char **argv)
{
    unsigned int p, s;
    struct drm_dp_aux aux = {0}; struct dp_aux daux = {&aux};
    struct dp_link_private link = {.aux = &daux};
    struct dp_catalog_ctrl catalog = {.send_phy_pattern = send_pattern, .read_phy_pattern = read_pattern};
    struct dp_ctrl_private ctrl = {.link = &link.dp_link, .catalog = &catalog};
    const u32 readbacks[] = {8, 0x80, 0x100, 0x200, 0x40, 0};
#ifdef DONOR_REFERENCE
    const char *names[] = {"DP_TEST_PHY_PATTERN_NONE", "DP_TEST_PHY_PATTERN_D10_2_NO_SCRAMBLING",
        "DP_TEST_PHY_PATTERN_SYMBOL_ERR_MEASUREMENT_CNT", "DP_TEST_PHY_PATTERN_PRBS7",
        "DP_TEST_PHY_PATTERN_80_BIT_CUSTOM_PATTERN", "DP_TEST_PHY_PATTERN_CP2520_PATTERN_1",
        "DP_TEST_PHY_PATTERN_CP2520_PATTERN_2", "DP_TEST_PHY_PATTERN_CP2520_PATTERN_3"};
#else
    const char *names[] = {"DP_PHY_TEST_PATTERN_NONE", "DP_PHY_TEST_PATTERN_D10_2", "DP_PHY_TEST_PATTERN_ERROR_COUNT",
        "DP_PHY_TEST_PATTERN_PRBS7", "DP_PHY_TEST_PATTERN_80BIT_CUSTOM", "DP_PHY_TEST_PATTERN_CP2520",
        "SDE_DP_PHY_TEST_PATTERN_CP2520_PATTERN_2", "SDE_DP_PHY_TEST_PATTERN_CP2520_PATTERN_3"};
    CHECK(SDE_DP_PHY_TEST_PATTERN_CP2520_PATTERN_2 == 6 && SDE_DP_PHY_TEST_PATTERN_CP2520_PATTERN_3 == 7);
#endif
    CHECK(argc == 2); semantic = fopen(argv[1], "w"); CHECK(semantic);
    catalog_private.io.dp_link = &modeled_io; catalog_private.read = mmio_read; catalog_private.write = mmio_write;
    link.dp_link.send_test_response = send_response;
    for (p = 0; p <= 255; p++) {
        reset(); CHECK(dp_link_is_phy_test_pattern_supported(p) == supported(p));
        CHECK(!strcmp(dp_link_get_phy_test_pattern(p), p < 8 ? names[p] : "unknown"));
        fprintf(semantic, "support:%u:%u:name-class:%u\n", p, supported(p), p < 8 ? p : 255);
    }
    for (p = 0; p <= 255; p++) {
        reset(); aux_byte = p; link.dp_link.phy_params.phy_test_pattern_sel = 0xdead;
        CHECK(dp_link_parse_phy_test_params(&link) == (supported(p) ? 0 : -EINVAL));
        CHECK(aux_reads == 1 && link.dp_link.phy_params.phy_test_pattern_sel == p && !trace_count);
        fprintf(semantic, "parse:%u:%d:%u\n", p, supported(p) ? 0 : -EINVAL, link.dp_link.phy_params.phy_test_pattern_sel);
    }
    const int errors[] = {-ETIMEDOUT, -EIO, 0};
    for (p = 0; p < sizeof(errors) / sizeof(errors[0]); p++) {
        reset(); aux_result = errors[p]; link.dp_link.phy_params.phy_test_pattern_sel = 0xdead;
        CHECK(dp_link_parse_phy_test_params(&link) == -EINVAL && aux_reads == 1);
        CHECK(link.dp_link.phy_params.phy_test_pattern_sel == 0xdead && !trace_count);
        fprintf(semantic, "parse-error:%d:-22:57005\n", errors[p]);
    }
    for (p = 0; p <= 255; p++) {
        reset(); dp_catalog_ctrl_send_phy_pattern(&catalog, p);
        fprintf(semantic, "catalog:%u:", p); expect_trace(p); fputc('\n', semantic);
    }
    reset(); dp_catalog_ctrl_send_phy_pattern(NULL, 7); CHECK(!trace_count); fputs("null-catalog:no-write\n", semantic);
    for (p = 0; p <= 255; p++) for (s = 0; s < sizeof(readbacks) / sizeof(readbacks[0]); s++) {
        bool success = (readbacks[s] == 8 && p == 1) || (readbacks[s] == 0x80 && (p == 2 || p == 5)) ||
                       (readbacks[s] == 0x100 && p == 3) || (readbacks[s] == 0x200 && p == 4) || (readbacks[s] == 0x40 && p == 7);
        reset(); report_pattern = readbacks[s]; link.dp_link.phy_params.phy_test_pattern_sel = p;
        dp_ctrl_send_phy_test_pattern(&ctrl);
        CHECK(callback_count == 5 && !memcmp(callbacks, "HSKTR", 5)); CHECK(debug_success == success);
        fprintf(semantic, "caller:%u:%x:%u:HSKTR:", p, readbacks[s], success); expect_trace(p); fputc('\n', semantic);
    }
    CHECK(fclose(semantic) == 0);
    printf("PASS %d actual-function PHY scenarios; AUX/MMIO/callbacks modeled, no hardware proof\n", scenarios);
    return 0;
}
