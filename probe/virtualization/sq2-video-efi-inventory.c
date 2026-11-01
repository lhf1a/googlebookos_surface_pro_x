// SPDX-License-Identifier: BSD-3-Clause
/* Read-only Clock protocol name lookup, then chain the unchanged EL2 launcher.
 * ABI prefix checked against Carina ClockDxe 0x10006 and Qualcomm EFIClock.h.
 * No clock/power/reset/SCM operation is exposed by this probe.
 */
#include <efi.h>
#include <efilib.h>

struct sq2_clock;
typedef EFI_STATUS (EFIAPI *lookup_id)(struct sq2_clock *, const CHAR8 *, UINTN *);
struct sq2_clock {
    UINT64 version;
    lookup_id clock_id;
    VOID *unused[8];
    lookup_id power_id;
};
static const char *clocks[] = {
    "gcc_video_axic_clk", "gcc_video_axi0_clk", "gcc_video_axi1_clk",
    "video_cc_iris_ahb_clk", "video_cc_mvsc_core_clk",
    "video_cc_mvs0_core_clk", "video_cc_mvs1_core_clk",
    "sq2_nonexistent_clock_negative_control"
};
static const char *domains[] = {
    "mvsc_gdsc", "mvs0_gdsc", "mvs1_gdsc",
    "sq2_nonexistent_domain_negative_control"
};
static char report[2048];
static UINTN used;

static BOOLEAN append(const char *text)
{
    UINTN len = 0;
    while (text[len]) len++;
    if (len >= sizeof(report) - used) return FALSE;
    for (UINTN i = 0; i < len; i++) report[used + i] = text[i];
    used += len;
    report[used] = 0;
    return TRUE;
}

static BOOLEAN hex(UINT64 value)
{
    char text[17];
    UINTN i;
    for (i = 0; i < 16; i++)
        text[i] = "0123456789abcdef"[(value >> (60 - i * 4)) & 15];
    text[16] = 0;
    return append(text);
}

static EFI_STATUS collect(struct sq2_clock *clock)
{
    UINTN i, id;
    EFI_STATUS status;
    used = 0;
    if (!append("SQ2 EFI clock name inventory; NO POWER OR SCM CALLS\nversion=") ||
        !hex(clock->version) || !append("\n")) return EFI_BUFFER_TOO_SMALL;
    if (clock->version != 0x10006 || !clock->clock_id || !clock->power_id) {
        append("Unsupported protocol; no lookup performed\n");
        return EFI_UNSUPPORTED;
    }
    for (i = 0; i < sizeof(clocks) / sizeof(clocks[0]); i++) {
        id = ~(UINTN)0;
        status = clock->clock_id(clock, (const CHAR8 *)clocks[i], &id);
        if (!append(clocks[i]) || !append(" status=") || !hex(status) ||
            !append(" id=") || !hex(id) || !append("\n")) return EFI_BUFFER_TOO_SMALL;
    }
    for (i = 0; i < sizeof(domains) / sizeof(domains[0]); i++) {
        id = ~(UINTN)0;
        status = clock->power_id(clock, (const CHAR8 *)domains[i], &id);
        if (!append(domains[i]) || !append(" status=") || !hex(status) ||
            !append(" id=") || !hex(id) || !append("\n")) return EFI_BUFFER_TOO_SMALL;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS inventory(EFI_HANDLE device)
{
    EFI_GUID guid = {0x241afae6, 0x885f, 0x4f6c, {0xa7,0xea,0xc2,0x8e,0xab,0x79,0xc3,0xe5}};
    struct sq2_clock *clock = NULL;
    EFI_FILE_HANDLE root, file;
    EFI_STATUS status, close_status;
    UINTN size;
    CHAR16 *name = L"\\EFI\\SQ2VideoInventory\\clock-inventory.txt";
    root = LibOpenRoot(device);
    if (!root) return EFI_NOT_FOUND;
    status = root->Open(root, &file, name, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
        file->Close(file);
        root->Close(root);
        return EFI_ALREADY_STARTED; /* Preserve the first report. */
    }
    if (status != EFI_NOT_FOUND) { root->Close(root); return status; }
    status = LibLocateProtocol(&guid, (VOID **)&clock);
    if (!EFI_ERROR(status) && clock) {
        status = collect(clock);
        Print(L"Clock protocol inventory: %r\n", status);
    } else {
        used = 0;
        append("Clock protocol unavailable; status="); hex(status); append("\n");
    }
    Print(L"%a", report);
    status = root->Open(root, &file, name,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    root->Close(root);
    if (EFI_ERROR(status)) return status;
    size = used;
    status = file->Write(file, &size, report);
    if (!EFI_ERROR(status) && size != used) status = EFI_DEVICE_ERROR;
    if (!EFI_ERROR(status)) status = file->Flush(file);
    close_status = file->Close(file);
    return EFI_ERROR(status) ? status : close_status;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE *loaded;
    EFI_DEVICE_PATH *path;
    EFI_HANDLE next;
    EFI_STATUS status;
    InitializeLib(image, st);
    status = BS->HandleProtocol(image, &loaded_guid, (VOID **)&loaded);
    if (EFI_ERROR(status)) return status;
    status = inventory(loaded->DeviceHandle);
    Print(L"SQ2 read-only video clock inventory saved: %r\n", status);
    path = FileDevicePath(loaded->DeviceHandle, L"\\EFI\\SQ2EL2Android\\el2launch.efi");
    if (!path) return EFI_OUT_OF_RESOURCES;
    status = BS->LoadImage(FALSE, image, path, NULL, 0, &next);
    FreePool(path);
    if (EFI_ERROR(status)) return status;
    return BS->StartImage(next, NULL, NULL);
}
