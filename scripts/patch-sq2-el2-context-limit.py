#!/usr/bin/env python3
"""Opt-in SC8180X apps SMMU IRQ-backed CB pool; never changes normal DT policy."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[1]
k = Path(sys.argv[1])
p = k/'drivers/iommu/arm/arm-smmu/arm-smmu.c'
s = p.read_text()
marker = 'SQ2_EL2_IRQ_BACKED_CONTEXT_POOL'
if marker in s:
    (p.parent/'sq2-el2-context-limit.h').write_bytes((root/'probe/virtualization/sq2-el2-context-limit.h').read_bytes().replace(b'\r\n',b'\n'))
    print('Already patched')
    raise SystemExit(0)
anchor = '#include "arm-smmu.h"'
assert s.count(anchor) == 1
s = s.replace(anchor, anchor+'\n#ifdef CONFIG_ARM64\n#include <asm/virt.h>\n#include "sq2-el2-context-limit.h"\n#endif')
old = '''		if (smmu->num_context_banks > smmu->num_context_irqs) {
			dev_err(dev,
			      "found only %d context irq(s) but %d required\\n",
			      smmu->num_context_irqs, smmu->num_context_banks);
			return -ENODEV;
		}

		/* Ignore superfluous interrupts */
		smmu->num_context_irqs = smmu->num_context_banks;'''
new = '''		if (smmu->num_context_banks > smmu->num_context_irqs) {
#ifdef CONFIG_ARM64
			/* SQ2_EL2_IRQ_BACKED_CONTEXT_POOL: the experimental DT
			 * only describes IRQs for CB0..105. Keep the physical CB
			 * count for allocation sizing/reset, but reserve CB106..126
			 * permanently so no domain can select an unhandled IRQ.
			 * Keep translated domains and the normal strict check.
			 */
			if (sq2_el2_context_limit_allowed(
				    of_property_read_bool(dev->of_node,
					"microsoft,el2-irq-backed-context-pool"),
				    is_hyp_mode_available(),
				    of_device_is_compatible(dev->of_node,
					"qcom,sc8180x-smmu-500"),
				    smmu->ioaddr, smmu->num_context_banks,
				    smmu->num_context_irqs, smmu->num_s2_context_banks)) {
				bitmap_set(smmu->context_map, smmu->num_context_irqs,
					   smmu->num_context_banks - smmu->num_context_irqs);
				dev_warn(dev, "SQ2 EL2 experiment: CB allocation limited to %u IRQ-backed banks; physical reset count %u\\n",
					 smmu->num_context_irqs, smmu->num_context_banks);
			} else
#endif
			{
				dev_err(dev,
				      "found only %d context irq(s) but %d required\\n",
				      smmu->num_context_irqs, smmu->num_context_banks);
				return -ENODEV;
			}
		} else {
			/* Ignore superfluous interrupts */
			smmu->num_context_irqs = smmu->num_context_banks;
		}'''
assert s.count(old) == 1
p.write_text(s.replace(old,new))
(p.parent/'sq2-el2-context-limit.h').write_bytes((root/'probe/virtualization/sq2-el2-context-limit.h').read_bytes().replace(b'\r\n',b'\n'))
print('Patched scoped experimental IRQ-backed context allocation pool')
