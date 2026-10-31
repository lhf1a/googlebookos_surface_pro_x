#!/usr/bin/env python3
"""V12 observation only: sample HPD state around existing stream transitions.

Based on pinned V10. Adds bounded reads through the driver's existing,
non-acknowledging HPD status accessor. No AUX transfers or register writes.
Not a default build patch or a display fix.
"""
import hashlib
from pathlib import Path
import sys


def patch(stage):
    p=Path(stage)/'drivers/gpu/drm/msm/dp/dp_display.c'
    raw=p.read_bytes()
    if hashlib.sha256(raw).hexdigest()!='5b6d0d0ef47f03f765cd7e55e8c7ea7686a9747ea4df8d641bf6a1c549692972':
        raise ValueError('Expected pristine V10 display source')
    s=raw.decode()
    def replace(old,new):
        nonlocal s
        if s.count(old)!=1: raise ValueError('Ambiguous HPD trace anchor')
        s=s.replace(old,new)
    replace('sink=%u rate=%u\\n",','sink=%u rate=%u hpd_hw=%x\\n",')
    replace('\t\t\tdp->link->sink_count, dp->link->link_params.rate);',
            '\t\t\tdp->link->sink_count, dp->link->link_params.rate,\n'
            '\t\t\tdp_catalog_link_is_connected(dp->catalog));')
    replace('\trc = dp_ctrl_on_stream(dp->ctrl, force_link_train);',
            '\trc = dp_ctrl_on_stream(dp->ctrl, force_link_train);\n'
            '\tsq2_trace_display(dp, "stream_enable_end");')
    replace('\tdp_ctrl_push_idle(dp_display->ctrl);',
            '\tsq2_trace_display(dp_display, "bridge_before_idle");\n'
            '\tdp_ctrl_push_idle(dp_display->ctrl);\n'
            '\tsq2_trace_display(dp_display, "bridge_after_idle");')
    replace('\tif (dp->is_edp)\n\t\tdp_hpd_unplug_handle(dp_display, 0);',
            '\tsq2_trace_display(dp_display, "bridge_post_disable_begin");\n'
            '\tif (dp->is_edp)\n\t\tdp_hpd_unplug_handle(dp_display, 0);')
    replace('\tdp_display->power_on = false;',
            '\tsq2_trace_display(dp, "display_after_phy_off");\n'
            '\tdp_display->power_on = false;')
    p.write_text(s)


if __name__=='__main__':
    patch(sys.argv[1])
    print('HPD phase observations added; hardware connected bit is hpd_hw & 4')
