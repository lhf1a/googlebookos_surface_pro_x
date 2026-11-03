**Project standard build entry:** `scripts/build.py` creates one SQ1/SQ2 disk image. Defaults remain customizable; see [standard build](docs/STANDARD-BUILD.md).

# Googlebook OS on Surface Pro X SQ2

Unified SQ1/SQ2 offline build: [profiles, fixed inputs and limits](docs/UNIFIED-SQ1-SQ2.md). SQ1 hardware compatibility is not validated.

**English** | [简体中文](README.zh-CN.md)

An Android-based Googlebook OS port for **Surface Pro X SQ2 / Snapdragon 8cx Gen 2**. Based on mica **16471258 (Android 17)** and Linux **6.3.8-sq2-android1**. This is **not ChromeOS Flex**.

## Latest progress — 2026-11-03

- Standard builds now default to **320 DPI**, pinned **KernelSU Next manager/ksud auto-install**, and the shared touch repair/calibration bundle. Use `--dpi`, `--ksu-manager off` or `--touch-repair off` to customize. Existing user density overrides and installed KSU versions are preserved; the accepted kernel is not replaced. Offline integration only, no new full disk image or device acceptance in this update.
- Mainline commit `fee67b2` enables USB networking and Android USB input hotplug in the default two-entry build. The USB payload matches the reboot-tested device package; 30 USB tests passed. This does not mean 46 adapter models were tested or that a fresh complete EFI build was performed: the latter still needs the pinned private input caches.
- Normal EL2 keeps the explicitly selected existing passwordless-user login, but does **not** open Terminal or run the KVM witness at boot. Terminal remains available manually.
- Latest LTE branch `0100429` is merged. SQ2 native Radio/SIM settings, real LTE IPv4 data, mobile-data/airplane-mode controls and Wi-Fi fallback passed the recorded independent and normal-default reboots. Occasional registration rejection remains a known limit.
- LTE/Radio is **on by default in the standard build** (`--lte off` opts out), with strict private Radio04 inputs and unit-bound activation. The original non-LTE Mesa fallback is preserved. SQ1 LTE is not validated; see [baseline integration and provisioning limits](docs/LTE-BASELINE.md).
- Unified SQ1 GPU661 / SQ2 GPU700, 60Hz / 2.7Gbps×4, KSU/EL2 and first-touch calibration defaults remain customizable. Generic touch repair and the guided idle → 15 single taps → automatic candidate → retest/confirm → coordinate/thumb flow are now **on by default** (`--touch-repair off` opts out; `--touch-bundle` overrides the source-verified bundle). The algorithm is shared, while learned profiles stay device-bound. The new second layer is built/offline-tested, not yet device-accepted.

## Feature status

Updated **2026-11-03** · ✅ Available · ⭕️ Partially available · ❌ Unavailable or not confirmed

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
| Touchscreen | ⭕️ | Shared state-coherence + guided adaptive repair; residual split taps and the new second layer still require independent device validation |
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
| LTE | ⭕️ | SQ2: native Radio, SIM/signal/settings and real single-USIM LTE IPv4 data verified across reboot; unit-private calibration/APN required. No voice/SMS/IMS/eSIM/5G; SQ1 and IPv6 unverified |

Availability is scoped to the documented SQ2 configuration, not every peripheral or application. Long-term stability and battery life remain open work. A later boot had transient speaker silence that the user reported recovered without audio changes; the cause of the recorded SoundWire transport errors is still unresolved, separate from the verified stop-pop fix.

## Build & documentation

The current mainline SQ2 default bundle uses **EL2, Qualcomm Vulkan, native 60 Hz and GPU 700 MHz / RPMh432**, with exactly the new default and working Mesa fallback entries. It includes the Linux Terminal, EL2 H.264 decoding, adopted-ADSP audio/stop-pop fixes, USB NIC modules and Android USB input recovery. The legacy EL1 installer requires `--legacy-el1`; see BUILD for pinned private inputs and device restrictions.

The standard single-image build merges the USB/EL2 baseline with LTE/Radio by an audited LTE-only overlay, not by replacing the system with an older research bundle. Supply the exact private Radio04 input via `--lte-bundle`, or explicitly use `--lte off`; missing LTE inputs fail closed. Model/serial/ESP checks prevent transplanting one unit's FSG to SQ1 or another SQ2. This merge does not rebuild the previously delivered IMG/GZ or deploy to a device.

- [Build & deployment](docs/BUILD.md) · [Current handoff](docs/HANDOFF.md) · [Open work](docs/TODO.md)
- [USB storage/audio](docs/USB-FUNCTIONS.md) · [Bluetooth input](docs/BLUETOOTH-INPUT.md) · [Microphone](docs/MICROPHONE.md)
- [LTE baseline](docs/LTE-BASELINE.md) · [Native Radio](docs/LTE-RADIO.md) · [LTE data](docs/LTE-DATA.md)
- [LTE bring-up history](docs/LTE.md) · [LTE Windows reference](docs/LTE-WINDOWS-REFERENCE.md)
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
