#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8;
typedef uint32_t u32;
#include "../probe/graphics/sq2_edp_rates.h"
int main(void)
{
 u8 raw[16] = {0xa4,0x1f,0x30,0x2a,0x76,0x2f,0xbc,0x34,
               0x48,0x3f,0x60,0x54,0x78,0x69,0,0};
 u32 rates[8];
 assert(sq2_edp_parse_rates(raw, 540000, rates) == 540000);
 assert(sq2_edp_rate_index(rates, 540000) == 6);
 assert(sq2_edp_rate_index(rates, 270000) == 3);
 assert(sq2_edp_rate_index(rates, 162000) == 0);
 assert(sq2_edp_rate_index(rates, 432000) == -1);
 assert(sq2_edp_rate_index(rates, 0) == -1);
 assert(sq2_edp_lower_rate(rates, 540000) == 270000);
 assert(sq2_edp_lower_rate(rates, 270000) == 162000);
 assert(sq2_edp_lower_rate(rates, 162000) == 0);
 assert(sq2_edp_parse_rates(raw, 270000, rates) == 270000);
 assert(sq2_edp_rate_index(rates, 540000) == -1);
 assert(sq2_edp_parse_rates(raw, 100000, rates) == 0);
 raw[2] = raw[0]; raw[3] = raw[1];
 assert(sq2_edp_parse_rates(raw, 540000, rates) == 0);
 assert(sq2_edp_lower_rate(rates, ~0U) == 0);
 memset(raw, 0, sizeof(raw));
 assert(sq2_edp_parse_rates(raw, 540000, rates) == 0);
 memset(raw, 0xff, sizeof(raw));
 assert(sq2_edp_parse_rates(raw, 540000, rates) == 0);
 puts("PASS: exact receiver indices, PHY intersection, limits, downshift, malformed/empty tables");
}
