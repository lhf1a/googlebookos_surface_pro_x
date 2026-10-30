/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_EDP_RATES_H
#define SQ2_EDP_RATES_H
/* u8/u32 supplied by caller; also compiled by the host-side boundary tests. */
static inline int sq2_edp_rate_index(const u32 rates[8], u32 rate)
{
	int i;
	for (i = 0; i < 8; i++)
		if (rates[i] && rates[i] == rate)
			return i;
	return -1;
}

static inline u32 sq2_edp_lower_rate(const u32 rates[8], u32 ceiling)
{
	u32 best = 0;
	int i;
	for (i = 0; i < 8; i++)
		if (rates[i] < ceiling && rates[i] > best)
			best = rates[i];
	return best;
}

static inline u32 sq2_edp_parse_rates(const u8 raw[16], u32 max_rate,
				    u32 rates[8])
{
	u32 previous = 0, best = 0, rate;
	int i;
	for (i = 0; i < 8; i++)
		rates[i] = 0;
	for (i = 0; i < 8; i++) {
		rate = (raw[2*i] | ((u32)raw[2*i+1] << 8)) * 20;
		if (!rate)
			break;
		if (rate <= previous)
			goto invalid;
		previous = rate;
		/* Only rates already implemented by this controller/PHY. Preserve
		 * the receiver's original table index, including filtered slots. */
		if (rate > max_rate || (rate != 162000 && rate != 270000 &&
				       rate != 540000 && rate != 810000))
			continue;
		rates[i] = rate;
		best = rate;
	}
	return best;
invalid:
	for (i = 0; i < 8; i++)
		rates[i] = 0;
	return 0;
}
#endif
