#!/usr/bin/env python3
"""V17 isolated hypothesis: rearm enabled HPD detection after high-rate reinit.

Pinned V16 observations found TLMM HPD input high while DP HPD stayed pending.
Scope: Surface Pro X eDP, remembered rate >= 540000, pending after PHY init.
Does not bypass HPD, change timers/voltage or alter initial/low-rate startup.
V17 was loaded and exercised on the lab device but did not restore readiness
or output. Subsequent GPIO sampling found repeated short pulses. Failed
candidate retained for reproducibility; never included by default.
"""
import hashlib
from pathlib import Path
import re
import sys

BEGIN = '/* SQ2_HPD_REARM_BEGIN */\n'
END = '/* SQ2_HPD_REARM_END */\n'

SCOPE = '''static bool sq2_hpd_rearm_needed(bool is_edp, bool surface, u32 rate, u32 hpd)
{
	return is_edp && surface && rate >= 540000 && hpd == 2;
}

'''

HELPER = '''void sq2_catalog_hpd_rearm(struct dp_catalog *dp_catalog)
{
	struct dp_catalog_private *catalog = container_of(dp_catalog,
			struct dp_catalog_private, dp_catalog);
	u32 ctrl = dp_read_aux(catalog, REG_DP_DP_HPD_CTRL);

	if (!(ctrl & DP_DP_HPD_CTRL_HPD_EN))
		return;
	/* Preserve every other control bit; read back to order the transitions. */
	dp_write_aux(catalog, REG_DP_DP_HPD_CTRL, ctrl & ~DP_DP_HPD_CTRL_HPD_EN);
	dp_read_aux(catalog, REG_DP_DP_HPD_CTRL);
	dp_write_aux(catalog, REG_DP_DP_HPD_CTRL, ctrl);
	dp_read_aux(catalog, REG_DP_DP_HPD_CTRL);
}

'''


def patch(stage):
    directory = Path(stage) / 'drivers/gpu/drm/msm/dp'
    expected = {
        'dp_catalog.c': 'c6dec6b7888d5a5dc8fd5a2f21d9dfa3ca48abfbe40a996e6e875954f2051039',
        'dp_catalog.h': '093833d23b1cd350927fece34539e2223d83bfcfd36a7f1bceb4643137227fc0',
        'dp_display.c': 'af3e6652846663b101b6611a663d0ede403f91fee44e6e63b911d221e774900f',
    }
    outputs = {}
    for name, digest in expected.items():
        original = (directory / name).read_bytes()
        if hashlib.sha256(original).hexdigest() != digest:
            raise ValueError('Expected pinned V16: ' + name)
        text = original.decode()

        def before(anchor, addition):
            nonlocal text
            assert text.count(anchor) == 1
            text = text.replace(anchor, BEGIN + addition + END + anchor)

        if name == 'dp_catalog.c':
            before('void sq2_catalog_reinit_trace(', HELPER)
        elif name == 'dp_catalog.h':
            before('void sq2_catalog_reinit_trace(', 'void sq2_catalog_hpd_rearm(struct dp_catalog *dp_catalog);\n')
        else:
            before('#include <linux/module.h>', '#include <linux/of.h>\n')
            before('static void dp_display_host_phy_init(', SCOPE)
            before('\t\tdp->phy_initialized = true;', '''		if (sq2_hpd_rearm_needed(dp->dp_display.is_edp,
			    of_machine_is_compatible("microsoft,surface-pro-x"),
			    dp->link->link_params.rate,
			    dp_catalog_link_is_connected(dp->catalog))) {
			sq2_trace_display(dp, "before_hpd_rearm");
			sq2_catalog_hpd_rearm(dp->catalog);
			sq2_trace_display(dp, "after_hpd_rearm");
		}
''')
        assert re.sub(re.escape(BEGIN) + r'.*?' + re.escape(END), '', text, flags=re.S).encode() == original
        outputs[name] = text
    for name, text in outputs.items():
        (directory / name).write_text(text)


if __name__ == '__main__':
    patch(sys.argv[1])
    print('Scoped experimental HPD rearm added; HPD readiness gate retained')
