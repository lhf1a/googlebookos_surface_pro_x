/* SPDX-License-Identifier: BSD-3-Clause
 * Appended to pinned slbounce bounce_main.c in an isolated output source tree.
 */
static EFI_STATUS stage_error(CHAR16 *stage, EFI_STATUS status)
{
    Print(L"EL2 TEST ERROR [%s]: %r (0x%lx)\n", stage, status, status);
    Print(L"No Linux test result. Returning to GRUB in 20 seconds.\n");
    uefi_call_wrapper(BS->Stall, 1, 20000000);
    return status;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    EFI_STATUS ret;
    EFI_FILE_HANDLE volume, file;
    EFI_LOADED_IMAGE *loaded;
    EFI_HANDLE kernel;
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_DEVICE_PATH *path;
    EFI_GUID dt_guid = EFI_DTB_TABLE_GUID;
    EFI_PHYSICAL_ADDRESS dt = 0;
    UINT64 size;
    static CHAR16 options[] = L"console=tty0 earlycon=efifb keep_bootcon loglevel=7 acpi=off efi=novamap maxcpus=1 clk_ignore_unused pd_ignore_unused suspend.screen_only=0 nohibernate panic=15 rdinit=/init initrd=\\EFI\\SQ2EL2KVM\\initramfs.cpio.gz";
    InitializeLib(image, st);
    Print(L"SQ2 isolated EL2 KVM test. No disks or Android.\n");
    Print(L"CurrentEL=%d\n", read_currentel().el);
    if (read_currentel().el != 1) return stage_error(L"entry EL", EFI_UNSUPPORTED);
    volume = GetVolume(image);
    if (!volume) return stage_error(L"filesystem", EFI_NOT_FOUND);
    file = FileOpen(volume, L"\\EFI\\SQ2EL2KVM\\minimal-el2.dtb");
    if (!file) return stage_error(L"open DTB", EFI_NOT_FOUND);
    size = FileSize(file);
    if (size < 40 || size > 4*1024*1024) return stage_error(L"DTB size", EFI_INVALID_PARAMETER);
    ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData,
                           (size + 4095) / 4096, &dt);
    if (EFI_ERROR(ret)) return stage_error(L"DTB allocation", ret);
    if (FileRead(file, (UINT8 *)dt, size) != size || fdt_check_header((void *)dt) ||
        fdt_totalsize((void *)dt) > size) return stage_error(L"DTB read/header", EFI_INVALID_PARAMETER);
    FileClose(file);
    ret = uefi_call_wrapper(BS->InstallConfigurationTable, 2, &dt_guid, (void *)dt);
    if (EFI_ERROR(ret)) return stage_error(L"install DTB", ret);
    Print(L"Test DTB installed in EFI configuration table.\n");
    if (sl_is_allowed_by_fdt() != EFI_SUCCESS) {
        Print(L"EL2 DTB gate rejected; no takeover attempted.\n");
        return stage_error(L"DTB gate", EFI_UNSUPPORTED);
    }
    ret = uefi_call_wrapper(BS->HandleProtocol, 3, image, &loaded_guid, (void **)&loaded);
    if (EFI_ERROR(ret)) return stage_error(L"launcher handle", ret);
    path = FileDevicePath(loaded->DeviceHandle, L"\\EFI\\SQ2EL2KVM\\Image");
    if (!path) return stage_error(L"kernel path", EFI_OUT_OF_RESOURCES);
    ret = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image, path, NULL, 0, &kernel);
    FreePool(path);
    if (EFI_ERROR(ret)) return stage_error(L"kernel LoadImage", ret);
    Print(L"Kernel LoadImage succeeded.\n");
    ret = uefi_call_wrapper(BS->HandleProtocol, 3, kernel, &loaded_guid, (void **)&loaded);
    if (EFI_ERROR(ret)) return stage_error(L"kernel handle", ret);
    loaded->LoadOptions = options;
    loaded->LoadOptionsSize = sizeof(options);
    volume = GetVolume(image);
    if (!volume) return stage_error(L"TCB filesystem", EFI_NOT_FOUND);
    file = FileOpen(volume, L"\\EFI\\SQ2EL2KVM\\tcblaunch.exe");
    if (!file) return stage_error(L"open TCB", EFI_NOT_FOUND);
    ret = sl_install(file);
    if (EFI_ERROR(ret)) return stage_error(L"install SL hook", ret);
    Print(L"Starting minimal Linux with EL2 hook...\n");
    ret = uefi_call_wrapper(BS->StartImage, 3, kernel, NULL, NULL);
    Print(L"Kernel returned unexpectedly: %r; power-cycle before normal boot.\n", ret);
    for (;;) __asm__ volatile("wfe");
}
