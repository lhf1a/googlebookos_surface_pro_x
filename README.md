# Googlebook OS on Surface Pro X SQ2

**English** | [简体中文](README.zh-CN.md)

An Android-based Googlebook OS port for **Surface Pro X SQ2 / Snapdragon 8cx Gen 2**. Based on mica **16471258 (Android 17)** and Linux **6.3.8-sq2-android1**. This is **not ChromeOS Flex**.

## Feature status

Updated **2026-11-02** · ✅ Available · ⭕️ Partially available · ❌ Unavailable or not confirmed

| Feature | Status | Notes |
| --- | :---: | --- |
| NVMe boot & persistent storage | ✅ | ARM64 UEFI / GRUB |
| Android desktop & GLES | ⭕️ | Desktop usable; graphics compatibility remains limited |
| Shared storage & downloads | ✅ | Files and Chrome downloads |
| USB storage | ✅ | FAT32 portable storage; other filesystems/devices not guaranteed |
| USB audio | ✅ | USB-C EarPods playback, microphone and automatic routing |
| Wi-Fi | ✅ | Saved networks managed by Android |
| SSH | ✅ | Automatic public-key access after Android starts |
| Bluetooth | ⭕️ | Pairing and saved ON/OFF state; HID compatibility still limited |
| Touchscreen | ✅ | Multitouch and edge ghost-touch fixes |
| Keyboard cover & touchpad | ✅ | Hotplug recovery |
| Built-in speakers | ⭕️ | Playback restored and user-confirmed; EL2 stop-pop remains open; default RX 0 dB |
| Built-in microphone | ✅ | Stereo/mono recording and playback |
| Hardware video decoding | ⭕️ | Current EL2: Android H.264 1080p verified; HEVC / VP8 / VP9 results are from EL1 |
| Linux Terminal | ⭕️ | EL2 Debian shell and HTTPS verified; guest hardware decoding unverified |
| HDR video | ❌ | Not supported as a complete application feature |
| Vulkan & games | ⭕️ | Qualcomm Vulkan by default, Mesa fallback; application compatibility limits remain |
| Closed Qualcomm graphics | ⭕️ | Some paths usable; full-system compatibility incomplete |
| Internal display | ⭕️ | Native 60 Hz; optional device-specific ~65 Hz profile |
| External USB-C DisplayPort | ❌ | DP / AltMode integration incomplete |
| CPU & GPU frequency scaling | ✅ | CPU up to 1.7664 / 3.1488 GHz; SQ2 GPU default 700 MHz (RPMh432) |
| Thermal management | ✅ | CPU / GPU temperature monitoring and passive cooling |
| Battery & resource monitor | ⭕️ | Capacity/current/SOH available; capacity estimated, battery temperature unavailable |
| RTC | ✅ | System clock support |
| Power key & lid screen-off | ✅ | Screen-off only; system continues running |
| Suspend / hibernation | ❌ | Disabled; true suspend/resume unresolved |
| Orientation sensors | ❌ | Not available |
| Built-in cameras | ❌ | Not available |
| LTE | ❌ | Usability not confirmed |

Availability is scoped to the documented SQ2 configuration, not every peripheral or application. Long-term stability and battery life remain open work.

## Build & documentation

The current SQ2 default bundle uses **EL2, Qualcomm Vulkan, native 60 Hz and GPU 700 MHz**, with a working Mesa fallback. It includes the Linux Terminal, EL2 H.264 decoding and adopted-ADSP audio fixes. The legacy EL1 installer requires `--legacy-el1`; see BUILD for pinned private inputs and device restrictions.

- [Build & deployment](docs/BUILD.md) · [Current handoff](docs/HANDOFF.md) · [Open work](docs/TODO.md)
- [USB storage/audio](docs/USB-FUNCTIONS.md) · [Bluetooth input](docs/BLUETOOTH-INPUT.md) · [Microphone](docs/MICROPHONE.md)
- [Video decoding](docs/VIDEO-DECODE.md) · [Codec limits](docs/VIDEO-CAPABILITY-ALIGNMENT.md)
- [Graphics compatibility](docs/WILD-LIFE.md) · [Closed graphics](docs/QUEST2-GPU-RESEARCH.md) · [Display profiles](docs/DISPLAY-REFRESH.md)
- [Sources & redistribution](docs/PROVENANCE.md) · [Contributor instructions](AGENTS.md)

Most detailed engineering documents are currently in Chinese. This repository contains adaptation source, configurations and build tools—**not a ready-to-install image or a clean-clone one-command build**. Pinned proprietary images, firmware and local dependencies are required and are not redistributed.

## Safety

- Never format userdata to repair mounting or blindly reuse an installer on another disk/device.
- Keep normal/rescue boot paths and the screen-only suspend guard. Do not hot-restart `vold` on this port.
- Generic build profiles and existing device deployments can differ; read BUILD before replacing boot files.
- Root/debug/permissive lab builds are not production-secure systems. Secure boot and hardware-backed security integration are incomplete.
- Keys, credentials, proprietary binaries, disk images and raw logs stay out of Git. Components retain their respective licenses.
