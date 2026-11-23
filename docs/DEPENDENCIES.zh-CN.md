# Hybrid 6.18：依赖来源、Windows 采集与离线交接

适用 Surface Pro X SQ1/SQ2 共用镜像（保留摄像头、自动亮度、自动旋转与触控）。Windows 负责下载、只读采集与打包；Linux x86_64 负责 ARM64 交叉构建。

## 1. 按来源分类

| 来源 | 依赖 / 用途 | 获取渠道及准备方式 |
|---|---|---|
| 本项目公开源码 | 6.18 适配补丁、SoundWire/CAMSS/ALS/SLPI 驱动、Camera HAL/ISP、sensor SubHal、触控向导、图形/视频包装层 | [本仓库](https://github.com/lhf1a/googlebookos_surface_pro_x)固定提交。 |
| 其他公开仓库 | firmware 工具、gbos-vm 参考、minigbm、cutils/log 头、hexagonrpc、子模块与嵌套配置 | `configs/hybrid-dependencies.json.repositories` 固定 HTTPS URL 与 commit；Windows 脚本保存 Git bundle。 |
| 官方/上游下载 | Googlebook mica recovery、Cuttlefish 安全组件、Mesa、libdrm、libsepol、NDK r28c、SDK API35/build-tools35 | manifest `downloads` 固定 URL/hash（源自 `configs/sources.lock.json`、`configs/graphics-sources.lock.json`）。 |
| OEM 共用硬件资产 | ADSP 音频固件、CDSP、SLPI、GPU vendor、WLAN/BT、modem/MCFG、传感器型号配置、ALS 默认校准 | 原 Surface Windows DriverStore 或官方恢复镜像 Windows 目录，共用采集一次。 |
| OEM Windows 启动文件 | `tcblaunch.exe` | 只读复制 `Windows/System32` 对应文件；`start.efi`/`el2launch.efi` 与 `qebspilaa64.efi` 须由项目源码构建。 |
| 第三方归档的 QTI 二进制 | Quest 2 v77 的 gralloc/metadata、libgsl/adreno_utils、原生 Vulkan/QGL 等 | 固定 OTA build `51584880050400150` 位于 `configs/quest2-full-donor-candidate.json`，使用 `-IncludeThirdParty` 启用。 |
| QTI/AOSP 公开源码构建 | Display/HWC/SDM、mapper5/allocator、OMX、HIDL、Stagefright | 配合本仓构建脚本与匹配 Android 平台头/链接库编译。 |
| 需配套平台树的重建项 | Camera AIDL/HIDL/mapper/allocator 生成头与链接库、sensor HAL 链接库 | [AOSP](https://android.googlesource.com/) 对应版本源码及平台产物，路径填入 `configs/camera-build.example.json` 副本。 |
| 本仓内核重建配方 | 官方 6.18.60 + 项目 delta + 外部模块源码与 DT 覆盖 | `configs/hybrid-kernel-source.json`、`patches/kernel-6.18-surface-baseline.patch`、`source-dependencies/kernel-6.18/`。 |
| 需另行准备的输入 | `board.dts`/EFI、完整 Android/QTI runtime 与模板布局 | 尚无统一脚本从零生成，须作为明确输入提供。 |

Linux 6.3 的 `surface_kernel` 锁仅为历史基线，不作为 6.18 源码下载。ALS 默认光学校准仅从 `SurfaceLightSensor.sys` 提取带 CRC 的 1480 字节型号数据。

### Android/QTI 来源分工

| 分工 | 固定来源 / 配方 | 进入 Hybrid 的内容 |
|---|---|---|
| Android 基础与框架 | mica recovery `16471258`、Cuttlefish `16373615`、`quest2-hidl-build.json`（AOSP android-16.0.0_r1）、`frameworks/av` `e2f098935447ca4945946de5cb69db843fe3f003` | 基础分区、软件 KeyMint/Gatekeeper、源码 HIDL 支撑与 Stagefright/P010 路径 |
| Quest 2 v77 闭源核心 | `quest2-full-donor-candidate.json`、`quest2-v77-graphics-reference.json`（V@0819.0 / GIT@274d25476a） | `gralloc.kona`、`gralloccore`、`qdMetaData`、`qcodec2_utils`、`gsl`、`adreno_utils`、原生 QTI Vulkan/QGL（由 `build-quest2-native-vulkan.py` 重定向；桌面 EGL/GLES 由 Mesa 承担） |
| QTI CAF + AOSP 源码 HAL | SM8350 display `e9bbe9a980d408af36b52f2abaae7c35647b7425`；SM8250 media `05f3cd8dbfa66f2e79ffda3cb294697374510160`；oculus kernel 头 `6f22d8e7cb7b48f3026f72bc3dbefef787a62ecc`；mapper5 配方 | `build-sm8350-display.py`（HWC/SDM）、`build-quest2-mapper5.py`（mapper/allocator）、`build-sm8250-omx.py`（OMX） |
| Mesa / libdrm | Mesa 26.2.4、libdrm 2.4.133、NDK r28c、`gbos-vm` mapper5 patch | Freedreno EGL/GLES、Turnip、共享 `libdrm` |
| 本仓粘合层 | `probe/graphics` 与 `scripts/` | Vulkan 分流、Mesa→QTI gralloc 桥接、线性 RGB layout façade、SDM 背光重定向、dma_heap/ION 与 Vulkan 内存适配 |

### Quest 2 下载与提取

[归档索引](https://cocaine.trade/Quest_2_firmware) 固定 build `51584880050400150`（ZIP SHA256 `f09e3a0de4d5c345975b0869d0049a339fba820cb178039287120e7247775e1b`）。默认仅下载 96,436 字节 prefix 与 84,162,032 字节 vendor 区段，加 `-FullDonorArchive` 才下载完整 1,172,543,128 字节归档。

Linux 侧使用 `rebuild-quest2-v77-vendor.py` 与 `extract-quest2-v77-reference.py`（donor 来源见 [PROVENANCE](PROVENANCE.zh-CN.md)），`hybrid_bootstrap.py --stage quest` 已将区段校验、vendor 重建与只读 debugfs 提取串联。

### 官方渠道

- [微软 Surface Pro 驱动](https://support.microsoft.com/en-us/surface/drivers-firmware/download-drivers-and-firmware-for-surface-pro)（Pro X 使用 Windows Update）
- [微软恢复镜像](https://support.microsoft.com/en-us/surface-recovery-image)
- [linux-surface firmware 工具](https://github.com/linux-surface/aarch64-firmware)
- [NDK 官方入口](https://developer.android.com/ndk/downloads)（本项目锁定 r28c Linux x86_64）

## 2. 原 Surface Windows：统一入口

需 Git for Windows、系统 `curl.exe`/`tar.exe` 与 PowerShell 5.1+；输出盘需 NTFS/exFAT 并预留空间（下载约 7.6 GB）。如读取 DriverStore 被拒绝，以管理员打开 PowerShell。

```powershell
# 预览清单（不联网、不创建输出）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps -Plan

# 下载、提取并统一打包（Output 须不存在）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps -IncludeThirdParty

# 使用本地缓存目录
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps-2 -CacheDirectory D:\spx-download-cache
```

产物为 `D:\spx-deps.tar`、`.sha256.json` 及展开目录，其中 `MANIFEST.json` 记录逐文件 SHA256、大小、来源提交与 `unresolved` 项。

离线导出设备资产：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Export-SurfaceHybridDependencies.ps1 -Output D:\spx-oem
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps-3 -SurfaceExport D:\spx-oem
# 或指定解包的官方恢复 Windows 树
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Export-SurfaceHybridDependencies.ps1 -Output D:\spx-oem-recovery -WindowsRoot E:\recovery\Windows
```

导出仅访问指定 INF 前缀的 DriverStore 包与 `tcblaunch.exe`，不读取账户、Wi-Fi、SSH 密钥、注册表或 modem NV/EFS。若新旧驱动同名文件内容冲突，Linux 导入会报错，需选定匹配版本后重新采集。

### 本地补充归档

可通过 `-LocalInputs D:\local-inputs.json` 合入明确列出的普通文件：

```json
{
  "files": [
    {
      "id": "qti-v77-vendor",
      "path": "D:\\authorized-donors\\vendor.img",
      "sha256": "填入该文件真实的64位SHA256",
      "provenance": "实际来源、版本及用途",
      "personal_state_absent": true
    }
  ]
}
```

`-SkipDownloads/-SkipRepositories/-SkipSurface` 供分段排查；下载失败保留 `.part` 供检查，重试需选新 Output。

## 3. 拷贝到构建主机并校验

Linux 主机要求 Ubuntu 24.04 x86_64、Python 3.12+、`python3-venv`、Git、`patch`、GCC/G++、`make`、`aarch64-linux-gnu` 工具链、`ninja`、`pkg-config`、`flex`/`bison`、`glslang-tools`、JDK、`patchelf` 及设备树/ELF/EROFS 工具。将 tar 与 SHA256 receipt 拷贝到 `downloads/`，构建输出放在 Linux 文件系统：

```sh
python3 scripts/import-hybrid-dependencies.py \
  --archive downloads/spx-deps.tar --sha256 SHA256 \
  --output private/dependency-import
```

导入校验外包、成员大小/hash 与下载锁后，生成 `SURFACE-PREPARATION.json`，整理 DSP/SLPI 固件、型号 sensor config 并运行 CRC 校准提取器。

恢复 Git 源码示例：

```sh
git clone private/dependency-import/repositories/hexagonrpc.bundle upstream/hexagonrpc
git -C upstream/hexagonrpc checkout --detach da7a3742ab8d5273a281b70ee11f16fb2cb6a690
```

`device_faker` 的 config 单独提供为 `device_faker_config.bundle`；其他仓库子模块须按其 gitlink 另行准备。NDK/SDK ZIP 与 Mesa tar 按 [CAMERA-BUILD](CAMERA-BUILD.zh-CN.md)、[SENSORS](SENSORS.zh-CN.md) 路径解包构建。

## 4. 从空构建目录到镜像

1. 克隆本仓及固定子模块，安装 Linux 构建依赖并完成 Windows 交接。
2. 准备匹配的内核与 external 源码、`board.dts`、EFI 与工具链。
3. 制备模板与清除个人状态的 runtime。
4. 按 CAMERA-BUILD 从源码生成 `camera/CAMERA.json`，按 SENSORS 构建 sensor HAL/RPC/overlay/模块/EFI 并生成 `sensors-bundle.json`。
5. 检查输入完整性：

```sh
python3 scripts/import-hybrid-dependencies.py --private-root private/build-inputs
```

6. 输入齐备后启动默认 Hybrid 构建（交付规则见 [STANDARD-BUILD](STANDARD-BUILD.zh-CN.md)）：

```sh
python3 scripts/import-hybrid-dependencies.py --private-root private/build-inputs \
  --build --build-output /home/USER/spx-build/hybrid-001 --jobs 8
```

## 5. 离线 Bootstrap 阶段

导入后可直接执行或分阶段调用 `hybrid_bootstrap.py`（每次 `--work` 使用新目录）：

```sh
python3 scripts/import-hybrid-dependencies.py \
  --archive downloads/spx-deps.tar --sha256 SHA256 \
  --output private/dependency-import \
  --bootstrap-work /home/USER/spx-build/sources-001 --bootstrap-stage sources

python3 scripts/hybrid_bootstrap.py --imported private/dependency-import \
  --work /home/USER/spx-build/quest-001 --stage quest
```

| 阶段 | 执行的操作 |
|---|---|
| `sources` | 从 Windows Git bundles 离线 clone/checkout 并核对固定 commit |
| `toolchains` | 校验并解包 NDK/SDK、Mesa/libdrm 归档，保留可执行位与内部链接 |
| `kernel` | kernel.org 6.18.60 + 项目 patch + external 源码，生成 `SOURCE.json`/`source.tar.gz` |
| `platform` | 编译 AIDL、摄像头 provider、mapper/allocator、传感器 HIDL HAL 与旋转 overlay，生成 source-only camera 输入 |
| `mesa` | 使用离线 Python wheels 与 `--wrap-mode=nodownload` 编译 6 个 ARM64 库（EGL/GLES/Turnip/libdrm） |
| `quest` | 校验 prefix/vendor 区段或切分完整 ZIP，执行 131-operation 重建与只读 debugfs 提取 |
| `android` | 校验 mica ZIP/RAW 与 Cuttlefish sparse super，提取 A 槽分区及平台文件 |
| `glue` | 用 `--ndk` 从本仓源码编译两个 ARM64 粘合库并组织 `runtime-overlay/` |
| `assemble` | 显式复用已完成组件与 runtime 生成整盘镜像 |
| `all` | 顺序执行源码准备阶段；因 HWC/OMX/runtime 从零制备尚未闭合，末尾退出非零 |

通用启动载荷还需 `spx-boot-gnu-efi.bundle`：checkout 后将 `sources/spx-boot-gnu-efi` 作为 `--boot-gnu-efi` 参数或置于 `kernel-6.18/gnu-efi`（勿替换 SLPI 加载器使用的 `qebspil-gnu-efi`）。

独立验证命令示例：

```sh
python3 scripts/hybrid_bootstrap.py --stage mesa \
  --imported private/dependency-import --ndk NDK_DIRECTORY \
  --work /home/USER/spx-build/mesa-001 --jobs 8
```

## 6. 传感器提取与组件复用装配

在 SQ1 或 SQ2 原 Windows 中运行 `Prepare-HybridDependencies.ps1 -Output D:\spx-dependencies -IncludeThirdParty`（或 `Export-SurfaceHybridDependencies.ps1 -Output D:\spx-oem -RequireSensors` 仅提取 OEM 文件），默认从 `Windows\System32\DriverStore\FileRepository` 提取：

- `surfaceprox_subextscss.inf_*`：`qcslpi8180.mbn`（SLPI 固件）
- `surfaceprox_sensorsconfig.inf_*`：23 项型号配置（22 个 JSON 与 `config_list.txt`）
- `surfacelightsensor.inf_*`：`SurfaceLightSensor.sys`（Linux 导入时提取 1480 字节型号默认光学校准）

导入产物位于 `surface-assets/sensors/` 与 `surface-assets/firmware/qcom/msft/surface/pro-x/`。缺项或版本冲突时默认中止并写入 `SURFACE-EXPORT.json`，可通过 Windows Update 补齐或用 `-WindowsRoot X:\Windows` 指定完整恢复目录。

传感器运行时与整盘复用装配：

1. 使用 `scripts/build-spx-sensor-runtime.py` 传入 `--surface-assets`、`--kernel-components`、`--platform-components`、`--sources-root`（含 `hexagonrpc`、`qebspil`、`qebspil-gnu-efi`、`qebspil-dtc`）与 `--venus-firmware` 生成完整 sensors bundle（已有 RPC/EFI 可传 `--reuse-rpc`、`--reuse-efi`）。
2. 如需单独刷新 Venus，按 [SUSPEND](SUSPEND.zh-CN.md) 执行 `rebuild-hybrid-venus.py`。
3. 用 `full_image_reuse.py` 记录产物 SHA256 并构建整盘（需设置 `SQ2_ANDROID_SDK` 编译触控向导）：

```sh
python3 scripts/full_image_reuse.py --kernel KERNEL_EXPORT --camera CAMERA_COMPLETE \
  --sensors SENSOR_BUNDLE --mesa MESA_RUNTIME --glue GLUE_OVERLAY --output reuse.json
python3 scripts/build.py --reuse-components reuse.json --private-root PREPARED_INPUTS --output BUILD_OUTPUT
# 或
python3 scripts/hybrid_bootstrap.py --stage assemble --reuse-components reuse.json --private-root PREPARED_INPUTS --work BUILD_WORK
```

更多构建细节见 [BUILD](BUILD.zh-CN.md)。
