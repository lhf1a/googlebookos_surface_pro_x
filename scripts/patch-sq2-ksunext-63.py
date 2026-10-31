#!/usr/bin/env python3
"""Compile adaptation for pinned Next legacy; does not establish runtime safety."""
from pathlib import Path
import sys

p = Path(sys.argv[1])
kbuild = p / 'Kbuild'
s = kbuild.read_text()
# git rev-list --count at 9b08e888 is 2979; upstream formula is 30000+count+150.
s = s.replace('KSU_VERSION_FALLBACK := 1\n', 'KSU_VERSION_FALLBACK := 33129\n')
s = s.replace('KSU_VERSION_TAG_FALLBACK := v0.0.1\n', 'KSU_VERSION_TAG_FALLBACK := v3.2.0-legacy\n')
kbuild.write_text(s)
h = p / 'selinux/sepolicy.h'
s = h.read_text()
anchor = '#define __KSU_H_SEPOLICY'
assert s.count(anchor) == 1
addition = '''
/* SQ2 Linux 6.3 uses struct selinux_policy (introduced in Linux 5.10). */
struct selinux_policy;
struct selinux_policy *ksu_dup_sepolicy(struct selinux_policy *old_pol);
void ksu_destroy_sepolicy(struct selinux_policy *pol);
'''
if '/* SQ2 Linux 6.3' not in s:
    h.write_text(s.replace(anchor, anchor + addition))
c = p / 'selinux/sepolicy.c'
s = c.read_text()
if not s.startswith('#define SELINUX_POLICY_INSTEAD_SELINUX_SS'):
    c.write_text('#define SELINUX_POLICY_INSTEAD_SELINUX_SS\n' + s)
