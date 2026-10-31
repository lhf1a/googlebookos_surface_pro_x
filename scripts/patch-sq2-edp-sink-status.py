#!/usr/bin/env python3
"""V18 bounded, read-only receiver snapshots before the observed HPD reset.

Pinned V16 input. Reads link configuration and sink status immediately after
stream enable and before idle, only while the existing HPD accessor is ready.
No writes/acknowledgements, no readiness bypass. AUX reads can affect timing;
this is an isolated diagnostic, not a default fix.
"""
from pathlib import Path
import hashlib
import re
import sys

BEGIN = '/* SQ2_SINK_SNAPSHOT_BEGIN */\n'
END = '/* SQ2_SINK_SNAPSHOT_END */\n'


def patch(stage):
    path = Path(stage) / 'drivers/gpu/drm/msm/dp/dp_display.c'
    original = path.read_bytes()
    if hashlib.sha256(original).hexdigest() != 'af3e6652846663b101b6611a663d0ede403f91fee44e6e63b911d221e774900f':
        raise ValueError('Expected pinned V16 display source')
    text = original.decode()
    anchor = 'static void sq2_trace_display(struct dp_display_private *dp, const char *event)\n'
    helper = '''static void sq2_sink_snapshot(struct dp_display_private *dp, const char *event)
{
	u8 config[16] = {0}, rate[6] = {0}, status[8] = {0};
	int ret;
	static atomic_t count = ATOMIC_INIT(0);

	if (!dp->dp_display.is_edp || !dp->core_initialized ||
	    atomic_inc_return(&count) > 4)
		return;
	if (!(dp_catalog_link_is_connected(dp->catalog) & 4)) {
		pr_info("SQ2_SINK_SNAPSHOT event=%s skipped_hpd_not_ready\\n", event);
		return;
	}
	ret = drm_dp_dpcd_read(dp->aux, 0x200, status, sizeof(status));
	pr_info("SQ2_SINK_SNAPSHOT event=%s addr=200 ret=%d data=%*phN\\n",
		event, ret, ret > 0 ? min_t(int, ret, sizeof(status)) : 0, status);
	if (ret != sizeof(status))
		return;
	ret = drm_dp_dpcd_read(dp->aux, 0x100, config, sizeof(config));
	pr_info("SQ2_SINK_SNAPSHOT event=%s addr=100 ret=%d data=%*phN\\n",
		event, ret, ret > 0 ? min_t(int, ret, sizeof(config)) : 0, config);
	if (ret != sizeof(config))
		return;
	ret = drm_dp_dpcd_read(dp->aux, 0x110, rate, sizeof(rate));
	pr_info("SQ2_SINK_SNAPSHOT event=%s addr=110 ret=%d data=%*phN\\n",
		event, ret, ret > 0 ? min_t(int, ret, sizeof(rate)) : 0, rate);
}

'''
    assert text.count(anchor) == 1
    text = text.replace(anchor, BEGIN + helper + END + anchor)
    for event in ['stream_enable_end', 'bridge_before_idle']:
        obj = 'dp' if event == 'stream_enable_end' else 'dp_display'
        anchor = '\tsq2_trace_display(' + obj + ', "' + event + '");\n'
        assert text.count(anchor) == 1
        text = text.replace(anchor, anchor + BEGIN + '\tsq2_sink_snapshot(' + obj + ', "' + event + '");\n' + END)
    assert re.sub(re.escape(BEGIN) + r'.*?' + re.escape(END), '', text, flags=re.S).encode() == original
    path.write_text(text)


if __name__ == '__main__':
    patch(sys.argv[1])
    print('Bounded read-only receiver snapshots added without HPD bypass')
