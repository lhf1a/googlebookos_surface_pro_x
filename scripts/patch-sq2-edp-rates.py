#!/usr/bin/env python3
"""Patch a dedicated matching msm source copy; no live device operations."""
import hashlib
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
dp = Path(sys.argv[1]) / 'drivers/gpu/drm/msm/dp'
expected = {
 'dp_panel.c': '79a6bdb50478bfaf82c40b330e2b0531185c27f3e9794f5f86286550bf7069f5',
 'dp_panel.h': '2032ccc416f4d7eb630e2ffc44dffe4c251f4a7f942f42cb5c5eb5ebdf4418c7',
 'dp_ctrl.c': '43bc6b3701cc4ad2078680daaed46efac22b3d03a53893a1dfc55f1069a53bcc',
}
texts = {}
for name, sha in expected.items():
 data = (dp/name).read_bytes()
 if hashlib.sha256(data).hexdigest() != sha:
  raise ValueError('Expected unmodified pinned source: '+name)
 texts[name] = data.decode()

def replace(name, old, new):
 if texts[name].count(old) != 1:
  raise ValueError('Nonunique anchor in '+name)
 texts[name] = texts[name].replace(old, new)

replace('dp_panel.h', '#include "dp_hpd.h"', '#include "dp_hpd.h"\n#include "sq2_edp_rates.h"')
replace('dp_panel.h', '\tu32 max_bw_code;', '\tu32 max_bw_code;\n\tu32 sq2_edp_rates[8];')
replace('dp_panel.c', '#include <drm/drm_print.h>', '#include <drm/drm_print.h>\n#include <linux/of.h>')
replace('dp_panel.c', 'static int dp_panel_read_dpcd(struct dp_panel *dp_panel)', '''/* SQ2_EDP_RATE_TABLE_V1: opt-in by using the isolated candidate module. */
static void sq2_edp_read_rates(struct dp_panel *p, struct dp_panel_private *priv)
{
	u8 rev, raw[16];
	u32 rate, limit;

	memset(p->sq2_edp_rates, 0, sizeof(p->sq2_edp_rates));
	if (!p->connector || p->connector->connector_type != DRM_MODE_CONNECTOR_eDP ||
	    !of_machine_is_compatible("microsoft,surface-pro-x"))
		return;
	if (drm_dp_dpcd_read(priv->aux, DP_EDP_DPCD_REV, &rev, 1) != 1 ||
	    rev < DP_EDP_14)
		return;
	if (drm_dp_dpcd_read(priv->aux, DP_SUPPORTED_LINK_RATES, raw, sizeof(raw)) != sizeof(raw))
		return;
	limit = min(p->max_dp_link_rate, (u32)drm_dp_bw_code_to_link_rate(p->max_bw_code));
	rate = sq2_edp_parse_rates(raw, limit, p->sq2_edp_rates);
	if (rate) {
		p->link_info.rate = rate;
		dev_info(priv->dev, "SQ2 eDP rate table: max=%u index=%d lanes=%u\\n",
			 rate, sq2_edp_rate_index(p->sq2_edp_rates, rate), p->link_info.num_lanes);
	}
}

static int dp_panel_read_dpcd(struct dp_panel *dp_panel)''')
replace('dp_panel.c', '\tdrm_dbg_dp(panel->drm_dev, "version: %d.%d\\n", major, minor);', '\tsq2_edp_read_rates(dp_panel, panel);\n\n\tdrm_dbg_dp(panel->drm_dev, "version: %d.%d\\n", major, minor);')
# connector is needed before sink capability read, not only after EDID handling.
replace('dp_panel.c', '\n\trc = dp_panel_read_dpcd(dp_panel);', '\n\tdp_panel->connector = connector;\n\trc = dp_panel_read_dpcd(dp_panel);')
replace('dp_ctrl.c', 'struct dp_link_info *link)\n{\n\tu8 values[2];', 'struct dp_link_info *link, struct dp_panel *panel)\n{\n\tu8 values[2], rate_index;\n\tint index;')
replace('dp_ctrl.c', '\tvalues[0] = drm_dp_link_rate_to_bw_code(link->rate);', '''	index = sq2_edp_rate_index(panel->sq2_edp_rates, link->rate);
	if (sq2_edp_lower_rate(panel->sq2_edp_rates, ~0U) && index < 0)
		return -EINVAL;
	values[0] = index >= 0 ? 0 : drm_dp_link_rate_to_bw_code(link->rate);''')
replace('dp_ctrl.c', '''	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
	if (err < 0)
		return err;

	return 0;''', '''	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
	if (err != sizeof(values))
		return err < 0 ? err : -EIO;
	if (index >= 0) {
		rate_index = index;
		err = drm_dp_dpcd_write(aux, DP_LINK_RATE_SET, &rate_index, 1);
		if (err != 1)
			return err < 0 ? err : -EIO;
	}
	return 0;''')
replace('dp_ctrl.c', '\tdp_aux_link_configure(ctrl->aux, &link_info);', '''	ret = dp_aux_link_configure(ctrl->aux, &link_info, ctrl->panel);
	if (ret)
		return ret;''')
replace('dp_ctrl.c', '''static int dp_ctrl_link_rate_down_shift(struct dp_ctrl_private *ctrl)
{
	int ret = 0;
''', '''static int dp_ctrl_link_rate_down_shift(struct dp_ctrl_private *ctrl)
{
	int ret = 0;
	u32 next;

	if (sq2_edp_lower_rate(ctrl->panel->sq2_edp_rates, ~0U)) {
		next = sq2_edp_lower_rate(ctrl->panel->sq2_edp_rates,
					ctrl->link->link_params.rate);
		if (!next)
			return -EINVAL;
		ctrl->link->link_params.rate = next;
		return 0;
	}
''')
for name, data in texts.items():
 (dp/name).write_text(data)
(dp/'sq2_edp_rates.h').write_bytes((root/'probe/graphics/sq2_edp_rates.h').read_bytes())
# External module build has an absolute $(src); avoid prefixing it with srctree.
makefile = dp.parent/'Makefile'
makefile.write_text(makefile.read_text().replace('$(srctree)/$(src)', '$(src)'))
print('Patched pinned msm source: eDP rate table, original indices, checked AUX writes, table-aware fallback')
