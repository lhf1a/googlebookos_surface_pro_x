#!/usr/bin/env python3
"""Isolated V11 candidate: keep internal-panel modesets out of unplug state.

Requires the pinned V10 trace stage. Not a default or a verified hardware fix.
2026-10-30 test: corrected DISPLAY_OFF transition, but retraining still blocked
at HPD wait; user saw black and manually restored. Not a successful fix.
The upstream state-transition change is discussed at
https://lkml.iu.edu/2206.3/00689.html ; this experiment is Surface Pro X scoped.
It retains HPD checks, PHY configuration, power handling and forced retraining.
"""
import hashlib
from pathlib import Path
import sys


def patch(stage):
    p = Path(stage) / 'drivers/gpu/drm/msm/dp/dp_display.c'
    raw = p.read_bytes()
    if hashlib.sha256(raw).hexdigest() != '5b6d0d0ef47f03f765cd7e55e8c7ea7686a9747ea4df8d641bf6a1c549692972':
        raise ValueError('Expected untouched V10 display source')
    s = raw.decode()
    def replace(old, new):
        nonlocal s
        if s.count(old) != 1:
            raise ValueError('Nonunique modeset anchor')
        s = s.replace(old, new)
    replace('#include <linux/of_irq.h>', '#include <linux/of_irq.h>\n#include <linux/of.h>')
    replace('\tif (dp->is_edp)\n\t\tdp_hpd_plug_handle(dp_display, 0);',
            '\t/* SQ2_EDP_MODESET_STATE_V1: ordinary modeset is not unplug. */\n'
            '\tif (dp->is_edp &&\n'
            '\t    (!of_machine_is_compatible("microsoft,surface-pro-x") ||\n'
            '\t     dp_display->hpd_state == ST_DISCONNECTED))\n'
            '\t\tdp_hpd_plug_handle(dp_display, 0);')
    replace('\tif (dp->is_edp)\n\t\tdp_hpd_unplug_handle(dp_display, 0);',
            '\tsq2_trace_display(dp_display, "bridge_post_disable_begin");\n'
            '\tif (dp->is_edp && !of_machine_is_compatible("microsoft,surface-pro-x"))\n'
            '\t\tdp_hpd_unplug_handle(dp_display, 0);')
    p.write_text(s)


if __name__ == '__main__':
    patch(sys.argv[1])
    print('Surface Pro X eDP modeset state candidate prepared; hardware result pending')
