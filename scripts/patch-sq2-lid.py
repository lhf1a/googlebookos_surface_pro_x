#!/usr/bin/env python3
"""Expose the SSAM cover's known closed state as the standard Linux lid switch."""
from pathlib import Path
import sys

p = Path(sys.argv[1])
s = p.read_text()
if 'SQ2_LID_SWITCH_V1' not in s:
    marker = '\tinput_report_switch(sw->mode_switch, SW_TABLET_MODE, tablet);'
    assert s.count(marker) == 2
    s = s.replace(marker, marker + '''
	/* SQ2_LID_SWITCH_V1: the SSAM state distinguishes closed from folded. */
	input_report_switch(sw->mode_switch, SW_LID,
		!strcmp(sw->ops.state_name(sw, &sw->state), "closed"));''')
    cap = '\tinput_set_capability(sw->mode_switch, EV_SW, SW_TABLET_MODE);'
    assert s.count(cap) == 1
    s = s.replace(cap, cap + '\n\tinput_set_capability(sw->mode_switch, EV_SW, SW_LID);')
    s = s.replace('#include <linux/types.h>', '#include <linux/types.h>\n#include <linux/string.h>')
    p.write_text(s)
print('SSAM cover state -> SW_LID patch ready')
