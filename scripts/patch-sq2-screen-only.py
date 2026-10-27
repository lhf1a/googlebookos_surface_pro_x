#!/usr/bin/env python3
"""Block system suspend before the freezer; retain display and runtime PM."""
from pathlib import Path
import sys

path = Path(sys.argv[1]) / 'kernel/power/suspend.c'
text = path.read_text()
marker = 'SQ2_SCREEN_ONLY_V1'
if marker in text:
    raise SystemExit(0)
anchor = 'static const struct platform_suspend_ops *suspend_ops;'
assert text.count(anchor) == 1
text = text.replace(anchor, '''/* SQ2_SCREEN_ONLY_V1: system sleep is unported on this lab device.
 * Read-only after boot: a framework restart cannot remove this protection.
 * Display blanking and device runtime PM remain independent.
 */
static bool sq2_screen_only = true;
module_param_named(screen_only, sq2_screen_only, bool, 0444);
MODULE_PARM_DESC(screen_only, "Reject system suspend; display blanking only");

''' + anchor)
anchor = '''int pm_suspend(suspend_state_t state)
{
	int error;
'''
assert text.count(anchor) == 1
text = text.replace(anchor, anchor + '''
	if (sq2_screen_only) {
		pr_warn_ratelimited("SQ2 screen-only policy: system suspend blocked\\n");
		return -EOPNOTSUPP;
	}
''')
path.write_text(text)
print('Applied SQ2 screen-only suspend guard')
