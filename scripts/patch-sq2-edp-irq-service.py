#!/usr/bin/env python3
"""V19 experiment: service eDP short HPD pulses through the existing handler.

Only Surface Pro X internal eDP without internal_hpd. Enable IRQ_HPD after
successful training, mask it before disconnect, never enable plug/unplug or
replug events. A 32-event boot budget prevents a persistent experimental IRQ
storm. V19 hardware still showed black output: mask bit was enabled but no
handler events were recorded before teardown. Not a default patch or a fix.
Input is V16 plus the corrected <=16-byte sink snapshot diagnostic.
"""
from pathlib import Path
import hashlib
import re
import sys

BEGIN = '/* SQ2_IRQ_SERVICE_BEGIN */\n'
END = '/* SQ2_IRQ_SERVICE_END */\n'


def patch(stage):
    path = Path(stage) / 'drivers/gpu/drm/msm/dp/dp_display.c'
    original = path.read_bytes()
    if hashlib.sha256(original).hexdigest() != 'f3ebfdf91472c139884cbb594f364e9b70601cb212360955095583b05374758a':
        raise ValueError('Expected V16 plus corrected sink snapshots')
    text = original.decode()

    def before(anchor, code):
        nonlocal text
        assert text.count(anchor) == 1
        text = text.replace(anchor, BEGIN + code + END + anchor)

    before('#include <linux/module.h>', '#include <linux/of.h>\n')
    before('static int dp_hpd_plug_handle(', '''static bool sq2_edp_irq_target(struct dp_display_private *dp)
{
	return dp->dp_display.is_edp && !dp->dp_display.internal_hpd &&
		of_machine_is_compatible("microsoft,surface-pro-x");
}

''')
    before('\t/* enable HDP irq_hpd/replug interrupt */', '''	if (!ret && sq2_edp_irq_target(dp)) {
		dp_catalog_hpd_config_intr(dp->catalog, DP_DP_IRQ_HPD_INT_MASK, true);
		pr_info("SQ2_IRQ_SERVICE enabled rate=%u\\n", dp->link->link_params.rate);
	}
''')
    before('\t/* disable irq_hpd/replug interrupts */', '''	if (sq2_edp_irq_target(dp))
		dp_catalog_hpd_config_intr(dp->catalog, DP_DP_IRQ_HPD_INT_MASK, false);
''')
    # Bound this experimental path before invoking the existing request handler.
    start = text.index('static int dp_irq_hpd_handle(')
    end = text.index('static void dp_display_deinit_sub_modules', start)
    fragment = text[start:end]
    anchor = '\tdp_display_usbpd_attention_cb(&dp->pdev->dev);\n'
    assert fragment.count(anchor) == 1
    fragment = fragment.replace(anchor, BEGIN + '''	if (sq2_edp_irq_target(dp)) {
		static atomic_t events = ATOMIC_INIT(0);
		int n = atomic_inc_return(&events);
		if (n > 32) {
			dp_catalog_hpd_config_intr(dp->catalog, DP_DP_IRQ_HPD_INT_MASK, false);
			dp_del_event(dp, EV_IRQ_HPD_INT);
			if (n == 33)
				pr_warn("SQ2_IRQ_SERVICE budget exhausted; masked\\n");
			mutex_unlock(&dp->event_mutex);
			return 0;
		}
		pr_info("SQ2_IRQ_SERVICE event=%d state=%u\\n", n, state);
	}
''' + END + anchor)
    text = text[:start] + fragment + text[end:]
    assert re.sub(re.escape(BEGIN) + r'.*?' + re.escape(END), '', text, flags=re.S).encode() == original
    path.write_text(text)


if __name__ == '__main__':
    patch(sys.argv[1])
    print('Scoped IRQ_HPD service experiment added with 32-event budget')
