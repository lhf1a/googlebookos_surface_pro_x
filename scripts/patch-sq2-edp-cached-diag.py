#!/usr/bin/env python3
"""Remove diagnostic AUX I/O from the pinned isolated V4/V5 source only.

Keep training/capability AUX transactions, PHY/clock/MSA code and all timings.
This is an experimental instrumentation change, not a high-refresh guarantee.
"""
import hashlib
from pathlib import Path
import sys

p = Path(sys.argv[1]) / 'drivers/gpu/drm/msm/dp/dp_panel.c'
data = p.read_bytes()
assert hashlib.sha256(data).hexdigest() in {
    '7976cd2e565fcbae59b778189f4d70d6b07602e2ce61be67cb7a438d20463774',
    '8198f8a58e12757b9e93ecbb355ca3fd599b9338e09fc1477782ba2a34e70f4f',
}, 'Expected untouched isolated diagnostic source (V4 or V5)'
s = data.decode()
start = s.index('void sq2_edp_diag_snapshot(')
end = s.index('static int dp_panel_read_dpcd(', start)
old = s[start:end]
cached = old[old.index('\tif (!sq2_edp_lower_rate'):old.index('\tfor (i = 0;')]
new = ('void sq2_edp_diag_snapshot(struct dp_panel *p, struct seq_file *seq)\n{\n'
       '\t/* V6 cached state only: no diagnostic AUX transactions. */\n'
       + cached + '}\n\n')
assert 'drm_dp_dpcd_read' not in new
result = (s[:start] + new + s[end:]).encode()
assert data != result
p.write_bytes(result)
assert p.read_bytes() == result
print('Cached diagnostic only; capability/training AUX retained. SHA256:',
      hashlib.sha256(result).hexdigest())
