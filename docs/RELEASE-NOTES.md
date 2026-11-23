# Release Notes

Prebuilt disk images (`*.img.gz`), ESP OTA packages (`*-ota-esp-*.tar.gz`), and `SHA256SUMS` are published under [GitHub Releases](https://github.com/lhf1a/googlebookos_surface_pro_x/releases), [Google Drive](https://drive.google.com/drive/folders/1uVAA6UnydSyUHQAO8Y5rTm8SxZ62Zykr), and [Baidu Netdisk](https://pan.baidu.com/s/51GjLdmTGir0PelomZvboYw).

---

## v0.9.0 (2026-11-21)

- **OV7251 850nm IR Face Unlock (Class 2) & Hardware Hall `SW_LID`**: Added `IFace` AIDL V4 HAL (`android.hardware.biometrics.face-service.default`) with HAL-side IR preview overlay (`spx-face-overlay.apk`) for frontal capture and fast lock-screen matching; wired TLMM GPIO 121 Hall `SW_LID` (`Lid Switch` for `touchpad-inhibitor`) alongside Type Cover state.
- **Gallery Video Playback & Hardware Encoding**: Fixed `MediaCodec.setOutputSurface()` `attachBuffer` failure by preserving `Surface::mGenerationNumber` across `Surface::disconnect()` in `libgui.so`; added Codec2 AIDL V2 Venus encoder HAL, Venus OUTPUT crop/NV12 alignment fixes, and Gralloc5 `0x10303` encoder usage.
- **Wi-Fi SoftAP Hotspot**: Implemented `nl80211` SoftAP, STA/AP concurrency, and Android 17 Wi-Fi HAL table ABI.
- **GNSS / GPS (`IGnss` AIDL V4 + `SpxGnssPsds`)**: Lazy QMI LOC V4 HAL over `AF_QIPCRTR` (zero idle polling for `s2idle`), auto-clearing MPSS MI engine lock on fix start and filtering coarse CELLID placeholders.
- **Camera, ISP, Sensors & SLPI Stability**: Added OV7251 850nm IR strobe, OV13858 HFLIP/VFLIP, multi-IFE CAMSS, refcounted privacy LEDs, CT-interpolated 3×3 CCM in `spx-isp`, dual-copy `mapper.quest2.so` install fix, `spx-tcs3430` 1x–64x auto-gain, SLPI gyro/mag (LSM6DSO/AK0991x), and SLPI crash fix (`DISABLE_REQ` + idle QRTR socket reuse).
- **Backlight, Display, Input & Hygiene**: Linear 0..511 9-bit LPG PWM table + `0.006` minimum brightness floor (eliminates blackout below 21%), DPU CRTC pending-frame race fix, `spi-hid` cold-boot hardening, Slim Pen 2 (MPP2) thresholds, Dell/SupportAssist donor cleanup, standard `zygote` shutdown path for internal-disk poweroff, and high-frequency log hygiene.
- **Directory Cleanup**: Moved user flashing/dual-boot/ADB-upgrade tools into [`tools/`](../tools/) and removed 209 unused legacy scripts from [`scripts/`](../scripts/).

---

## v0.8.0 (2026-11-20)

- **USB LiveCD -> Internal NVMe Installer**: Boot from an external 5-partition USB desktop and install to the internal NVMe SSD via **Advanced Settings -> Install to Internal Disk** (with SHA-256 verification and standalone UEFI boot).
- **Expanded Advanced Settings**: Added **Touch** (disable touchscreen, ghost-touch toggle, calibration import/export, seamless migration), **Debugging** (LAN/VM IP display, network ADB `5555` + lower-port bridge, initramfs Dropbear SSH `22` & `ssh-copy-id` `/root/.ssh` persistence), **System update**, and **Install to Internal Disk**.
- **On-Device OTA & PC ADB One-Click Upgrade**: Added `SystemUpdateActivity`, `/bin/spx-ota-tool`, and cross-platform upgrade scripts, preserving `/data`, calibration, and dual-boot `grub.cfg` by default.
- **GRUB Rescue Mode, KernelSU & Type-C DP 4K@60Hz `s2idle`**: Rescue Mode includes port `22` SSH, USB Ethernet, and `/bin/spx-wifi-tui`; updated KernelSU-Next `v3.4.2` + ZygiskNext `1.5.3` with `poipu` identity; enabled DP 1.4 HBR3 `4K@60Hz` and multi-cycle `s2idle` suspend/resume recovery.

---

## v0.7.5 (2026-11-19)

- **Advanced Settings (Initial)**: Material You app with system/per-app Turnip vs. QCOM Vulkan routing (protecting system and `systemui` on Mesa) and dynamically enumerated, persistent CPU governor selector.
- **Slim 5-Partition Layout & Windows Same-Disk Dual-Boot**: Compressed full disk image to 8 GiB 5-partition GPT (auto-expanding `userdata` on first boot) and added Windows same-disk dual-boot installer.
- **All-App `VM_BIND` & Media/Boot Fixes**: Enabled QCOM `VM_BIND` + cached submission by default for regular app UIDs; fixed OMX low-latency control, 48 kHz speaker buffer cadence, and generic portable boot/touch wizard.

---

## v0.7.0 (2026-11-17)

- Initial unified SQ1 / SQ2 Hybrid preview on Android 17 `mica 16471258` + Linux `6.18.60` with dual Type-C DP output, native multi-display settings tile, and lower-port wired USB ADB.
