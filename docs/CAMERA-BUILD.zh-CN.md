# 摄像头源码构建

前后摄支持基于 Linux 驱动、仓库中的 SC8180X/传感器补丁、软件 ISP 和 Android Camera provider 构建，无需从 Surface 提取启动包或引入 Windows 摄像头驱动。Hybrid 缓冲桥接仍需共用 SDK/供体平台匹配的头文件、生成接口及链接库。

## 准备输入

复制 `configs/camera-build.example.json` 为本地配置并修改以下路径。所有相对路径均按 JSON 所在目录解析。

| 输入 | 内容 |
| --- | --- |
| kernel_source | Linux 6.18.60 源码，含 CAMSS、OV5693/OV13858、CCI 和 regulator 源码 |
| kernel_build | 内核构建输出，含 `.config`、`Module.symvers` 和生成头文件；`kernel.release` 必须为 `6.18.60-sq2-android1` |
| toolchains | 项目工具链布局：`sq2-graphics/android-ndk-r28c`、Mesa 26.2.4 `android_stub`、`sq2-c2-core-reference/libcutils`、`sq2-c2-native-reference/libs/binder/ndk`、`sq2-c2-aidl-generated/include` |
| interfaces | AOSP `hardware/interfaces` 冻结 AIDL 源码，版本由 `build-spx-camera-aidl.py` 指定 |
| aidl_compiler | Android SDK `android-15/aidl` |
| dependencies | `jpeg` 目录内的 `jconfig.h`/`jpeglib.h`/`jmorecfg.h`；`system-media` 内的 `camera_metadata.h`/`camera_metadata_tags.h`/`camera_vendor_tags.h`；`native-headers/libs/nativewindow/include` |
| libraries | 匹配 Android 平台的 `libcamera_metadata.so`、`libnativewindow.so`、`libjpeg.so`、`libcutils.so` |
| platform_headers | 平台 mapper5 生成头文件，包括 Stable-C IMapper 与 metadata 接口 |
| binder_library | 平台 `libbinder_ndk.so` |
| layout_headers / hidl_headers | 图形构建的布局生成头文件及 HIDL 头文件 |
| allocator_headers | `android.hardware.graphics.allocator` 的生成 NDK 头文件 |
| graphics_library_dirs | `libgrallocutils`、`libqdMetaData`、`libcutils`、平台 `libc++`、`allocator-V2`/`common-V2` NDK 接口库和 `libbinder_ndk` 所在目录，按搜索优先级排列 |
| cross_compile | ARM64 内核工具链前缀，默认 `aarch64-linux-gnu-` |

所需 AIDL 版本：`common` V2、`FMQ` V1、`graphics.common` V5、`camera.common` V1、`camera.metadata`/`device`/`provider` V3。每份冻结源目录必须含 `.hash`，平台库须与目标 Android ABI 一致。如内核尚未编译，需先准备匹配的内核 build 树。

## 编译与接入

```sh
python3 scripts/build-spx-camera-bundle.py --inputs private/camera-build.json --output out/camera-from-source
```

入口依次执行：

1. 在输出目录的源副本上应用摄像头补丁，编译 sensor/CCI/CAMCC/SPMI 和 CAMSS 模块。
2. 从冻结 AIDL 源码生成并编译 Binder 接口。
3. 编译当前仓库的 Camera HAL 及软件 ISP。
4. 在 `probe/graphics` 源副本上应用 0004/0005/0006 补丁并编译 mapper/allocator。
5. 输出 `camera/CAMERA.json` 和 13 个运行 ELF，并校验 ARM64 架构、模块 ABI 和完整性。

将输出目录传入标准 Hybrid 整包构建，总流程见 [STANDARD-BUILD](STANDARD-BUILD.zh-CN.md)：

```sh
python3 scripts/build.py full-image --graphics hybrid --private-root /path/to/platform-inputs --camera-inputs out/camera-from-source/camera --output out/full-camera-image --esp-only
```

`source` 内核模式下会针对当前内核重编 camera 模块，用户空间使用刚生成的包。构建日志位于 `build/*.log`，命令记录位于 `build/commands.json`。
