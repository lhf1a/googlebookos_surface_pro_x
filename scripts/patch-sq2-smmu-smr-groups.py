#!/usr/bin/env python3
"""Backport upstream 5583a55e074b: preserve extended SMRs on bare metal."""
import hashlib
from pathlib import Path
import sys

path = Path(sys.argv[1]) / 'drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c'
source = path.read_text()
marker = 'SQ2_SMMU_EXTENDED_SMR_UPSTREAM_5583a55e074b'
if marker in source:
    print('Already patched')
    raise SystemExit(0)
if hashlib.sha256(path.read_bytes()).hexdigest() != \
        'de9e5cb3d8bd0d19de90b01ec02c14c5adfe9c5520600c420ff709a5e899ba8a':
    raise ValueError('Unexpected arm-smmu-qcom source; do not patch blindly')
old = '''	/*
	 * Some platforms support more than the Arm SMMU architected maximum of
	 * 128 stream matching groups. For unknown reasons, the additional
	 * groups don't exhibit the same behavior as the architected registers,
	 * so limit the groups to 128 until the behavior is fixed for the other
	 * groups.
	 */
	if (smmu->num_mapping_groups > 128) {
		dev_notice(smmu->dev, "\\tLimiting the stream matching groups to 128\\n");
		smmu->num_mapping_groups = 128;
	}

	last_s2cr = ARM_SMMU_GR0_S2CR(smmu->num_mapping_groups - 1);'''
new = '''	/* SQ2_SMMU_EXTENDED_SMR_UPSTREAM_5583a55e074b:
	 * The hypervisor does not emulate extra groups correctly. Probe the
	 * reliable S2CR127 first; bare metal must preserve firmware's extra
	 * groups so a new consumer cannot create a duplicate stream match.
	 */
	if (smmu->num_mapping_groups > 128)
		last_s2cr = ARM_SMMU_GR0_S2CR(127);
	else
		last_s2cr = ARM_SMMU_GR0_S2CR(smmu->num_mapping_groups - 1);'''
assert source.count(old) == 1
source = source.replace(old, new)
old = '''		arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBAR(qsmmu->bypass_cbndx), reg);
	}

	for (i = 0; i < smmu->num_mapping_groups; i++) {'''
new = '''		arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBAR(qsmmu->bypass_cbndx), reg);
		if (smmu->num_mapping_groups > 128) {
			dev_notice(smmu->dev, "\\tLimiting the stream matching groups to 128\\n");
			smmu->num_mapping_groups = 128;
		}
	}

	for (i = 0; i < smmu->num_mapping_groups; i++) {'''
assert source.count(old) == 1
source = source.replace(old, new)
assert source.count('smmu->num_mapping_groups = 128;') == 1
path.write_text(source)
print('Backported upstream 5583a55e074b; hypervisor workaround retained')
