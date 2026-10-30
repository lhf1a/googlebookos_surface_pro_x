#!/usr/bin/env python3
"""Add cached training/clock results and bounded AUX snapshots to rate candidate."""
from pathlib import Path
import sys
dp=Path(sys.argv[1])/'drivers/gpu/drm/msm/dp'
def edit(name, old, new):
 p=dp/name; text=p.read_text()
 assert text.count(old)==1, (name,old[:80],text.count(old))
 p.write_text(text.replace(old,new))
assert 'SQ2_EDP_DIAG_V4' not in (dp/'dp_panel.h').read_text()
edit('dp_panel.h','\tu32 sq2_edp_rates[8];','''	u32 sq2_edp_rates[8];
	/* SQ2_EDP_DIAG_V4: last observed results, not synthetic success flags. */
	u32 sq2_training_attempts;
	int sq2_train_ret, sq2_phy_config_ret, sq2_phy_power_ret, sq2_opp_ret;
	int sq2_pixel_ret, sq2_video_ret;
	unsigned long sq2_pixel_requested, sq2_pixel_actual;
''')
edit('dp_panel.h','int dp_panel_init_panel_info(struct dp_panel *dp_panel);','struct seq_file;\nvoid sq2_edp_diag_snapshot(struct dp_panel *p, struct seq_file *seq);\nint dp_panel_init_panel_info(struct dp_panel *dp_panel);')
edit('dp_panel.c','#include <linux/of.h>','#include <linux/of.h>\n#include <linux/seq_file.h>')
edit('dp_panel.c','static int dp_panel_read_dpcd(struct dp_panel *dp_panel)','''void sq2_edp_diag_snapshot(struct dp_panel *p, struct seq_file *seq)
{
	struct dp_panel_private *priv = container_of(p, struct dp_panel_private, dp_panel);
	const unsigned int offsets[] = { 0x100, 0x110, 0x200 };
	u8 data[16];
	ssize_t rc;
	int i;
	if (!sq2_edp_lower_rate(p->sq2_edp_rates, ~0U))
		return;
	seq_printf(seq, "SQ2_EDP_DIAG_V4 train_attempts=%u train_ret=%d phy_config=%d phy_power=%d opp=%d pixel_ret=%d pixel_requested=%lu pixel_actual=%lu video_ret=%d\\n",
		p->sq2_training_attempts, p->sq2_train_ret, p->sq2_phy_config_ret,
		p->sq2_phy_power_ret, p->sq2_opp_ret, p->sq2_pixel_ret,
		p->sq2_pixel_requested, p->sq2_pixel_actual, p->sq2_video_ret);
	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		memset(data, 0, sizeof(data));
		rc = drm_dp_dpcd_read(priv->aux, offsets[i], data, sizeof(data));
		seq_printf(seq, "dpcd_%05x rc=%zd data=%*ph\\n", offsets[i], rc,
			rc == sizeof(data) ? (int)sizeof(data) : 0, data);
	}
}

static int dp_panel_read_dpcd(struct dp_panel *dp_panel)''')
edit('dp_debug.c','\tdrm_mode = &debug->panel->dp_mode.drm_mode;','\tsq2_edp_diag_snapshot(debug->panel, seq);\n\tdrm_mode = &debug->panel->dp_mode.drm_mode;')
edit('dp_ctrl.c','\tret = dp_aux_link_configure(ctrl->aux, &link_info, ctrl->panel);\n\tif (ret)\n\t\treturn ret;', '''	ctrl->panel->sq2_training_attempts++;
	ret = dp_aux_link_configure(ctrl->aux, &link_info, ctrl->panel);
	ctrl->panel->sq2_train_ret = ret;
	if (ret)
		return ret;''')
edit('dp_ctrl.c','end:\n\tdp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);','''end:
	ctrl->panel->sq2_train_ret = ret;
	dev_info(ctrl->dev, "SQ2_EDP_TRACE train rate=%u lanes=%u step=%d rc=%d\\n",
		ctrl->link->link_params.rate, ctrl->link->link_params.num_lanes,
		*training_step, ret);
	dp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);''')
edit('dp_ctrl.c','\tif (num)\n\t\tclk_set_rate(cfg->clk, rate);\n\telse','''	if (num) {
		ctrl->panel->sq2_pixel_ret = clk_set_rate(cfg->clk, rate);
		ctrl->panel->sq2_pixel_requested = rate;
		ctrl->panel->sq2_pixel_actual = clk_get_rate(cfg->clk);
		dev_info(ctrl->dev, "SQ2_EDP_TRACE clock=%s requested=%lu actual=%lu rc=%d\\n",
			name, rate, ctrl->panel->sq2_pixel_actual, ctrl->panel->sq2_pixel_ret);
	} else''')
edit('dp_ctrl.c','\tphy_configure(phy, &dp_io->phy_opts);\n\tphy_power_on(phy);\n\n\tdev_pm_opp_set_rate(ctrl->dev, ctrl->link->link_params.rate * 1000);','''	ctrl->panel->sq2_phy_config_ret = phy_configure(phy, &dp_io->phy_opts);
	ctrl->panel->sq2_phy_power_ret = phy_power_on(phy);
	ctrl->panel->sq2_opp_ret = dev_pm_opp_set_rate(ctrl->dev, ctrl->link->link_params.rate * 1000);
	dev_info(ctrl->dev, "SQ2_EDP_TRACE phy rate=%u config=%d power=%d opp=%d\\n",
		ctrl->link->link_params.rate, ctrl->panel->sq2_phy_config_ret,
		ctrl->panel->sq2_phy_power_ret, ctrl->panel->sq2_opp_ret);''')
edit('dp_ctrl.c','\tret = dp_ctrl_wait4video_ready(ctrl);\n\tif (ret)\n\t\treturn ret;', '''	ret = dp_ctrl_wait4video_ready(ctrl);
	ctrl->panel->sq2_video_ret = ret;
	dev_info(ctrl->dev, "SQ2_EDP_TRACE video_ready=%d\\n", ret);
	if (ret)
		return ret;''')
edit('dp_catalog.c','\tdp_write_link(catalog, REG_DP_SOFTWARE_MVID, mvid);','\tpr_info("SQ2_EDP_TRACE msa rate=%u pixel=%u mvid=%u nvid=%u\\n", rate, stream_rate_khz, mvid, nvid);\n\tdp_write_link(catalog, REG_DP_SOFTWARE_MVID, mvid);')
print('Added eDP diagnostic snapshot; no PLL/timing/voltage changes')
