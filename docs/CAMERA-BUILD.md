# Camera Source Build

Front and rear camera support builds from Linux drivers, the SC8180X/sensor patches in this repository, the software ISP, and the Android Camera provider—no Surface boot-bundle extraction or Windows camera drivers are required. The Hybrid buffer bridge still requires matching platform headers, generated interfaces, and linked libraries from the shared SDK/donor platform.

## Preparing Inputs

Copy `configs/camera-build.example.json` to a local config and update the paths below. Relative paths resolve against the JSON file's directory.

| Input | Contents |
| --- | --- |
| kernel_source | Linux 6.18.60 source with CAMSS, OV5693/OV13858, CCI, and regulator sources |
| kernel_build | Kernel build output with `.config`, `Module.symvers`, and generated headers; `kernel.release` must be `6.18.60-sq2-android1` |
| toolchains | Project toolchain layout: `sq2-graphics/android-ndk-r28c`, Mesa 26.2.4 `android_stub`, `sq2-c2-core-reference/libcutils`, `sq2-c2-native-reference/libs/binder/ndk`, `sq2-c2-aidl-generated/include` |
| interfaces | AOSP `hardware/interfaces` frozen AIDL source, versions specified by `build-spx-camera-aidl.py` |
| aidl_compiler | Android SDK `android-15/aidl` |
| dependencies | `jconfig.h`/`jpeglib.h`/`jmorecfg.h` in `jpeg`; `camera_metadata.h`/`camera_metadata_tags.h`/`camera_vendor_tags.h` in `system-media`; `native-headers/libs/nativewindow/include` |
| libraries | Platform-matching `libcamera_metadata.so`, `libnativewindow.so`, `libjpeg.so`, `libcutils.so` |
| platform_headers | Platform mapper5 generated headers, including Stable-C IMapper and metadata interfaces |
| binder_library | Platform `libbinder_ndk.so` |
| layout_headers / hidl_headers | Layout generated headers and HIDL headers from the graphics build |
| allocator_headers | Generated NDK headers for `android.hardware.graphics.allocator` |
| graphics_library_dirs | Directories containing `libgrallocutils`, `libqdMetaData`, `libcutils`, platform `libc++`, `allocator-V2`/`common-V2` NDK interface libraries, and `libbinder_ndk`, in search priority order |
| cross_compile | ARM64 kernel toolchain prefix, default `aarch64-linux-gnu-` |

Required AIDL versions: `common` V2, `FMQ` V1, `graphics.common` V5, `camera.common` V1, and `camera.metadata`/`device`/`provider` V3. Each frozen source directory must contain its `.hash`, and platform libraries must match the target Android ABI. Prepare a matching kernel build tree first if the kernel is not yet built.

## Building and Integration

```sh
python3 scripts/build-spx-camera-bundle.py --inputs private/camera-build.json --output out/camera-from-source
```

This entrypoint executes:

1. Apply camera patches on a source copy in the output directory and compile sensor/CCI/CAMCC/SPMI and CAMSS modules.
2. Generate and compile Binder interfaces from frozen AIDL sources.
3. Compile the Camera HAL and software ISP from the repository.
4. Apply patches 0004/0005/0006 on a `probe/graphics` source copy and compile mapper/allocator.
5. Output `camera/CAMERA.json` and 13 runtime ELFs, verifying ARM64 architecture, module ABI, and integrity.

Pass the output directory to the standard Hybrid full-image build (see [STANDARD-BUILD](STANDARD-BUILD.md)):

```sh
python3 scripts/build.py full-image --graphics hybrid --private-root /path/to/platform-inputs --camera-inputs out/camera-from-source/camera --output out/full-camera-image --esp-only
```

In `source` kernel mode, camera modules are recompiled against the current kernel while userspace uses the generated bundle. Build logs are written to `build/*.log` and command records to `build/commands.json`.
