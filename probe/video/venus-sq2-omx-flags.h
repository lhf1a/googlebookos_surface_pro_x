/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_OMX_FLAGS_H
#define VENUS_SQ2_OMX_FLAGS_H
/* Downstream 0x20000 overlaps upstream timestamp-source bits. */
#define SQ2_V4L2_BUF_FLAG_CODECCONFIG 0x01000000U
static inline u32 sq2_omx_input_flags(u32 flags)
{
 return flags & SQ2_V4L2_BUF_FLAG_CODECCONFIG ? HFI_BUFFERFLAG_CODECCONFIG : 0;
}
#endif
