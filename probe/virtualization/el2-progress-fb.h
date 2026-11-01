/* SPDX-License-Identifier: BSD-3-Clause */
/* Diagnostic only: GOP is captured before EBS; later writes use no EFI calls.
 * Top-left: two hex stage digits, then a 64-bit value in hex.
 */
static volatile UINT32 *sq2_progress_fb;
static UINT32 sq2_progress_stride;

static void sq2_progress_init(void)
{
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    if (EFI_ERROR(LibLocateProtocol(&guid, (void **)&gop)) || !gop->Mode ||
        !gop->Mode->Info || gop->Mode->Info->HorizontalResolution < 512 ||
        gop->Mode->Info->VerticalResolution < 48 ||
        (gop->Mode->Info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
         gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor) ||
        gop->Mode->FrameBufferSize < (UINT64)gop->Mode->Info->PixelsPerScanLine * 48 * 4)
        return;
    sq2_progress_fb = (volatile UINT32 *)(UINTN)gop->Mode->FrameBufferBase;
    sq2_progress_stride = gop->Mode->Info->PixelsPerScanLine;
}

static void sq2_progress_mark(UINT8 stage, UINT64 value)
{
    static const UINT8 digits[16][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}
    };
    if (!sq2_progress_fb) return;
    for (UINT32 y = 0; y < 40; y++)
        for (UINT32 x = 0; x < 480; x++)
            sq2_progress_fb[y * sq2_progress_stride + x] = 0;
    for (UINT32 n = 0; n < 18; n++) {
        UINT8 digit = n < 2 ? (stage >> (4 * (1 - n))) & 15 :
            (value >> (4 * (17 - n))) & 15;
        UINT32 left = 4 + n * 24 + (n >= 2 ? 16 : 0);
        for (UINT32 y = 0; y < 7; y++)
            for (UINT32 x = 0; x < 5; x++)
                if (digits[digit][y] & (1 << (4 - x)))
                    for (UINT32 dy = 0; dy < 4; dy++)
                        for (UINT32 dx = 0; dx < 4; dx++)
                            sq2_progress_fb[(4 + y * 4 + dy) * sq2_progress_stride +
                                            left + x * 4 + dx] = 0xffffff;
    }
    __asm__ volatile("dsb sy" ::: "memory");
}
