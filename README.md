项目标准构建入口：`scripts/build.py`，生成单个 SQ1/SQ2 通用镜像；默认设置可自定义。见 [标准构建](docs/STANDARD-BUILD.md)。

# Googlebook OS on Surface Pro X SQ2

Unified SQ1/SQ2 offline build: [profiles, fixed inputs and limits](docs/UNIFIED-SQ1-SQ2.md). SQ1 hardware compatibility is not validated.

**English** | [简体中文](README.zh-CN.md)

An Android-based Googlebook OS port for **Surface Pro X SQ2 / Snapdragon 8cx Gen 2**. Based on mica **16471258 (Android 17)** and Linux **6.3.8-sq2-android1**. This is **not ChromeOS Flex**.

## Latest progress — 2026-11-02

- Mainline commit `fee67b2` enables USB networking and Android USB input hotplug in the default two-entry build. The USB payload matches the reboot-tested device package; 30 USB tests passed. This does not mean 46 adapter models were tested or that a fresh complete EFI build was performed: the latter still needs the pinned private input caches.
- Normal EL2 keeps the explicitly selected existing passwordless-user login, but does **not** open Terminal or run the KVM witness at boot. Terminal remains available manually.
- LTE lab progress includes a bound IPA driver/netdev and accepted device-generated NV/production MCFG. QMI EFS metadata works, but file reads are rejected and the modem remains in factory-test mode; cellular registration/data service is **not available**. Experimental payloads are not promoted to the normal boot.

## Feature status

Updated **2026-11-02** · ✅ Available · ⭕️ Partially available · ❌ Unavailable or not confirmed

| Feature | Status | Notes |
| --- | :---: | --- |
| NVMe boot & persistent storage | ✅ | ARM64 UEFI / GRUB |
| Android desktop & GLES | ⭕️ | Desktop usable; graphics compatibility remains limited |
| Shared storage & downloads | ✅ | Files and Chrome downloads |
| USB storage | ✅ | FAT32 portable storage; other filesystems/devices not guaranteed |
| USB audio | ✅ | USB-C EarPods playback, microphone and automatic routing |
| USB wired networking | ⭕️ | AX88179A via CDC NCM: Android DHCP and validated networking passed reboot; 46 added drivers/dependencies, other adapters not hardware-verified; no generic USB Wi-Fi HAL support |
| USB mouse input | ✅ | Logitech C539 movement/buttons/wheel user-confirmed; automatic Android input registration verified after reboot |
| Wi-Fi | ✅ | Saved networks managed by Android |
| SSH | ✅ | Automatic public-key access after Android starts |
| Bluetooth | ⭕️ | Pairing and saved ON/OFF state; HID compatibility still limited |
| Touchscreen | ✅ | Multitouch and edge ghost-touch fixes |
| Keyboard cover & touchpad | ✅ | Hotplug recovery |
| Built-in speakers | ✅ | EL2 stop-pop fixed; playback, 10-second reopen and original app user-verified; default RX 0 dB |
| Built-in microphone | ✅ | Stereo/mono recording and playback |
| Hardware video decoding | ⭕️ | Current EL2: Android H.264 1080p verified; HEVC / VP8 / VP9 results are from EL1 |
| Linux Terminal | ⭕️ | Open manually; EL2 Debian shell and HTTPS verified; guest hardware decoding unverified |
| HDR video | ❌ | Not supported as a complete application feature |
| Vulkan & games | ⭕️ | Qualcomm Vulkan by default, Mesa fallback; application compatibility limits remain |
| Closed Qualcomm graphics | ⭕️ | Some paths usable; full-system compatibility incomplete |
| Internal display | ⭕️ | Current default is native 60 Hz; device-specific ~65 Hz remains experimental |
| External USB-C DisplayPort | ❌ | DP / AltMode integration incomplete |
| CPU & GPU frequency scaling | ✅ | CPU up to 1.7664 / 3.1488 GHz; SQ2 GPU default 700 MHz (RPMh432) |
| Thermal management | ✅ | CPU / GPU temperature monitoring and passive cooling |
| Battery & resource monitor | ⭕️ | Capacity/current/SOH available; capacity estimated, battery temperature unavailable |
| RTC | ✅ | System clock support |
| Power key & lid screen-off | ✅ | Screen-off only; system continues running |
| Suspend / hibernation | ❌ | Disabled; true suspend/resume unresolved |
| Orientation sensors | ❌ | Not available |
| Built-in cameras | ❌ | Not available |
| LTE | ❌ | IPA netdev and production MCFG available in lab; factory-test / EFS-read blockers remain; no cellular service |

Availability is scoped to the documented SQ2 configuration, not every peripheral or application. Long-term stability and battery life remain open work. A later boot had transient speaker silence that the user reported recovered without audio changes; the cause of the recorded SoundWire transport errors is still unresolved, separate from the verified stop-pop fix.

## Build & documentation

The current mainline SQ2 default bundle uses **EL2, Qualcomm Vulkan, native 60 Hz and GPU 700 MHz / RPMh432**, with exactly the new default and working Mesa fallback entries. It includes the Linux Terminal, EL2 H.264 decoding, adopted-ADSP audio/stop-pop fixes, USB NIC modules and Android USB input recovery. The legacy EL1 installer requires `--legacy-el1`; see BUILD for pinned private inputs and device restrictions.

The LTE research snapshot documented here is based on `73ea6ee` and does not yet contain the USB default-build integration in `fee67b2`. Do not deploy its older boot bundle over the validated device configuration.

- [Build & deployment](docs/BUILD.md) · [Current handoff](docs/HANDOFF.md) · [Open work](docs/TODO.md)
- [USB storage/audio](docs/USB-FUNCTIONS.md) · [Bluetooth input](docs/BLUETOOTH-INPUT.md) · [Microphone](docs/MICROPHONE.md)
- [LTE bring-up](docs/LTE.md) · [LTE Windows reference](docs/LTE-WINDOWS-REFERENCE.md)
- [Video decoding](docs/VIDEO-DECODE.md) · [Codec limits](docs/VIDEO-CAPABILITY-ALIGNMENT.md)
- [Graphics compatibility](docs/WILD-LIFE.md) · [Closed graphics](docs/QUEST2-GPU-RESEARCH.md) · [Display profiles](docs/DISPLAY-REFRESH.md)
- [Sources & redistribution](docs/PROVENANCE.md) · [Contributor instructions](AGENTS.md)

Most detailed engineering documents are currently in Chinese. This repository contains adaptation source, configurations and build tools—**not a ready-to-install image or a clean-clone one-command build**. Pinned proprietary images, firmware and local dependencies are required and are not redistributed.

## Safety

- Never format userdata to repair mounting or blindly reuse an installer on another disk/device.
- Keep exactly the new default and working Mesa fallback entries, both payloads, shared GRUB/firmware and the screen-only suspend guard. Do not hot-restart `vold` on this port.
- Generic build profiles and existing device deployments can differ; read BUILD before replacing boot files.
- Root/debug/permissive lab builds are not production-secure systems. Secure boot and hardware-backed security integration are incomplete.
- Keys, credentials, proprietary binaries, disk images and raw logs stay out of Git. Components retain their respective licenses.
