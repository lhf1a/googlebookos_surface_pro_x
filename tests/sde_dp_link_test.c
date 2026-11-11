/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual production functions; AUX transport and sleep are explicit models. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
#define BIT(n) (1UL << (n))
#include "actual-definitions.h"
struct drm_dp_aux { int unused; };
static int read_result, write_result, reads, writes, sleeps, scenarios;
static u8 input, output[2];
static unsigned int address;
static size_t length;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL case %d line %d: %s\n", scenarios, __LINE__, #c); exit(1); } } while (0)
static void reset(void)
{
	read_result = 1; write_result = 1; reads = writes = sleeps = 0;
	input = 0xb7; memset(output, 0, sizeof(output)); address = 0; length = 0;
	scenarios++;
}
static int drm_dp_dpcd_readb(struct drm_dp_aux *aux, unsigned int addr, u8 *value)
{
	CHECK(aux && addr == DP_SET_POWER); reads++;
	/* No supplied byte on a short/error read, as with an actual transport. */
	if (read_result == 1) *value = input;
	return read_result;
}
static int drm_dp_dpcd_write(struct drm_dp_aux *aux, unsigned int addr, const void *value, size_t count)
{
	CHECK(aux && count <= sizeof(output)); writes++; address = addr; length = count;
	memcpy(output, value, count); return write_result;
}
static int drm_dp_dpcd_writeb(struct drm_dp_aux *aux, unsigned int addr, u8 value)
{
	return drm_dp_dpcd_write(aux, addr, &value, 1);
}
static void usleep_range(unsigned long min, unsigned long max)
{
	CHECK(min == 1000 && max == 2000 && writes == 1 && write_result == 1); sleeps++;
}
#include "actual-functions.h"
int main(void)
{
	struct drm_dp_aux aux = {0};
	struct dp_link_info link = {.revision = 0x14, .rate = 540000, .num_lanes = 4};
	const unsigned int rates[] = {162000, 270000, 540000, 810000};
	const unsigned int codes[] = {6, 10, 20, 30};
	const unsigned int lanes[] = {1, 2, 4};
	const int errors[] = {-ETIMEDOUT, -EIO, 0, 1, 3};
	unsigned int r, l, e, j;
	int up, ret;
	for (r = 0; r < 4; r++) for (l = 0; l < 3; l++) for (e = 0; e < 2; e++) {
		reset(); write_result = 2; link.rate = rates[r]; link.num_lanes = lanes[l]; link.capabilities = e;
		CHECK(sde_dp_link_configure(&aux, &link) == 0);
		CHECK(writes == 1 && !reads && !sleeps && address == 0x100 && length == 2);
		CHECK(output[0] == codes[r] && output[1] == (lanes[l] | (e ? 0x80 : 0)));
	}
	for (j = 0; j < sizeof(errors)/sizeof(errors[0]); j++) {
		reset(); write_result = errors[j]; ret = sde_dp_link_configure(&aux, &link);
		CHECK(ret == (errors[j] < 0 ? errors[j] : -EIO)); CHECK(writes == 1 && !sleeps);
	}
	const unsigned int invalid_rates[] = {0, 161999, 162001, 216000, 324000, 432000, 675000, 1000000, 1350000, 2000000, UINT32_MAX};
	for (j = 0; j < sizeof(invalid_rates)/sizeof(invalid_rates[0]); j++) {
		reset(); link.rate = invalid_rates[j]; CHECK(sde_dp_link_configure(&aux, &link) == -EOPNOTSUPP); CHECK(!writes && !reads);
	}
	link.rate = 540000;
	const unsigned int invalid_lanes[] = {0, 3, 5, 8, UINT32_MAX};
	for (j = 0; j < sizeof(invalid_lanes)/sizeof(invalid_lanes[0]); j++) {
		reset(); link.num_lanes = invalid_lanes[j]; CHECK(sde_dp_link_configure(&aux, &link) == -EINVAL); CHECK(!writes && !reads);
	}
	link.num_lanes = 4; reset(); link.capabilities = 2;
	CHECK(sde_dp_link_configure(&aux, &link) == -EOPNOTSUPP && !writes); link.capabilities = 0;
	reset(); CHECK(sde_dp_link_configure(NULL, &link) == -EINVAL && !writes);
	reset(); CHECK(sde_dp_link_configure(&aux, NULL) == -EINVAL && !writes);
	for (up = 0; up < 2; up++) {
		int (*power)(struct drm_dp_aux *, const struct dp_link_info *) = up ? sde_dp_link_power_up : sde_dp_link_power_down;
		for (r = 0x10; r <= 0x14; r++) {
			reset(); link.revision = r; CHECK(power(&aux, &link) == 0);
			if (r == 0x10) { CHECK(!reads && !writes && !sleeps); continue; }
			CHECK(reads == 1 && writes == 1 && address == 0x600 && length == 1);
			CHECK(output[0] == (u8)((input & ~3) | (up ? 1 : 2)) && sleeps == up);
		}
		link.revision = 0x14;
		const int bad[] = {-ETIMEDOUT, -EIO, 0, 2};
		for (j = 0; j < sizeof(bad)/sizeof(bad[0]); j++) {
			reset(); read_result = bad[j]; CHECK(power(&aux, &link) == (bad[j] < 0 ? bad[j] : -EIO));
			CHECK(reads == 1 && !writes && !sleeps);
			reset(); write_result = bad[j]; CHECK(power(&aux, &link) == (bad[j] < 0 ? bad[j] : -EIO));
			CHECK(reads == 1 && writes == 1 && !sleeps);
		}
		reset(); link.revision = 0; CHECK(power(&aux, &link) == -EINVAL && !reads && !writes); link.revision = 0x14;
		reset(); CHECK(power(NULL, &link) == -EINVAL && !reads && !writes);
		reset(); CHECK(power(&aux, NULL) == -EINVAL && !reads && !writes);
	}
	printf("PASS %d actual-function AUX protocol scenarios; transport/sleep modeled, no hardware proof\n", scenarios);
	return 0;
}
