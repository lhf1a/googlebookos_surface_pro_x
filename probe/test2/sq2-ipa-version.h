/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_IPA_VERSION_H
#define SQ2_IPA_VERSION_H
/* These bit slices are independently verified in the original ARM64 driver.
 * This decoder does not imply a Linux IPA register/profile compatibility. */
static inline int sq2_ipa_version_valid(unsigned int raw)
{
	return raw != 0 && raw != ~0U && (raw >> 28) >= 3 && (raw >> 28) <= 7;
}
static inline unsigned int sq2_ipa_major(unsigned int raw) { return raw >> 28; }
static inline unsigned int sq2_ipa_minor(unsigned int raw) { return (raw >> 16) & 0xfff; }
static inline unsigned int sq2_ipa_revision(unsigned int raw) { return raw & 0xffff; }
#endif
