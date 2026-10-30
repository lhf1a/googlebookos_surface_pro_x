# Googlebook OS on Surface Pro X SQ2

**English** | [简体中文](README.zh-CN.md)

An Android-based Googlebook OS port for **Surface Pro X SQ2 / Snapdragon 8cx Gen 2**. Based on mica **16471258 (Android 17)** and Linux **6.3.8-sq2-android1**. This is **not ChromeOS Flex**.

## Feature status

Updated **2026-10-30** · ✅ Available · ⭕️ Partially available · ❌ Unavailable or not confirmed

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
| Built-in speakers | ✅ | User-verified loudness, stereo and stop/start; default RX 0 dB (+12 dB versus original) |
| Built-in microphone | ✅ | Stereo/mono recording and playback |
| Hardware video decoding | ⭕️ | AVC / HEVC / VP8 / VP9 SDR; resolution/profile limits apply |
| HDR video | ❌ | Not supported as a complete application feature |
| Vulkan & games | ⭕️ | Turnip Vulkan 1.3; application-specific compatibility limits |
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

Default source builds include **hardware video, USB storage/audio and Bluetooth input fixes**, with separate normal/rescue policies and selectable display profiles.

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
