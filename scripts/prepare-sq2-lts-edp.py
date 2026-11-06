#!/usr/bin/env python3
"""Prepare a fresh external msm copy with eDP rate-table support for 6.18.

No deployment, diagnostic hooks, timing change or modpost warning override.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import shutil


def patch(texts):
    def rep(name, old, new):
        assert texts[name].count(old) == 1, (name, old)
        texts[name] = texts[name].replace(old, new, 1)
    rep('dp_panel.h', '#include "dp_link.h"', '#include "dp_link.h"\n#include "sq2_edp_rates.h"')
    rep('dp_panel.h', '\tu32 max_bw_code;', '\tu32 max_bw_code;\n\tu32 sq2_edp_rates[8];')
    rep('dp_panel.c', '#include <drm/drm_print.h>', '#include <drm/drm_print.h>\n#include <linux/of.h>')
    anchor = 'static int msm_dp_panel_read_dpcd(struct msm_dp_panel *msm_dp_panel)'
    helper = '''/* SQ2_LTS_EDP_RATES: intersect receiver table with source/endpoint/repeater. */
static void sq2_lts_edp_read_rates(struct msm_dp_panel *p,
		struct msm_dp_panel_private *priv, int repeater_limit)
{
	u8 rev, raw[16];
	u32 rate, limit;

	memset(p->sq2_edp_rates, 0, sizeof(p->sq2_edp_rates));
	if (!p->connector || p->connector->connector_type != DRM_MODE_CONNECTOR_eDP ||
	    !of_machine_is_compatible("microsoft,surface-pro-x"))
		return;
	if (drm_dp_dpcd_read(priv->aux, DP_EDP_DPCD_REV, &rev, 1) != 1 || rev < DP_EDP_14)
		return;
	if (drm_dp_dpcd_read(priv->aux, DP_SUPPORTED_LINK_RATES, raw, sizeof(raw)) != sizeof(raw))
		return;
	limit = min(p->max_dp_link_rate, (u32)drm_dp_bw_code_to_link_rate(p->max_bw_code));
	if (repeater_limit > 0)
		limit = min(limit, (u32)repeater_limit);
	rate = sq2_edp_parse_rates(raw, limit, p->sq2_edp_rates);
	if (rate)
		p->link_info.rate = rate;
}

'''
    rep('dp_panel.c', anchor, helper + anchor)
    rep('dp_panel.c', '\tdrm_dbg_dp(panel->drm_dev, "version: %d.%d\\n", major, minor);',
        '\tsq2_lts_edp_read_rates(msm_dp_panel, panel, max_lttpr_rate);\n\n\tdrm_dbg_dp(panel->drm_dev, "version: %d.%d\\n", major, minor);')
    rep('dp_panel.c', '\trc = msm_dp_panel_read_dpcd(msm_dp_panel);',
        '\tmsm_dp_panel->connector = connector;\n\trc = msm_dp_panel_read_dpcd(msm_dp_panel);')
    rep('dp_ctrl.c', 'struct msm_dp_link_info *link)\n{\n\tu8 values[2];',
        'struct msm_dp_link_info *link, struct msm_dp_panel *panel)\n{\n\tu8 values[2], rate_index;\n\tint index;')
    rep('dp_ctrl.c', '\tvalues[0] = drm_dp_link_rate_to_bw_code(link->rate);', '''	index = sq2_edp_rate_index(panel->sq2_edp_rates, link->rate);
	if (sq2_edp_lower_rate(panel->sq2_edp_rates, ~0U) && index < 0)
		return -EINVAL;
	values[0] = index >= 0 ? 0 : drm_dp_link_rate_to_bw_code(link->rate);''')
    rep('dp_ctrl.c', '''	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
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
    rep('dp_ctrl.c', '\tmsm_dp_aux_link_configure(ctrl->aux, &link_info);',
        '\tret = msm_dp_aux_link_configure(ctrl->aux, &link_info, ctrl->panel);\n\tif (ret)\n\t\treturn ret;')
    rep('dp_ctrl.c', '''static int msm_dp_ctrl_link_rate_down_shift(struct msm_dp_ctrl_private *ctrl)
{
	int ret = 0;
''', '''static int msm_dp_ctrl_link_rate_down_shift(struct msm_dp_ctrl_private *ctrl)
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
    return texts


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    assert not a.output.exists(), 'fresh copy required'
    source = a.kernel / 'drivers/gpu/drm/msm'
    names = ['dp_panel.h', 'dp_panel.c', 'dp_ctrl.c']
    before = {n: (source / 'dp' / n).read_text() for n in names}
    after = patch(before.copy())
    shutil.copytree(source, a.output / 'drivers/gpu/drm/msm')
    for header in source.parent.glob('*.h'):
        shutil.copy2(header, a.output / 'drivers/gpu/drm' / header.name)
    dest = a.output / 'drivers/gpu/drm/msm'
    for n in names:
        (dest / 'dp' / n).write_text(after[n])
    root = Path(__file__).resolve().parents[1]
    shutil.copy2(root / 'probe/graphics/sq2_edp_rates.h', dest / 'dp/sq2_edp_rates.h')
    mk = dest / 'Makefile'
    mk.write_text(mk.read_text().replace('$(srctree)/$(src)', '$(src)'))
    (a.output / 'source.patch').write_text(''.join(''.join(difflib.unified_diff(before[n].splitlines(True), after[n].splitlines(True), fromfile='a/drivers/gpu/drm/msm/dp/'+n, tofile='b/drivers/gpu/drm/msm/dp/'+n)) for n in names))
    (a.output / 'provenance.json').write_text(json.dumps({n: {'before': hashlib.sha256(before[n].encode()).hexdigest(), 'after': hashlib.sha256(after[n].encode()).hexdigest()} for n in names}, indent=2))
    print(a.output)


if __name__ == '__main__':
    main()
