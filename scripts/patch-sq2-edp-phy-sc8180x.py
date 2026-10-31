#!/usr/bin/env python3
"""Backport SC8180X eDP signal tables/LDO to an isolated pinned 6.3 PHY.

Experiment only: V8 loaded on hardware but did not fix the 65Hz blackscreen.
No live operations and no default-builder integration.
Upstream: Yongxing Mou's 2026 April series, patches 3/5 and 5/5;
torvalds/linux 3a2976df778a9af95e91f7ff88008b4517ddc658.
Keep the SC8180X identity, PLL/AUX configuration and external regulators.
"""
import hashlib
from pathlib import Path
import sys

EXPECTED = '8b8456da6ed8ea723cedb219162b5d4e77eb168f369eb6cd1a7e1e35e0ab0367'
TABLES = '''/* SC8180X eDP table v2 from upstream phy-qcom-edp.c (GPL-2.0),
 * contributed by Yongxing Mou in the 2026 April PHY series.
 * The old driver left SC8180X cfg NULL, so training levels were ignored.
 */
static const u8 sc8180x_edp_pre_emp_hbr2_hbr3[4][4] = {
	{ 0x0c, 0x15, 0x19, 0x1e },
	{ 0x08, 0x15, 0x19, 0xff },
	{ 0x0e, 0x14, 0xff, 0xff },
	{ 0x0d, 0xff, 0xff, 0xff }
};

static const struct qcom_edp_cfg sc8180x_edp_phy_cfg = {
	.is_dp = false,
	.swing_hbr_rbr = &edp_swing_hbr_rbr,
	.swing_hbr3_hbr2 = &edp_swing_hbr2_hbr3,
	.pre_emphasis_hbr_rbr = &edp_pre_emp_hbr_rbr,
	.pre_emphasis_hbr3_hbr2 = &sc8180x_edp_pre_emp_hbr2_hbr3,
};

static u8 sq2_edp_ldo_value(const struct qcom_edp_cfg *cfg, unsigned int rate)
{
	if (cfg == &sc8180x_edp_phy_cfg)
		return rate <= 2700 ? 0x81 : 0x41;
	return (cfg && cfg->is_dp) ? 0x1 : 0x0;
}

'''


def patch(data):
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError('Expected pinned unmodified 6.3 SQ2 eDP PHY')
    s = data.decode()
    anchor = 'static int qcom_edp_phy_init(struct phy *phy)'
    assert s.count(anchor) == 1
    s = s.replace(anchor, TABLES + anchor)
    anchor = '\tedp->cfg = of_device_get_match_data(&pdev->dev);'
    assert s.count(anchor) == 1
    s = s.replace(anchor, anchor + '''
	/* This backport is opt-in only on the Surface Pro X internal PHY. */
	if (of_machine_is_compatible("microsoft,surface-pro-x") &&
	    of_device_is_compatible(dev->of_node, "qcom,sc8180x-edp-phy")) {
		edp->cfg = &sc8180x_edp_phy_cfg;
		dev_info(dev, "SQ2_EDP_PHY_SC8180X_V2 enabled\\n");
	}''')
    anchor = '\tldo_config = (cfg && cfg->is_dp) ? 0x1 : 0x0;'
    assert s.count(anchor) == 2
    s = s.replace(anchor, '\tldo_config = sq2_edp_ldo_value(cfg, edp->dp_opts.link_rate);')
    anchor = '\twritel(ldo_config, edp->tx1 + TXn_LDO_CONFIG);'
    assert s.count(anchor) == 2
    s = s.replace(anchor, '\twritel(cfg == &sc8180x_edp_phy_cfg && edp->dp_opts.lanes <= 2 ?\n'
                          '\t       0 : ldo_config, edp->tx1 + TXn_LDO_CONFIG);')
    # Verify the preexisting common tables used by the new cfg remain intact.
    assert 'static const u8 edp_pre_emp_hbr_rbr[4][4] = {\n\t{ 0x05, 0x12, 0x17, 0x1d },' in s
    return s.encode()


if __name__ == '__main__':
    p = Path(sys.argv[1]) / 'phy-qcom-edp.c'
    result = patch(p.read_bytes())
    p.write_bytes(result)
    print(hashlib.sha256(result).hexdigest())
