#!/usr/bin/env python3
"""Fix final CTRL_INIT success boundary; retain only already-read boot results."""
from pathlib import Path
import argparse

def replace(s,a,b):
 if s.count(a)!=1:raise ValueError('Unknown boot-result anchor: '+repr(a))
 return s.replace(a,b,1)
def apply(directory):
 p=directory/'core.h';s=p.read_text()
 s=replace(s,' struct sq2_system_pm sq2_system_pm;\n bool sq2_no_tz_active;',
  ' struct sq2_system_pm sq2_system_pm;\n bool sq2_no_tz_active;\n'
  ' /* Existing CTRL read only; lifetime survives failed HFI destruction. */\n'
  ' u32 sq2_boot_last_ctrl, sq2_boot_last_count;\n'
  ' int sq2_boot_last_ret;\n'
  ' bool sq2_boot_last_valid;')
 p.write_text(s)
 p=directory/'hfi_venus.c';s=p.read_text()
 s=replace(s,'\twritel(BIT(VIDC_CTRL_INIT_CTRL_SHIFT), cpu_cs_base + VIDC_CTRL_INIT);',
  '#ifdef SQ2_VENUS_SYSTEM_PM\n'
  '\tif (IS_SQ2_HFI(hdev->core))\n'
  '\t\tWRITE_ONCE(hdev->core->sq2_boot_last_valid, false);\n'
  '#endif\n\n'
  '\twritel(BIT(VIDC_CTRL_INIT_CTRL_SHIFT), cpu_cs_base + VIDC_CTRL_INIT);')
 s=replace(s,'\tif (count >= max_tries)\n\t\tret = -ETIMEDOUT;',
  '\t/* A successful final read is still success even after count increments. */\n'
  '\tif (count >= max_tries && !ctrl_status)\n\t\tret = -ETIMEDOUT;')
 anchor='\t\twritel(0x0, cpu_cs_base + CPU_CS_X2RPMH_V6);\n\t}\n\n\treturn ret;\n}\n\nstatic u32 venus_hwversion'
 s=replace(s,anchor,
  '\t\twritel(0x0, cpu_cs_base + CPU_CS_X2RPMH_V6);\n\t}\n\n'
  '#ifdef SQ2_VENUS_SYSTEM_PM\n'
  '\tif (IS_SQ2_HFI(hdev->core)) {\n'
  '\t\t/* Do not re-read MMIO. Preserve the actual last sample and original\n'
  '\t\t * loop counter (error-break does not increment it) before hdev dies. */\n'
  '\t\tWRITE_ONCE(hdev->core->sq2_boot_last_ctrl, ctrl_status);\n'
  '\t\tWRITE_ONCE(hdev->core->sq2_boot_last_count, count);\n'
  '\t\tWRITE_ONCE(hdev->core->sq2_boot_last_ret, ret);\n'
  '\t\tWRITE_ONCE(hdev->core->sq2_boot_last_valid, true);\n'
  '\t\tif (ret)\n'
  '\t\t\tdev_err(dev, "SQ2_HFI_BOOT_RESULT ctrl=%08x count=%u ret=%d\\n",\n'
  '\t\t\t\tctrl_status, count, ret);\n'
  '\t}\n'
  '#endif\n\n'
  '\treturn ret;\n}\n\nstatic u32 venus_hwversion')
 p.write_text(s)
 p=directory/'venus-sq2-system-pm.h';s=p.read_text()
 s=replace(s,'vb2_bytes=%llu stops=%lu starts=%lu\\n",',
  'vb2_bytes=%llu stops=%lu starts=%lu boot_valid=%d boot_ctrl=%08x boot_count=%u boot_ret=%d\\n",')
 s=replace(s,'  core->sq2_system_pm.stops, core->sq2_system_pm.starts);',
  '  core->sq2_system_pm.stops, core->sq2_system_pm.starts,\n'
  '  READ_ONCE(core->sq2_boot_last_valid), READ_ONCE(core->sq2_boot_last_ctrl),\n'
  '  READ_ONCE(core->sq2_boot_last_count), READ_ONCE(core->sq2_boot_last_ret));')
 p.write_text(s)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args();apply(a.directory)
