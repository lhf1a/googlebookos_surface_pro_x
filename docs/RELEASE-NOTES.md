# Release Notes

Prebuilt disk images (`*.img.gz`), ESP OTA packages (`*-ota-esp-*.tar.gz`), and `SHA256SUMS` are published under [GitHub Releases](https://github.com/lhf1a/googlebookos_surface_pro_x/releases), [Google Drive](https://drive.google.com/drive/folders/1uVAA6UnydSyUHQAO8Y5rTm8SxZ62Zykr), and [Baidu Netdisk](https://pan.baidu.com/s/51GjLdmTGir0PelomZvboYw).

---

## v0.9.3 (2026-11-24)

- **GNSS / GPS XTRA3 PSDS Injection & 5-Constellation Convergence**: Added QMI LOC `0x00A7` XTRA3 PSDS chunk injection (`xtra3grcej.bin`, 168h validity via `0x0037` + UTC injection via `0x0038`) and Qualcomm XTRA3 server config bind-mount; mapped all 5 constellations (GPS, GLONASS, Galileo, BeiDou `system=6`, QZSS `system=7`, plus SBAS/NavIC) across 6 signal types, tracked `USED_IN_FIX` per `(constellation, svid)`, and tightened `IN_PROGRESS` horizontal uncertainty filtering to `<= 500 m` with `>= 4` SVs.
- **Auto Lower USB-C Role Switching & External DP `s2idle` Resume**: Set lower USB-C default to `auto` with event-driven `spx-lower-usb-watch` (automatic host/ADB gadget switching), Advanced Settings controls (`Auto` / `Host` / `ADB`), and `spx-typec` role serialization / `-ETIMEDOUT` retry / pre-KMS QMP mux sync; fixed external DisplayPort post-`s2idle` resume stalls in `patches/spx-dp-s2idle.patch` with deferred HPD retraining; localized `SurfaceDisplaySettings` (EN/ZH) and defaulted build parallelism to `max(1, nproc - 1)`.
- **SC8180X `s2idle` Hang Prevention & Scheduler/IRQ Performance**: Added kernel patches `0013`–`0016` (`mm0` keepalive, GCC RPM, UCSI suspend work cancel, SPMI PDC wake DT patching); enabled `CONFIG_PREEMPT`, `CONFIG_NO_HZ_IDLE`, `CONFIG_HZ_250`, `CONFIG_UCLAMP_TASK`, `CONFIG_CPU_IDLE_GOV_TEO`, and `CONFIG_QCOM_ICC_BWMON`; added portable `spx-irq-balance-boot` spreading high-rate GICv3 IRQs across little cores `cpu1..cpu3` and `vermagic` ABI detection for bundled `device_faker` LKM across kernel upgrades.
- **IR Face Unlock Live Enroll UI & Multi-Orientation Unlock**: Added Settings RRO `face_enrolling` layout (`org.surface.face.overlay` v2) with live IR preview ring, bilingual copy, and silent post-collection template verification; added gravity-backed `SpxOrientationMonitor` + multi-orientation fallback (single enrollment unlocks across Landscape `0°`, Volume-at-TOP `270°`, and Power-at-TOP `90°`) with improved side-bezel yaw/lighting NIR detection and liveness checks.
- **AVF VM Host VPN Sharing & OTA Keep-Data APK Cache Refresh**: Added zero-idle-overhead `NETLINK_ROUTE` watcher (`spx-avf-vpn-watch`) and generic reconciler (`spx-avf-vpn-reconcile`) to route AVF VM traffic through any active host VPN (default off in Advanced Settings, with BusyBox awk DNS parsing, stale `tc` offload detach, and high-priority return rules); avoided `1970-01-01` APK staging timestamps and cleared `/data/system/package_cache` + stale vendor APK/overlay `dalvik-cache` on both online and offline keep-data OTA updates.
- **Bilingual Docs & Repo Cleanup**: Completed full EN / ZH-CN documentation under `docs/`, fixed README links, and removed archived `reference/` binaries.

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
