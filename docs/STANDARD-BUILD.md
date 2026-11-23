# Hybrid Standard Build Flow and Artifacts

Default Hybrid build, Windows dependency preparation, and credential-free full-disk assembly.

## 1. Sole Product Line

- Standard entrypoint: `python3 scripts/build.py`; omitting the subcommand defaults to `full-image`.
- Graphics only allows `hybrid`: Mesa GLES/Turnip + QTI gralloc/mapper5/HWC, preserving the dual Vulkan selection policy.
- SQ1/SQ2 share the Linux 6.18.60 Image, matching modules, and initramfs; SMBIOS routing selects the device DT frequency configuration.
- Camera, auto-brightness, auto-rotation, and the touch wizard are required capabilities.
- `compile` builds kernel components; `verify/package` verifies/copies the legacy pinned package; `--esp-only` outputs only the boot partition.
- General delivery rules are in [DEVELOPMENT](DEVELOPMENT.md).

## 2. Build Order

Dependency sources, read-only Windows extraction, unified download packaging, and Linux import are in [DEPENDENCIES](DEPENDENCIES.md).

```text
Shared platform dependencies + matching 6.18 kernel build tree
 ├─ Camera source → driver/AIDL/Camera HAL/ISP/mapper/allocator → camera/CAMERA.json
 └─ Sensor source + shared OEM assets → modules/SubHal/overlay/RPC/SLPI EFI → sensors/sensors-bundle.json
                                   ↓
full-image: prepare kernel source → display/A6xx patches → Image/modules/DTB → out-of-tree modules/depmod
                                   ↓
Rebuild matching modules (or explicitly reuse completed components) → Hybrid graphics integration → build/verify/reuse touch wizard → install camera/sensors
                                   ↓
Venus suspend handshake module + desktop-services.jar patch + one-time suspend settings migration
                                   ↓
Strip personal credentials / skip first-time setup → rebuild initramfs → pair EFI/SLPI firmware/device DT
                                   ↓
Template system partitions + fresh userdata/metadata → GPT/ESP/filesystem readback → IMG/IMG.GZ/manifests
```

Camera and sensor bundles must be prepared before the full kernel build. Sensor SLPI firmware, model configs, and default optical calibration are shared between SQ1/SQ2 and prepared once.

## 3. Input Inventory

| Input | Purpose |
| --- | --- |
| `private/build-inputs/template.img` | Original 20 GiB layout and Android system partitions |
| `kernel-6.18/source/SOURCE.json`, `source.tar.gz` | Kernel / out-of-tree module source |
| `kernel-6.18/toolchain.tar.gz`, `board.dts` | ARM64 toolchain, DT source |
| `full-image-runtime/records.json`, `files/` | Shared Mesa/QTI/audio-video inputs |
| `camera/CAMERA.json` and ELFs | Front/rear camera modules, provider, mapper/allocator |
| `sensors/sensors-bundle.json` and files | ALS/accelerometer/SLPI/RPC/configs |
| `suspend/desktop-services.jar` | Patched at build time for 1000ms AC screen-off timeout; taken from same Android system |
| Android platform interfaces, generated headers, link libs, NDK/SDK | Camera/sensor/touch userspace builds |

Camera config template: `configs/camera-build.example.json`; fields and ABI requirements are in [CAMERA-BUILD](CAMERA-BUILD.md). Sensor commands and shared inputs are in [SENSORS](SENSORS.md).

## 4. Execution Entrypoints

Prepare `private/camera-build.json`, `kernel_source`/`kernel_build` matching 6.18, and platform dependencies:

```sh
python3 scripts/build-spx-camera-bundle.py --inputs private/camera-build.json --output out/camera-from-source
python3 scripts/build.py --camera-inputs out/camera-from-source/camera --sensor-inputs private/build-inputs/sensors --output FRESH_LINUX_OUTPUT --jobs 8
```

Generate the sensor bundle first with `build-spx-sensor-bundle.py`. `output` must be a fresh directory on a Linux filesystem.

To reuse completed components, generate an explicit hash manifest with `full_image_reuse.py`, then run:

```sh
python3 scripts/build.py --reuse-components REUSE.json --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT
```

When only Venus needs refreshing, run `rebuild-hybrid-venus.py` first:

```sh
python3 scripts/rebuild-hybrid-venus.py --reuse-components PREVIOUS_REUSE.json --kernel-build MATCHED_KOUT --source PREVIOUS_VENUS_SOURCE --output FRESH_VENUS_OUTPUT
python3 scripts/build.py --reuse-components FRESH_VENUS_OUTPUT/REUSE.json --private-root PREPARED_INPUTS --output FRESH_IMAGE_OUTPUT
```

When no affected source has changed, `full_image_reuse.py --assembled-runtime PREVIOUS_INITRAMFS` can explicitly reuse compiled small helpers.

## 5. Artifact Hierarchy and First-Boot Cleanup

| Path (relative to OUTPUT) | Meaning |
| --- | --- |
| `kernel-build/`, `kernel-export/` | Kernel build tree, Image, module stage, DTB, and command records |
| `camera-sensors/`, `camera-camss/`, sensor module dir | Component outputs matching this kernel build |
| `paired/EFI/SQ2Nat/` | First-pass paired boot bundle |
| `paired/EFI/SQ2Nat/PRIVACY.json` | Identity stripping and default first-time setup skip record |
| `disk/work/esp-tree/` | ESP tree after storage adaptation, normal/rescue entries, and SQ1/SQ2 routing |
| `disk/surface-pro-x-6.18-hybrid-generic.img(.gz)` | Full sparse disk image and compressed image |
| `disk/IMAGE-MANIFEST.json`, `disk/LAYOUT.json` | Image result and partition layout |
| `FULL-IMAGE.json`, `BUILD-COMMANDS.json` | End-to-end result, commands, and log locations |

Personal SSH/Wi-Fi/passwords and legacy setup-skip scripts are stripped after final component installation; old userdata/metadata, persist, FRP, and raw dumps are not copied. `surface-first-boot` sets `device_provisioned=1` / `user_setup_complete=1` for user 0 and writes a userdata marker; `PRIVACY.json` records `first_setup_preserved=false` and `first_setup_skipped=true`.

The touch calibration wizard is built from `probe/touch-calibration/`; during reuse builds, if only wizard Java/Manifest files change, the old APK is invalidated and rebuilt. See [IMAGE](IMAGE.md) for current artifact details and boundaries.
