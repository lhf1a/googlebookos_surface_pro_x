#!/usr/bin/env python3
"""Add explicit idle-only no-TZ system PM to a freshly prepared module copy."""
from pathlib import Path
import argparse,shutil
def replace(s,a,b):
 if s.count(a)!=1:raise ValueError('Unknown Venus PM source anchor: '+repr(a))
 return s.replace(a,b,1)
def apply(directory,root):
 p=directory/'core.h';s=p.read_text()
 s=replace(s,'struct venus_core {','#include "sq2-venus-system-pm.h"\nstruct venus_core {\n struct sq2_system_pm sq2_system_pm;\n bool sq2_no_tz_active;')
 p.write_text(s)
 p=directory/'core.c';s=p.read_text()
 s=replace(s,'#include "venus-sq2-core.h"','static void venus_sq2_pm_publish(struct venus_core *core);\n#include "venus-sq2-core.h"\n#include "venus-sq2-system-pm.h"')
 s=replace(s,'SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,\n\t\t\t\tpm_runtime_force_resume)','SET_SYSTEM_SLEEP_PM_OPS(venus_sq2_system_suspend,\n\t\t\t\tvenus_sq2_system_resume)')
 # Keep the independent runtime-PM EBUSY protection verbatim.
 if 'if (IS_SQ2_HFI(core))\n\t\treturn -EBUSY;' not in s:raise ValueError('Runtime resident guard lost')
 p.write_text(s)
 p=directory/'venus-sq2-core.h';s=p.read_text()
 s=replace(s,' if (ret) goto fail;\n return 0;\nfail:',
  ' if (ret) goto fail;\n venus_sq2_pm_publish(core);\n return 0;\nfail:')
 p.write_text(s)
 p=directory/'hfi.c';s=p.read_text()
 s=replace(s,'\tif (test_bit(0, &inst->core->sys_error)) {\n\t\tret = -EIO;\n\t\tgoto unlock;\n\t}\n',
  '\tif (test_bit(0, &inst->core->sys_error)) {\n\t\tret = -EIO;\n\t\tgoto unlock;\n\t}\n'
  '#ifdef SQ2_VENUS_SYSTEM_PM\n'
  '\t/* Pair with the PM second idle check: no new session after STOPPING. */\n'
  '\tif (IS_SQ2_HFI(core) && READ_ONCE(core->sq2_system_pm.phase) != SQ2_PM_RUNNING) {\n'
  '\t\tret = -EBUSY;\n\t\tgoto unlock;\n\t}\n#endif\n')
 p.write_text(s)
 for n in ['sq2-venus-system-pm.h','venus-sq2-system-pm.h']:
  shutil.copyfile(root/'probe/video'/n,directory/n)
 p=directory/'Makefile';s=p.read_text()
 if 'SQ2_VENUS_SYSTEM_PM' in s:raise ValueError('System PM already applied')
 p.write_text(s+'\nccflags-y += -DSQ2_VENUS_SYSTEM_PM\n')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);a=p.parse_args();apply(a.directory,a.root)
