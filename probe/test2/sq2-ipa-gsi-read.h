/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_IPA_GSI_READ_H
#define SQ2_IPA_GSI_READ_H
/* GSI2.0/IPA4.1 CH_C_CNTXT_0, matched to Linux reg/gsi_reg-v4.0.c
 * and original Windows address calculation. EE_AP=0; fixed channels3..6 only. */
static inline unsigned int sq2_gsi_state(unsigned int raw) { return (raw >> 20) & 15; }
static inline unsigned int sq2_gsi_chid(unsigned int raw) { return (raw >> 8) & 31; }
static inline unsigned int sq2_gsi_ee(unsigned int raw) { return (raw >> 4) & 15; }
static inline unsigned int sq2_gsi_event(unsigned int raw) { return (raw >> 14) & 31; }
static inline unsigned long sq2_gsi_ap_offset(unsigned int index) { return 0x80UL * (index + 3); }
#endif
