# OTA & ADB One-Click Upgrade Architecture

## 1. Core Architecture and File Inventory

### 1.1 On-Device OTA Runtime (initramfs)
- `probe/ota/spx-ota-tool.c`: Zero-dependency C11 tool statically compiled as an ARM64 ELF, providing:
  - `sha256` (file / stdin)
  - `detect` (identifies `full-img` / `gzip` / `manifest`)
  - `inspect-gpt` (parses full disk image GPT for `super` and `SQ2PROBE` offsets and sizes)
  - `extract-stream` (single-pass streaming decompression/extraction with `super` and `esp.fat` SHA-256 calculation)
- `probe/ota/spx-ota-stage`: Early initramfs offline updater running after `prepare-native-ssd` discovers the disk and mounts `/media/esp`, before `sq2-loop-slice` mounts `super` (skipped in Rescue Mode `sq2.rescue=1`):
  - Consumes `/media/esp/SPX-OTA-PENDING` and atomically renames it to `SPX-OTA-INPROGRESS` to prevent reboot loops
  - Supports incremental/full `manifest` `.tar.gz` and full `.img` / `.img.gz`
  - Verifies package SHA-256, flashes `super` as needed, syncs ESP, and writes `SPX-OTA-RESULT.txt` and `SPX-OTA-LAST.log`
  - Supports `WIPE_DATA=0` (default, preserving `userdata` and touch calibration) or `WIPE_DATA=1` (clean install: wipes `userdata`, `metadata`, `misc`)
- `probe/ota/spx-ota-boot`: Background root OTA bridge daemon after Android boots:
  - Takes over `android.settings.SYSTEM_UPDATE_SETTINGS`, enabling `org.surface.advancedcontrol/.SystemUpdateActivity` and disabling conflicting update pages
  - Handles `ota-refresh` / `ota-inspect` / `ota-install` / `ota-cancel` / `ota-clear-result` / `ota-reboot`
  - Performs online atomic ESP updates when `manifest` has unchanged `super` and `WIPE_DATA=0`; otherwise stages `/data/ota/pending.pkg` and writes `SPX-OTA-PENDING` for offline update on reboot
- `scripts/full_image_ota.py`: Cross-compiles `spx-ota-tool`, writes `/bin/spx-ota-{tool,stage,boot}` and `/etc/spx-ota-info` (with current `super` SHA-256), and patches `/init`.
- `probe/test2/prepare-image-ssd`: Relaxes non-`userdata` partition size checks to `<`, allowing legacy 39-partition layouts to boot the 5-partition generic initramfs after upgrade.

### 1.2 System Update UI (`SurfaceAdvancedControl.apk`)
- `probe/advanced-control/SystemUpdateActivity.java`, `OtaClient.java`, `OtaPolicy.java`
- `probe/advanced-control/AndroidManifest.xml`: Declares `android.settings.SYSTEM_UPDATE_SETTINGS` (`priority="999"`)
- `probe/advanced-control/MainActivity.java`, `SettingsTheme.java`, `Strings.java`, `res/values*/strings.xml`: Adds System update entry, bilingual Chinese/English UI, and keep-data vs. clean-install selection with double confirmation.

### 1.3 Build and OTA Packaging Integration
- `scripts/build_ota_package.py`: Supports `--mode auto|delta|full`. Full builds generate an OTA package automatically; when `super` is unchanged relative to baseline, only changed ESP files and manifest descriptions (`ota-manifest.kv` + `ota-files.tsv`) are packed; when `super` changes or `--mode full` is passed, `super` and checksum info are included.
- `scripts/full_image.py` & `scripts/build-lts.py`: `build.py full-image` generates `ota/` artifacts by default, or run standalone via `python3 scripts/build.py ota ...`.

### 1.4 PC Root ADB One-Click Upgrade Scripts
- Windows PowerShell: `tools/Upgrade-GooglebookViaAdb.ps1` + launcher `tools/Upgrade-GooglebookViaAdb.cmd`
- Linux POSIX sh: `tools/upgrade-googlebook-via-adb.sh`
- Capabilities:
  - Supports `.img.gz` / `.img` / `.tar.gz`, auto-detecting package type and parsing GPT / manifest
  - Supports keeping data or clean install (`-WipeData` / `--wipe-data`)
  - Checks target device `super` capacity and `/data` free space; rejects ESP-only delta packages on legacy builds lacking `/etc/spx-ota-info`
  - Reuses already-verified `/data/ota/pending.pkg` on device, skipping duplicate `adb push` on `check-staged-pkg` hit
  - **Dual-boot-safe GRUB repair (`ensure_grub_boots_sq2nat`)**: Backs up and clears residual `EFI/BOOT/grubenv`; preserves Windows entries intact when `bootmgfw.efi` is present while updating to `/EFI/SQ2Nat/start.efi`; backs up single-OS legacy menus to `EFI/BOOT/grub.cfg.ota-bak` and updates to `/EFI/SQ2Nat/start.efi`.

---

## 2. Key Build Notes

- `--boot-gnu-efi` must point to compatible revision `aeee48d838d40e8ef787748d5dc066ea2fb99dbe` to avoid PE32+ shim magic errors from older `gnu-efi`.
- Keep Turnip runtime libraries clean (`vulkan.freedreno.so`, `libdrm.so`), and pass the signing keystore and full camera/sensor input directories.
