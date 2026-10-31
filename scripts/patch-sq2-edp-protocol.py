#!/usr/bin/env python3
"""Isolated V6 experiment: preserve the sink's existing rate-selection protocol.

2026-10-30 SQ2 test failed: legacy BW_SET=0x14 at 5.4/60 also blackscreens,
with DPCD timeout near 54 s. Retained only to reproduce the rejected hypothesis.
Not a default build patch or a hardware-validated fix. Based on the protocol
selection in https://lkml.iu.edu/hypermail/linux/kernel/2512.2/03295.html .
No link-rate cap, PHY, timing, voltage or device identity changes.
"""
import hashlib
from pathlib import Path
import sys

EXPECTED = {
    'dp_panel.c': '7d87f7945c757b7f3e3eadbd86055a91765ebe4ab861d071bd49793dd691c706',
    'dp_panel.h': '7e3ff618fe44896b77fb9c8f43d4785f72578b12657c9eb0ddaa1a889fb6006b',
    'dp_ctrl.c': '59b44cbf192e1f37da088a03dba473baaf7f6affe061224fd30a33a55d31134a',
}


def patch(stage):
    dp = Path(stage) / 'drivers/gpu/drm/msm/dp'
    texts = {}
    for name, expected in EXPECTED.items():
        data = (dp / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('Expected pristine V6 input: ' + name)
        texts[name] = data.decode()

    def replace(name, old, new):
        if texts[name].count(old) != 1:
            raise ValueError('Nonunique anchor: ' + name)
        texts[name] = texts[name].replace(old, new)

    replace('dp_panel.h', '\tu32 sq2_edp_rates[8];',
            '\tu32 sq2_edp_rates[8];\n\tbool sq2_edp_use_rate_set;')
    replace('dp_panel.c', '\tu8 rev, raw[16];', '\tu8 rev, raw[16], bw_set;')
    replace('dp_panel.c', '\tmemset(p->sq2_edp_rates, 0, sizeof(p->sq2_edp_rates));',
            '\tp->sq2_edp_use_rate_set = false;\n\tmemset(p->sq2_edp_rates, 0, sizeof(p->sq2_edp_rates));')
    replace('dp_panel.c', '\tlimit = min(p->max_dp_link_rate,',
            '\t/* SQ2_EDP_PROTOCOL_V1: preserve firmware-selected legacy mode. */\n'
            '\tif (drm_dp_dpcd_read(priv->aux, DP_LINK_BW_SET, &bw_set, 1) != 1)\n'
            '\t\treturn;\n\tlimit = min(p->max_dp_link_rate,')
    replace('dp_panel.c', '\t\tp->link_info.rate = rate;',
            '\t\tp->sq2_edp_use_rate_set = !bw_set;\n\t\tp->link_info.rate = rate;\n'
            '\t\tdev_info(priv->dev, "SQ2_EDP_PROTOCOL bw_before=%u use_rate_set=%u rate=%u\\n",\n'
            '\t\t\t bw_set, p->sq2_edp_use_rate_set, rate);')
    replace('dp_ctrl.c', '\tu8 values[2], rate_index;', '\tu8 lane_count, rate_value;')
    start = texts['dp_ctrl.c'].index('\tvalues[0] = index >= 0 ? 0 :')
    end = texts['dp_ctrl.c'].index('\n\treturn 0;', start)
    texts['dp_ctrl.c'] = texts['dp_ctrl.c'][:start] + '''	lane_count = link->num_lanes;
	if (link->capabilities & DP_LINK_CAP_ENHANCED_FRAMING)
		lane_count |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	if (index >= 0) {
		/* Follow the existing protocol; do not zero BW_SET to switch it. */
		err = drm_dp_dpcd_write(aux, DP_LANE_COUNT_SET, &lane_count, 1);
		if (err != 1)
			return err < 0 ? err : -EIO;
		rate_value = panel->sq2_edp_use_rate_set ? index :
			drm_dp_link_rate_to_bw_code(link->rate);
		err = drm_dp_dpcd_write(aux, panel->sq2_edp_use_rate_set ?
			DP_LINK_RATE_SET : DP_LINK_BW_SET, &rate_value, 1);
		if (err != 1)
			return err < 0 ? err : -EIO;
	} else {
		/* Keep the original non-table path byte-for-byte on the wire. */
		u8 values[2] = { drm_dp_link_rate_to_bw_code(link->rate), lane_count };
		err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
		if (err != sizeof(values))
			return err < 0 ? err : -EIO;
	}''' + texts['dp_ctrl.c'][end:]
    for name, data in texts.items():
        (dp / name).write_text(data)


if __name__ == '__main__':
    patch(sys.argv[1])
    print('Isolated protocol candidate prepared; not hardware validated')
