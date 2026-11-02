// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include "../probe/test2/sq2-ipa-gsi-read.h"
int main(void)
{
 unsigned int raw=(16U<<24)|(2U<<20)|(7U<<14)|(3U<<8)|2U;
 assert(sq2_gsi_state(raw)==2 && sq2_gsi_chid(raw)==3 && sq2_gsi_ee(raw)==0 && sq2_gsi_event(raw)==7);
 assert(sq2_gsi_state(0)==0 && sq2_gsi_state(~0U)==15);
 assert(sq2_gsi_ap_offset(0)==0x180 && sq2_gsi_ap_offset(3)==0x300);
 puts("GSI_DECODE_PASS: AP3..6 offsets, state/chid/ee/event bitfields; no hardware operation");
 return 0;
}
