# Googlebook OS on Surface Pro X

Android-based Googlebook OS port for **Surface Pro X SQ1 / SQ2** (Snapdragon 8cx Gen 1/2),
based on mica **16471258 (Android 17)** and Linux **6.18.60-sq2-android1**.

The only default full-image build is **hybrid**, shared by SQ1 and SQ2: Mesa GLES/Turnip with
QTI gralloc/mapper5/HWC and native OMX.

[简体中文](README.zh-CN.md) · [Build](docs/BUILD.md) · [Dependencies](docs/DEPENDENCIES.md) ·
[Current image and first-boot policy](docs/IMAGE.md) · [Portability rules](docs/DEVELOPMENT.md)

## Build

```sh
python3 scripts/build.py --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT
```

Prepare shared OEM inputs on an original SQ1 or SQ2 Windows installation with
`scripts/Prepare-HybridDependencies.ps1`, then copy the tar package and checksum to the Linux
build host. Public downloads and Git revisions are pinned; the collection package does not yet
provide a complete clean-room Android/QTI runtime/template build — see the dependency guide.

Completed components can be reused with `--reuse-components`. `rebuild-hybrid-venus.py`
rebuilds only Venus against the exact matching KOUT before repacking. Full input list, order,
artifacts and reuse rules are in [STANDARD-BUILD](docs/STANDARD-BUILD.md).

## Status

Updated 2026-11-16. ✅ available · ⭕️ partial / limited evidence · ❌ unavailable

| Area | Status | Notes |
| --- | :---: | --- |
| NVMe boot & storage | ✅ | ARM64 UEFI / GRUB, sparse full-disk image |
| Android desktop & GLES | ⭕️ | Usable; graphics compatibility still limited |
| Camera (front/rear) | ⭕️ | V4L2 + software ISP + Camera2, native JPEG, H.264/AAC; IR/Hello and AF pending |
| Touchscreen | ⭕️ | Shared calibration wizard; per-device profiles stay device-bound |
| Auto brightness / rotation | ⭕️ | SQ2 verified; SQ1 pending |
| Suspend (s2idle) | ⭕️ | SQ2 fix verified once; timer wakeups remain, long-run power untested |
| Audio (speakers/mic/USB) | ✅ | Verified on SQ2 |
| Video decode (Venus) | ⭕️ | H.264/HEVC decode with QTI OMX; HDR not enabled |
| LTE | ⭕️ | SQ1/SQ2 data demonstrated; fresh dual-device image acceptance pending |
| USB networking / storage | ⭕️ | Validated adapters only |
| Wi-Fi / Bluetooth / SSH | ⭕️ | Android-managed Wi-Fi; BT HID limited; SSH key-only by default |
| Root / KernelSU | ✅ | KSU Next + Zygisk, lab-only permissive options |

## Documentation

Build and delivery:

- [BUILD](docs/BUILD.md) — current build entry, inputs, artifacts.
- [STANDARD-BUILD](docs/STANDARD-BUILD.md) — full flow, input status, reuse.
- [DEPENDENCIES](docs/DEPENDENCIES.md) — Windows dependency collection and handoff.
- [IMAGE](docs/IMAGE.md) — first-boot policy, touch wizard, current image checksums.
- [SUSPEND](docs/SUSPEND.md) — s2idle fix and its verified limits.
- [DEVELOPMENT](docs/DEVELOPMENT.md) — portability contract for any SQ1/SQ2.
- [TODO](docs/TODO.md) — open work only.
- [PROVENANCE](docs/PROVENANCE.md) — pinned runtime sources and licensing.

Features: [camera](docs/CAMERA-BUILD.md), [sensors](docs/SENSORS.md),
[touch calibration](docs/TOUCH-CALIBRATION.md), [touch compatibility](docs/TOUCH-DEVICE-COMPAT.md),
[LTE](docs/LTE-PRODUCT.md), [speakers](docs/SPEAKER-LOUDNESS.md), [microphone](docs/MICROPHONE.md),
[video](docs/VIDEO-CAPABILITY-ALIGNMENT.md), [USB](docs/USB-FUNCTIONS.md),
[USB-NVMe deploy](docs/USB-NVME-DEPLOY.md), [Bluetooth input](docs/BLUETOOTH-INPUT.md),
[Wild Life workaround](docs/WILD-LIFE.md), [root](docs/ROOT-MANAGEMENT.md),
[power policy](docs/UNRESTRICTED-POWER.md), [unified SQ1/SQ2](docs/UNIFIED-SQ1-SQ2.md),
[display density](docs/DEFAULT-DISPLAY-DENSITY.md), [4 KB page notice](docs/PAGE-MODE-NOTIFICATION.md).

## Scope and limits

Private firmware, libraries, credentials, disk images and raw captures stay outside Git; the
reviewed external kernel source/DT input is documented under
[source-dependencies](source-dependencies/kernel-6.18/README.md). Component licenses remain
applicable. Host build success does not establish device boot, cross-device acceptance,
long-term suspend or power behavior. See [TODO](docs/TODO.md) for what is still unverified.