/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_VENUS_FW_H
#define SQ2_VENUS_FW_H
struct device;
struct sq2_venus_fw;
/* Independent owner survives loss of a core/device pointer on failed probe.
 * On failure *out can be non-NULL: allocation is quarantined until reboot.
 * Does not handle HFI/IRQ/clock lifetime; callers must retain those separately.
 */
int sq2_venus_fw_boot(struct device *dev, struct sq2_venus_fw **out, bool pil_gap);
int sq2_venus_fw_shutdown(struct sq2_venus_fw **owner);
int sq2_venus_fw_probe_gap(void);
#endif
