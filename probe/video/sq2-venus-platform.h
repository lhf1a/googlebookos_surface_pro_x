/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_VENUS_PLATFORM_H
#define SQ2_VENUS_PLATFORM_H
struct platform_device;
struct venus_core;
struct sq2_venus_platform;
/* Explicit ownership, never devm. Caller serializes transitions. IRQ starts
 * disabled, and may only be enabled after core/HFI initialization and power-on.
 */
int sq2_venus_platform_alloc(struct platform_device *pdev,
                            struct sq2_venus_platform **out);
struct venus_core *sq2_venus_platform_core(struct sq2_venus_platform *owner);
int sq2_venus_platform_irq(struct sq2_venus_platform *owner, bool enable);
int sq2_venus_platform_secure_begin(struct sq2_venus_platform *owner);
int sq2_venus_platform_power_on(struct sq2_venus_platform *owner);
int sq2_venus_platform_power_off(struct sq2_venus_platform *owner);
int sq2_venus_platform_secure_end(struct sq2_venus_platform *owner, int pas_status);
/* Release requires confirmed PAS stop AND prior HFI/firmware cleanup.
 * Power/reset/ICC owner must also be released before this final operation.
 */
int sq2_venus_platform_release(struct sq2_venus_platform **owner);
#endif
