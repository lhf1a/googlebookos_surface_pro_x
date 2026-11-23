# Hybrid 6.18: Dependency Sources, Windows Collection, and Offline Handoff

Applies to the shared Surface Pro X SQ1/SQ2 image (preserving camera, auto-brightness, auto-rotation, and touch). Windows handles downloading, read-only extraction, and packaging; Linux x86_64 handles ARM64 cross-compilation.

## 1. By Source Category

| Source | Dependency / Purpose | Acquisition & Preparation |
|---|---|---|
| This project's public source | 6.18 adaptation patches, SoundWire/CAMSS/ALS/SLPI drivers, Camera HAL/ISP, sensor SubHal, touch wizard, graphics/video wrappers | Pinned commit of [this repository](https://github.com/lhf1a/googlebookos_surface_pro_x). |
| Other public repositories | Firmware tools, `gbos-vm` reference, `minigbm`, `cutils/log` headers, `hexagonrpc`, submodules, and nested configs | `configs/hybrid-dependencies.json.repositories` pins HTTPS URLs and commits; Windows script saves Git bundles. |
| Official / upstream downloads | Googlebook `mica` recovery, Cuttlefish security components, Mesa, `libdrm`, `libsepol`, NDK r28c, SDK API35/build-tools35 | Manifest `downloads` pins URLs/hashes (from `configs/sources.lock.json` and `configs/graphics-sources.lock.json`). |
| Shared OEM hardware assets | ADSP audio firmware, CDSP, SLPI, GPU vendor, WLAN/BT, modem/MCFG, sensor model configs, ALS default calibration | Original Surface Windows DriverStore or official recovery image Windows directory; collected once across SQ1/SQ2. |
| OEM Windows boot file | `tcblaunch.exe` | Read-only copy from `Windows/System32`; `start.efi`/`el2launch.efi` and `qebspilaa64.efi` must be built from project source. |
| Third-party archived QTI binaries | Quest 2 v77 `gralloc`/`metadata`, `libgsl`/`adreno_utils`, native Vulkan/QGL, etc. | Pinned OTA build `51584880050400150` in `configs/quest2-full-donor-candidate.json`, enabled via `-IncludeThirdParty`. |
| QTI/AOSP public source builds | Display/HWC/SDM, `mapper5`/allocator, OMX, HIDL, Stagefright | Built using in-repo scripts with matching Android platform headers and link libraries. |
| Rebuildable with platform tree | Camera AIDL/HIDL/mapper/allocator generated headers & link libs, sensor HAL link libs | Matching [AOSP](https://android.googlesource.com/) branch source and platform build artifacts; paths go into a copy of `configs/camera-build.example.json`. |
| In-repo kernel rebuild recipe | Official 6.18.60 + project delta + out-of-tree module source and DT overlays | `configs/hybrid-kernel-source.json`, `patches/kernel-6.18-surface-baseline.patch`, `source-dependencies/kernel-6.18/`. |
| Separately prepared inputs | `board.dts`/EFI, full Android/QTI runtime, and template layout | No unified from-scratch generator yet; must be provided as explicit inputs. |

The Linux 6.3 `surface_kernel` lock is a historical baseline only and is not downloaded as the 6.18 source. ALS default optical calibration extracts only the CRC-checked 1480-byte model data from `SurfaceLightSensor.sys`.

### Android/QTI Source Split

| Role | Pinned Source / Recipe | What Enters Hybrid |
|---|---|---|
| Android base & framework | `mica` recovery `16471258`, Cuttlefish `16373615`, `quest2-hidl-build.json` (AOSP `android-16.0.0_r1`), `frameworks/av` `e2f098935447ca4945946de5cb69db843fe3f003` | Base partitions, software KeyMint/Gatekeeper, source-built HIDL support, patched Stagefright/P010 paths |
| Quest 2 v77 proprietary core | `quest2-full-donor-candidate.json`, `quest2-v77-graphics-reference.json` (`V@0819.0` / `GIT@274d25476a`) | `gralloc.kona`, `gralloccore`, `qdMetaData`, `qcodec2_utils`, `gsl`, `adreno_utils`, native QTI Vulkan/QGL (redirected by `build-quest2-native-vulkan.py`; desktop EGL/GLES uses Mesa) |
| QTI CAF + AOSP source HALs | SM8350 display `e9bbe9a980d408af36b52f2abaae7c35647b7425`; SM8250 media `05f3cd8dbfa66f2e79ffda3cb294697374510160`; oculus kernel headers `6f22d8e7cb7b48f3026f72bc3dbefef787a62ecc`; `mapper5` recipe | `build-sm8350-display.py` (HWC/SDM), `build-quest2-mapper5.py` (mapper/allocator), `build-sm8250-omx.py` (OMX) |
| Mesa / libdrm | Mesa 26.2.4, libdrm 2.4.133, NDK r28c, `gbos-vm` `mapper5` patch | Freedreno EGL/GLES, Turnip, shared `libdrm` |
| In-repo glue | `probe/graphics` & `scripts/` | Vulkan routing, Mesa→QTI gralloc bridge, linear RGB layout façade, SDM backlight redirect, `dma_heap`/ION and Vulkan memory adaptation |

### Quest 2 Download and Extraction

[The archive index](https://cocaine.trade/Quest_2_firmware) pins build `51584880050400150` (ZIP SHA256 `f09e3a0de4d5c345975b0869d0049a339fba820cb178039287120e7247775e1b`). By default, only the 96,436-byte prefix and 84,162,032-byte vendor range are downloaded; pass `-FullDonorArchive` for the full 1,172,543,128-byte archive.

On Linux, `rebuild-quest2-v77-vendor.py` and `extract-quest2-v77-reference.py` handle vendor rebuild and read-only `debugfs` extraction (see [PROVENANCE](PROVENANCE.md) for donor provenance), chained by `hybrid_bootstrap.py --stage quest`.

### Official Channels

- [Microsoft Surface Pro drivers](https://support.microsoft.com/en-us/surface/drivers-firmware/download-drivers-and-firmware-for-surface-pro) (Pro X uses Windows Update)
- [Microsoft recovery image](https://support.microsoft.com/en-us/surface-recovery-image)
- [linux-surface firmware tool](https://github.com/linux-surface/aarch64-firmware)
- [Official NDK downloads](https://developer.android.com/ndk/downloads) (this project pins r28c Linux x86_64)

## 2. Original Surface Windows: Unified Entrypoint

Requires Git for Windows, system `curl.exe`/`tar.exe`, and PowerShell 5.1+; use NTFS/exFAT with sufficient space (~7.6 GB of downloads). Run PowerShell as Administrator if DriverStore access is denied.

```powershell
# Preview manifest (offline, creates no output)
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps -Plan

# Download, extract, and package (Output must not exist)
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps -IncludeThirdParty

# Use a local cache directory
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps-2 -CacheDirectory D:\spx-download-cache
```

Outputs are `D:\spx-deps.tar`, `.sha256.json`, and the expanded directory; `MANIFEST.json` records per-file SHA256, size, source commits, and `unresolved` items.

Offline device asset export:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Export-SurfaceHybridDependencies.ps1 -Output D:\spx-oem
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps-3 -SurfaceExport D:\spx-oem
# Or point to an unpacked official recovery Windows tree
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Export-SurfaceHybridDependencies.ps1 -Output D:\spx-oem-recovery -WindowsRoot E:\recovery\Windows
```

Exporting accesses only DriverStore packages matching specified INF prefixes and `tcblaunch.exe`, excluding accounts, Wi-Fi, SSH keys, registry, or modem NV/EFS. If same-named files differ across driver versions, Linux import reports a conflict; select the matching version and re-collect.

### Local Supplementary Archives

Merge explicitly listed regular files via `-LocalInputs D:\local-inputs.json`:

```json
{
  "files": [
    {
      "id": "qti-v77-vendor",
      "path": "D:\\authorized-donors\\vendor.img",
      "sha256": "<actual 64-char SHA256 of the file>",
      "provenance": "<actual source, version, and purpose>",
      "personal_state_absent": true
    }
  ]
}
```

`-SkipDownloads`/`-SkipRepositories`/`-SkipSurface` support segmented troubleshooting; failed downloads retain `.part` for inspection, and retries require a new Output path.

## 3. Copy to Build Host and Verify

Linux host requirements: Ubuntu 24.04 x86_64, Python 3.12+, `python3-venv`, Git, `patch`, GCC/G++, `make`, `aarch64-linux-gnu` toolchain, `ninja`, `pkg-config`, `flex`/`bison`, `glslang-tools`, JDK, `patchelf`, and device-tree/ELF/EROFS tools. Copy the tar and SHA256 receipt to `downloads/`, keeping build outputs on a Linux filesystem:

```sh
python3 scripts/import-hybrid-dependencies.py \
  --archive downloads/spx-deps.tar --sha256 SHA256 \
  --output private/dependency-import
```

Import verifies the outer archive, member sizes/hashes, and download locks, then generates `SURFACE-PREPARATION.json`, organizes DSP/SLPI firmware and model sensor configs, and runs the CRC calibration extractor.

Example of restoring Git source:

```sh
git clone private/dependency-import/repositories/hexagonrpc.bundle upstream/hexagonrpc
git -C upstream/hexagonrpc checkout --detach da7a3742ab8d5273a281b70ee11f16fb2cb6a690
```

`device_faker` config is provided separately as `device_faker_config.bundle`; submodules for other repos must be prepared per their gitlinks. Unpack/build NDK/SDK ZIPs and Mesa tarballs per [CAMERA-BUILD](CAMERA-BUILD.md) and [SENSORS](SENSORS.md).

## 4. From Empty Build Directory to Image

1. Clone this repository and pinned submodules, install Linux build dependencies, and complete the Windows handoff.
2. Prepare matching kernel + external source, `board.dts`, EFI, and toolchain.
3. Prepare template and credential-stripped runtime.
4. Generate `camera/CAMERA.json` from source per CAMERA-BUILD, and build sensor HAL/RPC/overlay/modules/EFI per SENSORS to generate `sensors-bundle.json`.
5. Check input completeness:

```sh
python3 scripts/import-hybrid-dependencies.py --private-root private/build-inputs
```

6. Run the default Hybrid build (delivery rules in [STANDARD-BUILD](STANDARD-BUILD.md)):

```sh
python3 scripts/import-hybrid-dependencies.py --private-root private/build-inputs \
  --build --build-output /home/USER/spx-build/hybrid-001 --jobs 8
```

## 5. Offline Bootstrap Stages

Run directly after import or invoke `hybrid_bootstrap.py` per stage (using a fresh `--work` directory each time):

```sh
python3 scripts/import-hybrid-dependencies.py \
  --archive downloads/spx-deps.tar --sha256 SHA256 \
  --output private/dependency-import \
  --bootstrap-work /home/USER/spx-build/sources-001 --bootstrap-stage sources

python3 scripts/hybrid_bootstrap.py --imported private/dependency-import \
  --work /home/USER/spx-build/quest-001 --stage quest
```

| Stage | Operations Performed |
|---|---|
| `sources` | Offline clone/checkout from Windows Git bundles, verifying pinned commits |
| `toolchains` | Verifies and unpacks NDK/SDK and Mesa/libdrm archives, preserving executable bits and internal symlinks |
| `kernel` | kernel.org 6.18.60 + project patch + external source, producing `SOURCE.json`/`source.tar.gz` |
| `platform` | Compiles AIDL, camera provider, mapper/allocator, sensor HIDL HAL, and rotation overlay, producing source-only camera inputs |
| `mesa` | Uses offline Python wheels and `--wrap-mode=nodownload` to build 6 ARM64 libraries (EGL/GLES/Turnip/libdrm) |
| `quest` | Verifies prefix/vendor ranges or splits full ZIP, runs 131-operation rebuild and read-only `debugfs` extraction |
| `android` | Verifies `mica` ZIP/RAW and Cuttlefish sparse `super`, extracts slot A partitions and platform files |
| `glue` | Compiles two ARM64 glue libraries with `--ndk` and organizes `runtime-overlay/` |
| `assemble` | Explicitly reuses completed components and runtime to produce a full disk image |
| `all` | Runs source preparation stages sequentially; exits non-zero at the end because clean-room HWC/OMX/runtime preparation is not yet closed |

The generic boot payload also requires `spx-boot-gnu-efi.bundle`: after checkout, pass `sources/spx-boot-gnu-efi` as `--boot-gnu-efi` or place it at `kernel-6.18/gnu-efi` (do not replace `qebspil-gnu-efi` used by the SLPI loader).

Standalone stage verification example:

```sh
python3 scripts/hybrid_bootstrap.py --stage mesa \
  --imported private/dependency-import --ndk NDK_DIRECTORY \
  --work /home/USER/spx-build/mesa-001 --jobs 8
```

## 6. Sensor Extraction and Component Reuse Assembly

Run `Prepare-HybridDependencies.ps1 -Output D:\spx-dependencies -IncludeThirdParty` on original SQ1 or SQ2 Windows (or `Export-SurfaceHybridDependencies.ps1 -Output D:\spx-oem -RequireSensors` for OEM files only) to extract from `Windows\System32\DriverStore\FileRepository`:

- `surfaceprox_subextscss.inf_*`: `qcslpi8180.mbn` (SLPI firmware)
- `surfaceprox_sensorsconfig.inf_*`: 23 model configs (22 JSONs and `config_list.txt`)
- `surfacelightsensor.inf_*`: `SurfaceLightSensor.sys` (1480-byte model default optical calibration extracted on Linux import)

Imported assets reside under `surface-assets/sensors/` and `surface-assets/firmware/qcom/msft/surface/pro-x/`. Missing items or version conflicts halt packaging and record details in `SURFACE-EXPORT.json`; update via Windows Update or point `-WindowsRoot X:\Windows` to a complete recovery directory.

Sensor runtime and full-disk reuse assembly:

1. Run `scripts/build-spx-sensor-runtime.py` with `--surface-assets`, `--kernel-components`, `--platform-components`, `--sources-root` (`hexagonrpc`, `qebspil`, `qebspil-gnu-efi`, `qebspil-dtc`), and `--venus-firmware` to generate the full sensors bundle (pass `--reuse-rpc` / `--reuse-efi` to skip rebuilding RPC/EFI).
2. If only Venus needs refreshing, run `rebuild-hybrid-venus.py` per [SUSPEND](SUSPEND.md).
3. Record artifact SHA256s with `full_image_reuse.py` and build the full image (`SQ2_ANDROID_SDK` required for the touch wizard):

```sh
python3 scripts/full_image_reuse.py --kernel KERNEL_EXPORT --camera CAMERA_COMPLETE \
  --sensors SENSOR_BUNDLE --mesa MESA_RUNTIME --glue GLUE_OVERLAY --output reuse.json
python3 scripts/build.py --reuse-components reuse.json --private-root PREPARED_INPUTS --output BUILD_OUTPUT
# or
python3 scripts/hybrid_bootstrap.py --stage assemble --reuse-components reuse.json --private-root PREPARED_INPUTS --work BUILD_WORK
```

See [BUILD](BUILD.md) for further build details.
