/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_VENUS_POWER_H
#define SQ2_VENUS_POWER_H
struct device;
struct sq2_venus_power;
bool sq2_venus_power_ready(struct sq2_venus_power *owner);
/* On error *out may remain owned; caller must run off before release. */
int sq2_venus_power_on(struct device *dev, void __iomem *base, struct sq2_venus_power **out);
int sq2_venus_power_off(struct sq2_venus_power **owner, bool firmware_attempted);
#endif
