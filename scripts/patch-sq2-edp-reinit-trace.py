#!/usr/bin/env python3
"""V16 diagnostic on pinned V14 + original PHY: bounded reinit snapshots.

Reads controller configuration/status while core clocks are on, and PHY
configuration only after successful PHY init or before its exit. No writes,
new AUX transactions, delays or behavior changes. Not a default build patch.
Removing marked blocks exactly reproduces the pinned input sources.
"""
import hashlib
from pathlib import Path
import re
import sys

BEGIN = '/* SQ2_REINIT_TRACE_BEGIN */\n'
END = '/* SQ2_REINIT_TRACE_END */\n'


def block(text):
    return BEGIN + text + END


def patch(stage, phy_source):
    directory = Path(stage) / 'drivers/gpu/drm/msm/dp'
    expected = {
        directory / 'dp_catalog.c': '9ee755e8e8cb70abfba7cdb3f64899297d267d40a1842c5c57b7c23c5d0e1b45',
        directory / 'dp_catalog.h': '95369d1269e2fedeb72ca8bed1128ea3f1de21897be5c81f5ea3d3c2536d72a0',
        directory / 'dp_display.c': 'ba1469e9f4b53a868f29c201ec2b1a8724971ce0ef970906a48c2c6e9fa2a471',
        Path(phy_source): '8b8456da6ed8ea723cedb219162b5d4e77eb168f369eb6cd1a7e1e35e0ab0367',
    }
    outputs = {}
    for path, digest in expected.items():
        original = path.read_bytes()
        if hashlib.sha256(original).hexdigest() != digest:
            raise ValueError('Unexpected diagnostic input: ' + str(path))
        text = original.decode()

        def insert(anchor, addition, before=False):
            nonlocal text
            if text.count(anchor) != 1:
                raise ValueError('Ambiguous trace anchor: ' + anchor)
            text = text.replace(anchor, block(addition) + anchor if before else anchor + block(addition))

        if path.name == 'dp_catalog.c':
            insert('u32 dp_catalog_link_is_connected(struct dp_catalog *dp_catalog)\n', '''void sq2_catalog_reinit_trace(struct dp_catalog *dp_catalog, const char *event)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
			struct dp_catalog_private, dp_catalog);
	static atomic_t samples = ATOMIC_INIT(0);
	if (atomic_inc_return(&samples) > 48)
		return;
	pr_info("SQ2_HPD_REG event=%s ctrl=%08x ref=%08x time0=%08x time1=%08x status=%08x mask=%08x aux=%08x reset=%08x phyctrl=%08x\\n",
		event, dp_read_aux(catalog, REG_DP_DP_HPD_CTRL),
		dp_read_aux(catalog, REG_DP_DP_HPD_REFTIMER),
		dp_read_aux(catalog, REG_DP_DP_HPD_EVENT_TIME_0),
		dp_read_aux(catalog, REG_DP_DP_HPD_EVENT_TIME_1),
		dp_read_aux(catalog, REG_DP_DP_HPD_INT_STATUS),
		dp_read_aux(catalog, REG_DP_DP_HPD_INT_MASK),
		dp_read_aux(catalog, REG_DP_AUX_CTRL),
		dp_read_ahb(catalog, REG_DP_SW_RESET),
		dp_read_ahb(catalog, REG_DP_PHY_CTRL));
}

''', before=True)
        elif path.name == 'dp_catalog.h':
            insert('u32 dp_catalog_link_is_connected(struct dp_catalog *dp_catalog);\n',
                   'void sq2_catalog_reinit_trace(struct dp_catalog *dp_catalog, const char *event);\n')
        elif path.name == 'dp_display.c':
            insert('static void sq2_trace_display(struct dp_display_private *dp, const char *event)\n{\n',
                   '\tif (dp->core_initialized && dp->dp_display.is_edp)\n'
                   '\t\tsq2_catalog_reinit_trace(dp->catalog, event);\n')
            insert('\t\tdp_ctrl_phy_init(dp->ctrl);\n',
                   '\t\tsq2_trace_display(dp, "host_phy_init_done");\n')
        else:
            insert('static int qcom_edp_phy_init(struct phy *phy)\n', '''static void sq2_phy_reinit_trace(const struct qcom_edp *edp, const char *event)
{
	static atomic_t samples = ATOMIC_INIT(0);
	if (atomic_inc_return(&samples) > 24)
		return;
	pr_info("SQ2_PHY_REG event=%s saved_rate=%u cfg=%08x cfg1=%08x pd=%08x mode=%08x aux0=%08x aux1=%08x aux2=%08x aux4=%08x aux5=%08x aux8=%08x aux9=%08x bias=%08x\\n",
		event, edp->dp_opts.link_rate,
		readl(edp->edp + DP_PHY_CFG), readl(edp->edp + DP_PHY_CFG_1),
		readl(edp->edp + DP_PHY_PD_CTL), readl(edp->edp + DP_PHY_MODE),
		readl(edp->edp + DP_PHY_AUX_CFG0), readl(edp->edp + DP_PHY_AUX_CFG1),
		readl(edp->edp + DP_PHY_AUX_CFG2), readl(edp->edp + DP_PHY_AUX_CFG4),
		readl(edp->edp + DP_PHY_AUX_CFG5), readl(edp->edp + DP_PHY_AUX_CFG8),
		readl(edp->edp + DP_PHY_AUX_CFG9),
		readl(edp->pll + QSERDES_V4_COM_BIAS_EN_CLKBUFLR_EN));
}

''', before=True)
            # Insert within each named function to distinguish repeated statements.
            for function, next_function, anchor, event in [
                ('qcom_edp_phy_init', 'qcom_edp_set_voltages', '\treturn 0;\n', 'phy_init_done'),
                ('qcom_edp_phy_power_off', 'qcom_edp_phy_exit', '\twritel(DP_PHY_PD_CTL_PSR_PWRDN, edp->edp + DP_PHY_PD_CTL);\n', 'before_power_off'),
                ('qcom_edp_phy_exit', None, '\tclk_bulk_disable_unprepare(ARRAY_SIZE(edp->clks), edp->clks);\n', 'before_phy_exit'),
            ]:
                start = text.index('static int ' + function + '(')
                end = text.index('static int ' + next_function + '(', start) if next_function else text.index('static const struct phy_ops', start)
                fragment = text[start:end]
                assert fragment.count(anchor) == 1
                fragment = fragment.replace(anchor, block('\tsq2_phy_reinit_trace(edp, "' + event + '");\n') + anchor)
                text = text[:start] + fragment + text[end:]
        assert re.sub(re.escape(BEGIN) + r'.*?' + re.escape(END), '', text, flags=re.S).encode() == original
        outputs[path] = text
    for path, text in outputs.items():
        path.write_text(text)


if __name__ == '__main__':
    patch(sys.argv[1], sys.argv[2])
    print('Bounded HPD/PHY reinit snapshots added; marked blocks strip to exact original sources')
