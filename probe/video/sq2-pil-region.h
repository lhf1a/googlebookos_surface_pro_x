/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_PIL_REGION_H
#define SQ2_PIL_REGION_H
#include <linux/types.h>
/* Read-only allowed window, never an ownership grant. */
int sq2_pil_get_bounds(phys_addr_t *start, phys_addr_t *end);
#endif
