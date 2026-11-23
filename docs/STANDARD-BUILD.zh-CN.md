# Hybrid 标准构建流程与产物

默认 Hybrid 构建、Windows 依赖准备和无个人凭据整盘装配流程。

## 1. 唯一产品线

- 标准入口：`python3 scripts/build.py`，无子命令默认 `full-image`。
- 图形仅允许 `hybrid`：Mesa GLES/Turnip + QTI gralloc/mapper5/HWC，保留双 Vulkan 选择策略。
- SQ1/SQ2 共用 Linux 6.18.60 Image、配套模块和 initramfs；SMBIOS 路由选择机型 DT 频率配置。
- 摄像头、自动亮度、自动旋转和触控向导为整包必需能力。
- `compile` 为内核组件构建；`verify/package` 为历史固定包核验/复制；`--esp-only` 仅输出启动分区内容。
- 通用交付规则见 [DEVELOPMENT](DEVELOPMENT.zh-CN.md)。

## 2. 构建顺序

依赖来源、Windows 只读提取、统一下载打包与 Linux 导入见 [DEPENDENCIES](DEPENDENCIES.zh-CN.md)。

```text
共用平台依赖 + 匹配 6.18 内核构建树
 ├─ 摄像头源码 → 驱动/AIDL/Camera HAL/ISP/mapper/allocator → camera/CAMERA.json
 └─ 传感器源码 + 共用 OEM 资料 → 模块/SubHal/overlay/RPC/SLPI EFI → sensors/sensors-bundle.json
                                   ↓
full-image：准备内核源码 → 显示/A6xx 补丁 → Image/modules/DTB → 外部模块/depmod
                                   ↓
匹配内核重编模块（或显式复用已完成组件）→ Hybrid 图形集成 → 构建/校验复用触控向导 → 安装 camera/sensors
                                   ↓
Venus 休眠握手模块 + desktop-services.jar 补丁 + 一次性休眠设置迁移
                                   ↓
清理个人凭据/跳过首次设置 → 重建 initramfs → 配对 EFI/SLPI 固件/机型 DT
                                   ↓
模板系统分区 + 新 userdata/metadata → GPT/ESP/文件系统读回 → IMG/IMG.GZ/清单
```

摄像头和传感器包须在完整内核构建前准备好。传感器 SLPI 固件、型号配置、默认光学校准由 SQ1/SQ2 共用，准备一次即可。

## 3. 输入清单

| 输入 | 作用 |
| --- | --- |
| `private/build-inputs/template.img` | 原 20 GiB 布局和 Android 系统分区 |
| `kernel-6.18/source/SOURCE.json`、`source.tar.gz` | 内核/外部模块源码 |
| `kernel-6.18/toolchain.tar.gz`、`board.dts` | ARM64 工具链、DT 源码 |
| `full-image-runtime/records.json`、`files/` | Mesa/QTI/音视频等共用输入 |
| `camera/CAMERA.json` 及 ELF | 前后摄模块、provider、mapper/allocator |
| `sensors/sensors-bundle.json` 及对应文件 | ALS/加速度/SLPI/RPC/配置 |
| `suspend/desktop-services.jar` | 构建时修改 AC 息屏等待至 1000ms，取同一 Android system 原 JAR |
| Android 平台接口、生成头、链接库、NDK/SDK | camera/sensor/触控用户态构建 |

摄像头配置模板：`configs/camera-build.example.json`；字段与 ABI 要求见 [CAMERA-BUILD](CAMERA-BUILD.zh-CN.md)。传感器命令与共用输入见 [SENSORS](SENSORS.zh-CN.md)。

## 4. 执行入口

准备 `private/camera-build.json`、与 6.18 匹配的 kernel_source/kernel_build 及平台依赖：

```sh
python3 scripts/build-spx-camera-bundle.py --inputs private/camera-build.json --output out/camera-from-source
python3 scripts/build.py --camera-inputs out/camera-from-source/camera --sensor-inputs private/build-inputs/sensors --output FRESH_LINUX_OUTPUT --jobs 8
```

传感器包先用 `build-spx-sensor-bundle.py` 生成。`output` 须为 Linux 文件系统上的全新目录。

复用已完成组件时，先用 `full_image_reuse.py` 生成显式哈希清单，再执行：

```sh
python3 scripts/build.py --reuse-components REUSE.json --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT
```

仅需刷新 Venus 时先执行 `rebuild-hybrid-venus.py`：

```sh
python3 scripts/rebuild-hybrid-venus.py --reuse-components PREVIOUS_REUSE.json --kernel-build MATCHED_KOUT --source PREVIOUS_VENUS_SOURCE --output FRESH_VENUS_OUTPUT
python3 scripts/build.py --reuse-components FRESH_VENUS_OUTPUT/REUSE.json --private-root PREPARED_INPUTS --output FRESH_IMAGE_OUTPUT
```

无受影响源码变化时，可用 `full_image_reuse.py --assembled-runtime PREVIOUS_INITRAMFS` 显式复用已编译小型 helper。

## 5. 产物层级与首启清理

| 路径（相对 OUTPUT） | 含义 |
| --- | --- |
| `kernel-build/`、`kernel-export/` | 内核构建树、Image、模块 stage、DTB 与命令记录 |
| `camera-sensors/`、`camera-camss/`、传感器模块目录 | 本轮匹配内核的组件输出 |
| `paired/EFI/SQ2Nat/` | 首轮配对启动包 |
| `paired/EFI/SQ2Nat/PRIVACY.json` | 去除身份与默认跳过初始设置的处理记录 |
| `disk/work/esp-tree/` | 存储适配、正常/救援入口和 SQ1/SQ2 路由后的 ESP 树 |
| `disk/surface-pro-x-6.18-hybrid-generic.img(.gz)` | 完整稀疏磁盘镜像与压缩镜像 |
| `disk/IMAGE-MANIFEST.json`、`disk/LAYOUT.json` | 镜像结果与分区布局 |
| `FULL-IMAGE.json`、`BUILD-COMMANDS.json` | 全流程结果、命令及日志位置 |

最终组件安装后清除个人 SSH/Wi-Fi/密码与旧 setup 跳过脚本，不复制旧 userdata/metadata、persist、FRP 及原始转储。`surface-first-boot` 为用户 0 设置 `device_provisioned=1` / `user_setup_complete=1` 并写入 userdata 标记；`PRIVACY.json` 记录 `first_setup_preserved=false`、`first_setup_skipped=true`。

触摸校准向导使用 `probe/touch-calibration/` 源码构建；复用构建时若仅向导 Java/Manifest 变化，旧 APK 自动失效并重编。当前成品信息与边界见 [IMAGE](IMAGE.zh-CN.md)。
