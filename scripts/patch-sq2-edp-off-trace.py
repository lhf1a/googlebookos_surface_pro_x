#!/usr/bin/env python3
"""V14 diagnostic: split the V12 controller-off/PHY-exit observation window.

Only bounded reads of the existing HPD status accessor and logging are added.
Core/AUX clocks stay enabled during dp_ctrl_off; no register writes, new AUX
transactions, extra waits or display behavior changes. Not a default fix.
"""
import hashlib
from pathlib import Path
import re
import sys

BEGIN = '/* SQ2_OFF_TRACE_BEGIN */\n'
END = '/* SQ2_OFF_TRACE_END */\n'


def block(code):
    return BEGIN + code + END


def patch(stage):
    directory = Path(stage) / 'drivers/gpu/drm/msm/dp'
    expected = {
        'dp_ctrl.c': '59b44cbf192e1f37da088a03dba473baaf7f6affe061224fd30a33a55d31134a',
        'dp_display.c': '9ab7d9a34d6c4a910a6ee31c0cfa27b79ab975ea334984e423d7d56aa16243ae',
    }
    outputs = {}
    for name, digest in expected.items():
        original = (directory / name).read_bytes()
        if hashlib.sha256(original).hexdigest() != digest:
            raise ValueError('Expected pinned V12 source: ' + name)
        text = original.decode()
        if name == 'dp_ctrl.c':
            helper = block('''static void sq2_trace_ctrl_off(struct dp_ctrl_private *ctrl, const char *event)
{
	static atomic_t samples = ATOMIC_INIT(0);
	if (atomic_inc_return(&samples) <= 96)
		pr_info("SQ2_OFF_TRACE event=%s hpd_hw=%x\\n", event,
			dp_catalog_link_is_connected(ctrl->catalog));
}

''')
            anchor = 'int dp_ctrl_off(struct dp_ctrl *dp_ctrl)\n'
            assert text.count(anchor) == 1
            text = text.replace(anchor, helper + anchor)
            start = text.index(anchor)
            end = text.index('\nvoid dp_ctrl_isr', start)
            function = text[start:end]
            steps = [
                ('\tphy = dp_io->phy;\n', 'begin'),
                ('\tdp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);\n', 'after_mainlink_off'),
                ('\tdp_catalog_ctrl_reset(ctrl->catalog);\n', 'after_sw_reset'),
                ('\tret = dp_power_clk_enable(ctrl->power, DP_STREAM_PM, false);\n', 'after_pixel_clocks_off'),
                ('\tdev_pm_opp_set_rate(ctrl->dev, 0);\n', 'after_opp_zero'),
                ('\tret = dp_power_clk_enable(ctrl->power, DP_CTRL_PM, false);\n', 'after_link_clocks_off'),
                ('\tphy_power_off(phy);\n', 'after_phy_power_off'),
            ]
            for anchor, event in steps:
                assert function.count(anchor) == 1
                function = function.replace(anchor, anchor + block(
                    '\tsq2_trace_ctrl_off(ctrl, "' + event + '");\n'))
            text = text[:start] + function + text[end:]
        else:
            anchor = '\t\tdp_ctrl_off(dp->ctrl);\n\t\tdp_display_host_phy_exit(dp);'
            assert text.count(anchor) == 1
            text = text.replace(anchor, '\t\tdp_ctrl_off(dp->ctrl);\n' + block(
                '\t\tsq2_trace_display(dp, "after_ctrl_off_before_phy_exit");\n') +
                '\t\tdp_display_host_phy_exit(dp);')
        stripped = re.sub(re.escape(BEGIN) + r'.*?' + re.escape(END), '', text, flags=re.S)
        assert stripped.encode() == original, 'Changes outside diagnostic blocks'
        outputs[name] = text
    for name, text in outputs.items():
        (directory / name).write_text(text)


if __name__ == '__main__':
    patch(sys.argv[1])
    print('V14 bounded off-path observations added; stripping blocks restores V12 exactly')
