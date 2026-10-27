#!/usr/bin/env python3
"""Bounded 3DMark workaround for SQ2/A680.1 GMEM corruption in Mesa 26.2.4.

This selects existing sysmem rendering; it is not a GMEM root-cause fix.
Other processes/GPUs retain normal heuristics. TU_DEBUG=gmem opts out for research.
"""
from pathlib import Path
import hashlib, sys

p = Path(sys.argv[1]) / 'src/freedreno/vulkan/tu_cmd_buffer.cc'
original_hash = 'd61e9642c7daadd6144fbbe20d9f1fe6d1b3f51db64d2c63da886a3182df4fe8'
s = p.read_text()
include = '#include "util/u_process.h"\n'
anchor = '   /* can\'t fit attachments into gmem */'
block = '''   /* SQ2_A680_1_3DMARK_SYSMEM: bounded compatibility workaround.
    * Wild Life misrenders in GMEM at both 514 and 661 MHz; sysmem produces
    * correct captured frames. Do not infer a hardware or UBWC root cause.
    * Keep other applications on their normal path while GMEM is investigated.
    */
   static const bool sq2_3dmark = []() {
      const char *name = util_get_process_name();
      return name && !strcmp(name, "com.futuremark.dmandroid.application");
   }();
   if (sq2_3dmark &&
       cmd->device->physical_device->dev_id.chip_id == 0x06080001 &&
       !TU_DEBUG(GMEM)) {
      cmd->state.rp.gmem_disable_reason = "SQ2 A680.1 3DMark compatibility";
      return true;
   }

'''
if 'SQ2_A680_1_3DMARK_SYSMEM' in s:
    assert s.count(block) == 1 and s.count(include) == 1
    original = s.replace(block, '').replace(include, '')
    assert hashlib.sha256(original.encode()).hexdigest() == original_hash
    print('SQ2 Turnip GMEM workaround already applied')
else:
    assert hashlib.sha256(s.encode()).hexdigest() == original_hash, 'Unexpected Mesa source'
    assert s.count(anchor) == 1
    s = s.replace('#include "tu_cmd_buffer.h"\n', '#include "tu_cmd_buffer.h"\n'+include)
    p.write_text(s.replace(anchor, block+anchor))
    print('SQ2 Turnip GMEM workaround applied')
