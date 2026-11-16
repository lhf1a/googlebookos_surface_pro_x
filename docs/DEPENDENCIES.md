# Hybrid 6.18：依赖来源、Windows 采集与离线交接

适用：Surface Pro X SQ1/SQ2 共用镜像；摄像头、自动亮度、自动旋转、触控均保留。
Windows 负责下载/只读采集/打包；Linux x86_64 负责 ARM64 交叉构建。Windows ARM64 上不执行 Linux NDK。

**当前进度：统一采集、校验、打包、导入入口已提供；仅有公开下载和 Windows 导出还不能从零生成完整镜像。**
6.18 内核已有公开重建配方；剩余缺口主要是 Android/QTI runtime 的完整准备、板级 EFI/模板及配套平台输入。
这些缺项在 `MANIFEST.json.unresolved` 中保留，不以旧镜像或空文件冒充。

## 1. 按来源分类

| 来源 | 依赖 / 用途 | 获取渠道及准备方式 |
|---|---|---|
| 本项目公开源码 | 6.18 适配补丁、SoundWire/CAMSS/ALS/SLPI 驱动、Camera HAL/ISP、sensor SubHal、触控向导、图形/视频包装层 | [本仓库](https://github.com/lhf1a/googlebookos_surface_pro_x)。克隆本项目并固定提交；不再额外应用聊天中的工作区补丁，不从 Windows 提取 Android HAL。 |
| 其他公开仓库 | firmware 工具及部分 GMU/SQE、gbos-vm 参考、minigbm、cutils/log 头、hexagonrpc、两个项目子模块和嵌套配置 | `configs/hybrid-dependencies.json.repositories` 逐项列 HTTPS URL 和 commit；Windows 脚本保存完整 Git bundle，不在 Windows checkout Linux 符号链接。 |
| 官方/上游下载 | Googlebook mica recovery、Cuttlefish 安全组件、Mesa、libdrm、libsepol、NDK r28c、SDK API35/build-tools35 | 同一 manifest 的 downloads 固定 URL/hash。原始锁来自 `configs/sources.lock.json` 和 `configs/graphics-sources.lock.json`。下载后校验；不是自动升级到最新版。 |
| OEM 共用硬件资产 | ADSP **音频固件**、CDSP、SLPI、GPU vendor、WLAN/BT、modem/MCFG、传感器型号配置、ALS 默认校准 | 原 Surface Windows DriverStore，或对应官方恢复镜像的 Windows 目录。共用依赖采集一次；不是每台设备一套。音频 HAL 与音频固件是两个不同输入。 |
| OEM Windows 启动文件 | `tcblaunch.exe` | 只读复制 Windows/System32 对应文件；缺失写入报告。`start.efi`/`el2launch.efi` 和增加 SLPI PAS12 的 `qebspilaa64.efi` 仍需项目配套构建，不能用任意 Windows EFI 替换。 |
| 第三方归档的 QTI 二进制 | Quest2 v77 的 gralloc/metadata、libgsl/adreno_utils、原生 Vulkan/QGL 等 | 固定 OTA build `51584880050400150` 的 URL、大小及站点公布 SHA256 位于 `configs/quest2-full-donor-candidate.json`；统一清单已纳入下载，使用 `-IncludeThirdParty` 启用。不能把第三方归档称为 Meta 官方认证输入。 |
| QTI/AOSP 公开源码构建 | Display/HWC/SDM、mapper5/allocator、OMX、HIDL、Stagefright | 下节列明各自仓库、固定提交及本仓构建脚本；不再把 OMX/HWC 笼统归为闭源 donor。仍需匹配的 Android 平台头/链接库与本地补丁。 |
| 可重建但需配套平台树 | Camera AIDL/HIDL/mapper/allocator 生成头、链接库，sensor HAL 链接库 | [AOSP](https://android.googlesource.com/) 对应版本源码及匹配平台构建产物；NDK 自身不含这些私有平台 ABI。路径填入 `configs/camera-build.example.json` 的副本。 |
| 本仓内核重建配方 | 官方 6.18.60 + 精确项目 delta + 外部模块源码及配套 DT 覆盖 | `configs/hybrid-kernel-source.json`、`patches/kernel-6.18-surface-baseline.patch`、`source-dependencies/kernel-6.18/`；`hybrid_bootstrap.py --stage kernel` 不需要旧本地源码归档。 |
| 当前尚未公开闭合的构建输入 | board.dts/EFI、完整 Android/QTI runtime 与模板布局 | 按现有平台组合配方准备并使用明确输入；统一原始依赖包尚不能自动生成全部这些内容。这是从零构建缺口，不是 Surface 提取项。 |

Linux 6.3 的 `surface_kernel` 锁只是历史诊断基线，**不下载它作为本次 6.18 源码**。
摄像头走源码构建，不需要采集 Windows 摄像头驱动或用户照片。
ALS 默认光学校准仅从 `SurfaceLightSensor.sys` 中提取带 CRC 的 1480 字节型号数据；不读取实际设备的学习/校准状态。

### Android/QTI 实际来源分工（4 类上游 + 本仓粘合层）

| 分工 | 固定来源 / 配方 | 进入 Hybrid 的内容 |
|---|---|---|
| Android 基础与框架 | mica recovery `16471258`、Cuttlefish `16373615`；`quest2-hidl-build.json` 的 AOSP android-16.0.0_r1；frameworks/av `e2f098935447ca4945946de5cb69db843fe3f003` | 基础分区、软件 KeyMint/Gatekeeper；源码构建 HIDL 支撑与打补丁的 Stagefright/P010 路径。这里使用锁文件中的 mica 名称，不推导另一设备型号。 |
| Quest 2 v77 闭源核心 | `quest2-full-donor-candidate.json` 与 `quest2-v77-graphics-reference.json`；V@0819.0 / GIT@274d25476a | gralloc.kona、gralloccore、qdMetaData、qcodec2_utils、gsl、adreno_utils；原生 QTI Vulkan/QGL。`build-quest2-native-vulkan.py` 重定向符号/路径。Hybrid 剥离 donor EGL/GLES，由 Mesa 承担桌面 EGL/GLES。 |
| QTI CAF + AOSP 源码 HAL | SM8350 display `e9bbe9a980d408af36b52f2abaae7c35647b7425`；SM8250 media `05f3cd8dbfa66f2e79ffda3cb294697374510160`；oculus kernel 头 `6f22d8e7cb7b48f3026f72bc3dbefef787a62ecc`；mapper 接口和 SM8150 gralloc 头见 mapper5 配方 | `build-sm8350-display.py` 构建 HWC/SDM 等；`build-quest2-mapper5.py` 构建 mapper/allocator；`build-sm8250-omx.py` 配合本仓服务源码与补丁构建 OMX。不是将 Quest 原厂 HWC/OMX 整套直接复制到 Surface。 |
| Mesa / libdrm | Mesa 26.2.4、libdrm 2.4.133、NDK r28c、gbos-vm mapper5 patch | Freedreno EGL/GLES、Turnip、共享 libdrm。 |
| 本仓粘合层 | `probe/graphics` 与 `scripts/` | Vulkan 分流、Mesa→QTI gralloc 桥接、线性 RGB layout façade、SDM 背光重定向、dma_heap/ION 与 Vulkan 内存适配；不另行向 Surface Windows 提取这些库。 |

统一清单的 `runtime_groups` 是上述分类的机器可读版本。源码记录与当前成品 ELF 的身份是两层证据：旧配置中保留的 candidate/not-deployed 状态不被这份来源表改写，也不据此新增硬件验收结论。

### Quest 2 下载与提取

[归档索引](https://cocaine.trade/Quest_2_firmware) 当前列有 build `51584880050400150`，公布 ZIP SHA256：
`f09e3a0de4d5c345975b0869d0049a339fba820cb178039287120e7247775e1b`。
固定下载地址已纳入清单。默认仅下载已固定 SHA256 的 96,436 字节 prefix 与 84,162,032 字节 vendor 区段；两者已实际完成 131 操作及最终 vendor hash 核对。加 `-FullDonorArchive` 才额外下载并核验完整 1,172,543,128 字节归档。
历史配置 `archive_hash_verified=false` 不改成 true：历史证据是 vendor 操作、分区与文件 hash，本次添加来源不等于重新下载验证整个 ZIP。

Linux 侧继续使用已有 `rebuild-quest2-v77-vendor.py` 和 `extract-quest2-v77-reference.py`，详见 [QUEST2-VENDOR-REFERENCE](QUEST2-VENDOR-REFERENCE.md)。前者接收固定 prefix/vendor range，并核验 131 个 REPLACE_XZ 操作及完整 vendor 分区 SHA；后者只读 debugfs 提取。
`hybrid_bootstrap.py --stage quest` 已将区段校验（或完整 ZIP 校验及区段切分）、vendor 重建和只读文件提取串联；区段偏移来自上述锁。两个区段不是两个独立 donor。
不刷写 Quest OTA/分区到 Surface，不把 A650 firmware 当作 Surface A680 firmware。

### 官方渠道说明

- [微软 Surface Pro 驱动渠道](https://support.microsoft.com/en-us/surface/drivers-firmware/download-drivers-and-firmware-for-surface-pro)：Pro X 使用 Windows Update；不套用 Intel Surface 的 MSI。
- [微软恢复镜像](https://support.microsoft.com/en-us/surface-recovery-image)：可人工获取匹配的恢复镜像；网站要求的账号/序列号不记录进依赖包。
- [linux-surface firmware 工具说明](https://github.com/linux-surface/aarch64-firmware)：支持从 Windows 安装/恢复镜像提取。仓库含部分固件，不等于已涵盖全部 OEM 文件。其脚本还会联网并转换 WLAN/BT/Venus；本包先保留原始 OEM 文件，不盲目改名为 Linux firmware。
- [NDK 官方入口](https://developer.android.com/ndk/downloads)：本项目仍用锁定的 r28c Linux x86_64 包，而非页面当前最新版。

下载可用性与许可是两回事。包默认本地使用，不提交 Git；公开分发前分别确认 OEM、Google、QTI donor 的许可。

## 2. 原 Surface Windows：统一入口

准备 Git for Windows、系统 `curl.exe`/`tar.exe`；PowerShell 5.1 或更新版本。下载含约 7.6 GB recovery、Cuttlefish、NDK及 Git 历史，输出盘留足空间；用 NTFS/exFAT，不用 FAT32。
如读取 DriverStore 被拒绝，以管理员打开 PowerShell 重试；脚本不安装驱动、不改注册表、不改启动项。

在仓库根目录先查看清单（不联网、不创建输出）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps -Plan
```

实际下载、提取并统一打包（Output 必须不存在；`-IncludeThirdParty` 同意获取上表所列第三方 Quest donor）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps -IncludeThirdParty
```

已有官方下载可放入缓存，文件名须与 manifest 的 `file` 相同；仍强制校验：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps-2 -CacheDirectory D:\spx-download-cache
```

产物：`D:\spx-deps.tar`、旁边的 `.sha256.json`，以及保留的展开目录。
`MANIFEST.json` 包含逐文件 SHA256/大小、来源提交、下载结果、未解决的准备步骤。
`build_ready=false` 表示这是原始依赖交接包，不是完成的 build-inputs。

只导出设备资产，也可以离线执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Export-SurfaceHybridDependencies.ps1 -Output D:\spx-oem
# 主机使用此前导出的同一套共用资产，不需要再到每台设备上采集
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-deps-3 -SurfaceExport D:\spx-oem
# 替代来源：解包的官方恢复 Windows 树，不是 recovery ZIP 本身
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Export-SurfaceHybridDependencies.ps1 -Output D:\spx-oem-recovery -WindowsRoot E:\recovery\Windows
```

导出仅访问指定 INF 前缀的 DriverStore 包和 tcblaunch.exe；保留所有安装版本的目录名。
不读取账户、用户目录、Wi-Fi profile、SSH key、注册表、磁盘原始分区、modem NV/EFS 或 live persist。
旧/新驱动同名文件内容不同，Linux 导入会报告 conflict，需选定匹配版本后重新采集，不能按枚举顺序偷偷选一个。

### 另备 Android/QTI 或已整理源码归档

其他本地补充输入（Quest donor 已有固定下载项）可用 `-LocalInputs D:\local-inputs.json` 合入同一 tar。
清单只允许明确列出的普通文件，不递归打包整个 private/ 或用户目录：

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

用 `Get-FileHash -Algorithm SHA256` 取得 hash。`personal_state_absent` 是提供者审核声明，不是内容扫描证明。
不允许填入含账户的完整设备备份/旧运行包；Android 只读 donor 分区和型号固件与个人 userdata 区分。
这些本地输入保存在 `local/`，导入时不执行、不自动覆盖 build-inputs；unresolved 不会因为任意同名文件就消失。

`-SkipDownloads/-SkipRepositories/-SkipSurface` 供分段排查，跳过项明确记录为 skipped，不能用它们产生“完整”结论。
下载失败保留 `.part`/失败目录供检查；重新执行选新 Output，缓存仅接受完整且 hash 匹配的文件。

## 3. 拷贝到构建主机并校验

把 tar 和 SHA256 receipt 拷贝到 Linux 构建目录 `downloads/`。安装 Python 3.12+、Git 和构建工具；内核输出在 Linux 文件系统，不在 `/mnt/c`。

```sh
# 在项目根目录；SHA256 用 Windows receipt 中的实际值
python3 scripts/import-hybrid-dependencies.py \
  --archive downloads/spx-deps.tar --sha256 SHA256 \
  --output private/dependency-import
```

导入先校验外包、全部成员大小/hash、下载锁；拒绝额外文件、路径穿越、软/硬链接和重名。
然后生成 `SURFACE-PREPARATION.json`，整理唯一版本的 DSP/SLPI 固件、型号 sensor config，并实际运行 CRC 校准提取器。
WLAN/BT/Venus 的二次转换未冒充完成；原始 DriverStore 文件保留，供固定 firmware recipe 使用。

恢复 Git 源码示例（用 manifest 中对应 commit，不用远端 HEAD）：

```sh
git clone private/dependency-import/repositories/hexagonrpc.bundle upstream/hexagonrpc
git -C upstream/hexagonrpc checkout --detach da7a3742ab8d5273a281b70ee11f16fb2cb6a690
```

Git bundle 不自动包含子模块内容。device_faker 的 config 单独提供为 `device_faker_config.bundle`；其他仓库的子模块须按其 gitlink 另行准备，不默认为离线齐全。
NDK/SDK ZIP 和 Mesa tar 仍在 downloads/：按 [CAMERA-BUILD](CAMERA-BUILD.md)、[SENSORS](SENSORS.md) 的路径解包/构建；导入不擅自运行外来脚本。

## 4. 从空构建目录到镜像：实际阶段与缺口

1. 克隆本项目及固定子模块，安装 Linux 构建依赖，完成上述 Windows 交接。
2. 取得/重建匹配的 prepared kernel source + external source、board.dts、EFI、toolchain。内核与 external 已由 `kernel` 阶段从固定公共归档加本仓源码差异重建，不再需要维护者历史 source.tar.gz；board/EFI 仍须接通。
3. 由 recovery/donor 制备模板与已清除个人状态的 runtime；当前尚无将第 2/3 步全部公开原料自动转换为 build-inputs 的统一配方。
4. 配套 Android platform 头/库就位，按 CAMERA-BUILD 从源码生成 camera/CAMERA.json。按 SENSORS 使用共用 SLPI/config/默认校准，源码构建 sensor HAL/RPC/overlay/模块/EFI 并生成 sensors-bundle.json。
5. 检查输入，缺项列出并退出 2，不跳过摄像头/传感器：

```sh
python3 scripts/import-hybrid-dependencies.py --private-root private/build-inputs
```

6. 输入齐备后，统一调用现有默认 Hybrid/source 构建，失败码原样传播：

```sh
python3 scripts/import-hybrid-dependencies.py --private-root private/build-inputs \
  --build --build-output /home/USER/spx-build/hybrid-001 --jobs 8
```

presence preflight 通过不等于编译成功；完整构建还会校验 runtime、模板、工具链及 ABI。
最终产物和匿名化规则见 [STANDARD-BUILD](STANDARD-BUILD.md)：新 userdata/metadata、首次设置保留、移除个人 SSH/Wi-Fi/账户材料。
只有 `FULL-IMAGE.json`、实际 IMG/IMG.GZ 及读回验证完成后，才是完整交付。当前文档不把脚本测试或原始依赖打包称作该事件。


## 5. 已接入的离线 bootstrap 阶段（2026-11-16）

Windows 统一准备新增 Android CI artifact 网页解析；不会把网页当作 Cuttlefish ZIP。
Linux 可在导入完成后直接执行已实现阶段：

```sh
python3 scripts/import-hybrid-dependencies.py \
  --archive downloads/spx-deps.tar --sha256 SHA256 \
  --output private/dependency-import \
  --bootstrap-work /home/USER/spx-build/sources-001 --bootstrap-stage sources
```

或对已导入目录执行独立阶段，每次 work 使用新目录：

```sh
python3 scripts/hybrid_bootstrap.py --imported private/dependency-import \
  --work /home/USER/spx-build/quest-001 --stage quest
```

| 阶段 | 已执行的操作 |
|---|---|
| `sources` | 从 Windows Git bundles 离线 clone/checkout，逐仓库核对固定 commit，不访问远端 |
| `toolchains` | 校验 NDK/SDK、Mesa/libdrm 归档，解包到当前 workspace，保留 SDK 可执行位/内部链接 |
| `kernel` | kernel.org 6.18.60 + 逐字核对的项目 patch + external 源码；生成 full-image 接受的 SOURCE.json/source.tar.gz |
| `platform` | AIDL、摄像头 provider、带摄像头 BLOB/YUV 适配的 mapper/allocator、传感器 HIDL HAL 和旋转框架 overlay，生成 source-only camera 输入 |
| `mesa` | 固定 Mesa/libdrm/Expat 与离线 Python wheels，Meson 禁止下载，编译 EGL/GLES/Turnip/libdrm 六个 ARM64 库 |
| `quest` | 校验已固定的 prefix/vendor 区段；也接受完整 ZIP 并切分，执行既有 131-operation 重建与只读 debugfs 提取 |
| `android` | 校验 mica ZIP/RAW、GPT及 LP 元数据，提取 A 槽分区；展开 Cuttlefish sparse super、提取 LP，再用 fsck.erofs 只读导出平台文件 |
| `glue` | 使用显式 NDK 从本仓源码编译两个 ARM64 库并核验 ELF；按 runtime 路径组织 `runtime-overlay/`，不读取隐式旧缓存 |
| `assemble` | 显式复用完成组件与已准备 runtime，生成整盘镜像；参数和本轮实跑见文末 |
| `all` | 顺序执行源码准备阶段；仅凭下载/Windows 导出从零准备 HWC/OMX/runtime 尚未闭合，末尾明确退出非零 |

隔离验证 glue 的调用为 `--stage glue --ndk NDK_DIRECTORY --work FRESH_OUTPUT`。
`BOOTSTRAP.json` 保存完成阶段、命令、退出码、日志 hash、产物 hash和失败点；`image_built` 仍为 false。
这些新增阶段没有跳过 full-image 必需的 camera/sensors，也没有用旧 initramfs 冒充从零生成的 runtime。
后续已接通传感器 RPC/EFI/固件打包和显式复用 runtime 的整盘装配；这不等于下载包可从零生成 HWC/OMX/runtime，见文末及 HYBRID-ASSEMBLY-20261116.md。

本轮实跑：Windows 将实际 Quest 两个区段和固定 hexagonrpc Git bundle 打包，Linux importer 随后调用 quest 阶段完成重建/提取（906 个普通文件）；独立离线恢复 hexagonrpc commit 成功。两个 Hybrid 粘合库已用明确指定的本机 NDK r28c 实编译为 ARM64 ELF，输出映射与 full_image.py 对齐。此实跑包未包含全部 Android/SDK/Surface 依赖，NDK也不是由该包解出，因此不是完整 clean-room 构建验收。


## 6. 本轮实际编译与可复用结果

- `configs/hybrid-kernel-source.json` 固定 kernel.org 6.18.60、111 文件项目差异及 external 源码包；与原 prepared 输入的 91,244 个内核普通文件逐字相等。已实际编译 Image、内核与 external 模块，再编译摄像头和传感器配套模块；没有恢复 KGSL/SDE 开发栈。
- `platform` 从固定 Android 头和只读分区库编译 179 个 AIDL 对象、摄像头 provider、mapper/allocator、传感器 sub-HAL/测试程序与已签名 overlay。签名 key 使用本次 work 独立文件，不进入依赖交接包。
- 摄像头 `CAMERA.json` schema 2 只含三个用户态产物。full-image 的 source 模式才接受它，随后必须按当前内核重编全部摄像头模块；prebuilt 模式拒绝，不以空模块代替。
- `mesa` 已在 `--wrap-mode=nodownload` 下构建成功。NDK 的 static-libstdc++ 参数仅在链接时生效；保留 upstream 的编译器特性检测，未用关闭编译错误代替修复。输出 Turnip 不含历史 3DMark 专用 sysmem 标记。
- Windows 下载器支持 Gitiles base64 解码后校验，29 项 HIDL 输入还在 Linux 重验 Git tree。新增 mapper/allocator 参考文件逐 SHA256 校验。

Linux 主机要求 Ubuntu 24.04 x86_64 / Python 3.12、python3-venv、Git、patch、GCC/G++/make、aarch64-linux-gnu 工具链、ninja、pkg-config、flex/bison、glslang-tools、JDK、设备树/ELF/EROFS 工具；Python 构建依赖 wheels 由 Windows 包提供，Mesa 阶段不联网安装。

独立 Mesa 验证：

```sh
python3 scripts/hybrid_bootstrap.py --stage mesa \
  --imported private/dependency-import --ndk NDK_DIRECTORY \
  --work /home/USER/spx-build/mesa-001 --jobs 8
```

独立 platform 阶段的 `--sources-root`、`--toolchains-root`、`--platform-files`、`--quest-vendor` 指向明确准备的输入，不自动搜索旧缓存；完整 `all` 使用其本次工作目录。实跑的平台库取自现有模板只读分区、NDK 使用明确指定的现存工具链，因此不是全依赖包从零验收。后续已用完成组件生成完整 IMG（见文末）；`all` 仍因 clean-room runtime 准备缺口返回非零。
# 传感器：在 SQ1 或 SQ2 原 Windows 上提取

在 Surface Pro X SQ1 **或** SQ2 的原 Windows 中运行统一入口（PowerShell 5.1）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Prepare-HybridDependencies.ps1 -Output D:\spx-dependencies -IncludeThirdParty
```

`-IncludeThirdParty` 用于固定 Quest donor 等已注明的第三方来源，**不会下载替代的 Surface 传感器 OEM 包**。
默认从本机 `Windows\System32\DriverStore\FileRepository` 提取：

- `surfaceprox_subextscss.inf_*`：`qcslpi8180.mbn`（SLPI 固件）。
- `surfaceprox_sensorsconfig.inf_*`：23 项型号配置（22 个 JSON 和 `config_list.txt`），含旋转/加速度/陀螺仪配置。
- `surfacelightsensor.inf_*`：`SurfaceLightSensor.sys`；Linux 导入时提取并校验其中 1480 字节的**型号默认光学校准**。

SQ1/SQ2 共用这些型号资产，不复制运行中的传感器 registry/persist、每台机器校准、用户账户、Wi-Fi 或 SSH 凭据。
脚本检查 Surface Pro X ARM64 型号，不记录设备序列号。缺少任何一项传感器输入，或多个已安装版本内容不同，默认停止打包并保留 `SURFACE-EXPORT.json` 中的缺项/冲突列表。
先通过 Windows Update 补齐驱动，或用 `-WindowsRoot X:\Windows` 指定一个完整的原厂恢复 Windows 目录；不要混合不同版本。
`-AllowPartialSurface` 仅用于诊断性部分导出，不代表可完整构建。

仅提取 OEM 文件时：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Export-SurfaceHybridDependencies.ps1 -Output D:\spx-oem -RequireSensors
```

将统一入口生成的 `spx-dependencies.tar` 和 `.tar.sha256.json` 复制到 Linux 构建目录，按下文导入。
导入产物 `surface-assets/sensors/` 包含型号配置和默认校准，SLPI 位于 `surface-assets/firmware/qcom/msft/surface/pro-x/`。
`SURFACE-PREPARATION.json` 的 `sensor_oem_ready=true` 仅表示传感器 OEM 输入完整，不等同整个镜像依赖齐全。

## 复用已完成组件，继续到完整磁盘镜像

最新休眠增量使用 `rebuild-hybrid-venus.py` 在匹配 KOUT 上重编 Venus，再将其生成的 REUSE.json 交给默认 hybrid 构建；不重编未变化组件。输入配对、命令和新镜像哈希见 [HYBRID-SLEEP-20261116](HYBRID-SLEEP-20261116.md)。

`scripts/build-spx-sensor-runtime.py` 接收上述 `--surface-assets`、已编译的 `--kernel-components`、
`--platform-components`、固定 Git `--sources-root` 和 `--venus-firmware`，编译静态 RPC/PAS12 EFI 后生成完整 sensors bundle。
源目录名为 `hexagonrpc`、`qebspil`、`qebspil-gnu-efi`、`qebspil-dtc`；统一 Windows 准备脚本收集对应固定 Git bundles。
已有 RPC/EFI 可显式传 `--reuse-rpc`、`--reuse-efi`，不重编模块/HAL/overlay。

用 `scripts/full_image_reuse.py --kernel KERNEL_EXPORT --camera CAMERA_COMPLETE --sensors SENSOR_BUNDLE --mesa MESA_RUNTIME --glue GLUE_OVERLAY --output reuse.json`
记录已完成产物的实际 SHA256；随后：

```sh
python3 scripts/build.py --reuse-components reuse.json --private-root PREPARED_INPUTS --output BUILD_OUTPUT
```

或统一阶段入口 `python3 scripts/hybrid_bootstrap.py --stage assemble --reuse-components reuse.json --private-root PREPARED_INPUTS --work BUILD_WORK`。
两者均构建完整 `.img` / `.img.gz`，不是组件编译。镜像接入 HWC/OMX、摄像头、传感器 RPC/EFI、自动亮度/旋转和触控，保留首次设置。
复用清单逐项检查内容，完整 camera bundle 和 sensor modules 必须匹配 6.18.60-sq2-android1；不能叠加新的内核 patch。
触控向导仍从当前源码生成，需设置 `SQ2_ANDROID_SDK`；NDK 编译器和 Linux 镜像工具同下文要求。

当前复用路径仍需要已准备的 Android/QTI runtime、EFI/固件及原模板；**不是仅靠公开下载和 Windows 导出即可从零生成所有运行库的完成声明**。
`--stage all` 对这一缺口继续明确失败，不会将原始输入包冒称 build-ready。
