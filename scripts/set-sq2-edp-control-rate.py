#!/usr/bin/env python3
"""Set an isolated staged candidate's rate cap for a table-negotiation control."""
from pathlib import Path
import sys

stage = Path(sys.argv[1])
rate = int(sys.argv[2])
assert rate in (270000, 540000)
p = stage / 'drivers/gpu/drm/msm/dp/dp_panel.c'
s = p.read_text()
anchor = '\trate = sq2_edp_parse_rates(raw, limit, p->sq2_edp_rates);'
assert s.count(anchor) == 1 and 'SQ2_EDP_CONTROL_CAP' not in s
s = s.replace(anchor, f'\tlimit = min(limit, (u32){rate}); /* SQ2_EDP_CONTROL_CAP */\n' + anchor)
p.write_text(s)
print('Isolated control rate cap:', rate)
