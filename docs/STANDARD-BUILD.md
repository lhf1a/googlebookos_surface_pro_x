# Hybrid 标准构建流程与产物

更新：2026-11-16。基于主线 `619af8b`（含 `940a769` 休眠修复），统一为 Hybrid 默认构建、Windows 依赖准备和无个人凭据整盘装配。

## 1. 唯一产品线

- 标准入口：`python3 scripts/build.py`，无子命令默认 `full-image`。
- 图形仅允许 `hybrid`：Mesa GLES/Turnip + QTI gralloc/mapper5/HWC；保留现有双 Vulkan 选择策略。
- SQ1/SQ2共用 Linux 6.18.60 Image、配套模块和initramfs；SMBIOS路由选择机型DT频率配置。
- 摄像头、自动亮度、自动旋转和当前触控向导为整包必需能力。
- `compile`是内核组件构建；`verify/package`是历史固定包核验/复制，不是另一条整机产品线。
- `--esp-only`仅输出启动分区内容，不能当完整磁盘镜像。

## 2. 构建顺序

依赖来源、原 Windows 只读提取、统一下载打包与 Linux 导入见 [DEPENDENCIES](DEPENDENCIES.md)。
`Prepare-HybridDependencies.ps1` 生成原始依赖包；尚未闭合的 prepared source/Android-QTI 转换步骤明确记为 unresolved，不能把采集完成当作完整镜像构建完成。

```text
共用平台依赖 + 匹配6.18内核构建树
 ├─ 摄像头源码 → 驱动/AIDL/Camera HAL/ISP/mapper/allocator → camera/CAMERA.json
 └─ 传感器源码 + 共用OEM资料 → 模块/SubHal/overlay/RPC/SLPI EFI → sensors/sensors-bundle.json
                                   ↓
full-image：准备内核源码 → 显示/A6xx补丁 → Image/modules/DTB → 外部模块/depmod
                                   ↓
匹配内核重编模块（或显式复用已完成组件）→ Hybrid图形集成 → 构建/校验复用触控向导 → 安装camera/sensors
                                   ↓
Venus休眠握手模块 + desktop-services.jar补丁 + 一次性休眠设置迁移
                                   ↓
清理个人凭据/自动跳过首次设置 → 重建initramfs → 配对EFI/SLPI固件/机型DT
                                   ↓
模板系统分区 + 新userdata/metadata → GPT/ESP/文件系统读回 → IMG/IMG.GZ/清单
```

摄像头和传感器包目前在完整内核构建**之前**加载：需要先准备它们，不能假定full-image会自动从零引导这些依赖。
摄像头新源码入口不依赖Surface在线、旧camera运行包或Windows摄像头驱动。
传感器的SLPI固件、型号配置、默认光学校准由SQ1/SQ2共用，准备一次即可；不要求逐机采集。

## 3. 输入及当前状态

| 输入 | 作用 | 本机状态 |
| --- | --- | --- |
| `private/build-inputs/template.img` | 原20 GiB布局和Android系统分区 | 已有 |
| `kernel-6.18/source/SOURCE.json`、`source.tar.gz` | 内核/外部模块源码 | 已有 |
| `kernel-6.18/toolchain.tar.gz`、`board.dts` | ARM64工具链、DT源码 | 已有 |
| `full-image-runtime/records.json`、`files/` | Mesa/QTI/音视频等共用输入 | 已有；实际镜像通过复用清单换入新编译干净 Mesa/Turnip |
| `camera/CAMERA.json`及13个ELF | 前后摄模块、provider、mapper/allocator | 已从源码生成并装入完整镜像 |
| `sensors/sensors-bundle.json`及对应文件 | ALS/加速度/SLPI/RPC/配置 | 模块/HAL/overlay/RPC/EFI 与型号资料已装配 |
| `suspend/desktop-services.jar` | 构建时修改 AC 息屏等待至 1000ms | 默认必需；取同一 Android system 的原 JAR，不从 Git 获取 |
| Android平台接口、生成头、链接库、NDK/SDK | camera/sensor/触控用户态构建 | 按本机路径配置并逐项检查，不能用示例路径代替 |

摄像头配置模板：`configs/camera-build.example.json`；完整字段与ABI要求见 [CAMERA-BUILD](CAMERA-BUILD.md)。
传感器命令与共用输入见 [SENSORS](SENSORS.md)。两个Git子模块按主线固定提交拉取；它们不代替上述平台依赖。

## 4. 执行入口

先创建填写真实路径的 `private/camera-build.json`，准备与6.18匹配的kernel_source/kernel_build及平台依赖：

```sh
python3 scripts/build-spx-camera-bundle.py --inputs private/camera-build.json --output out/camera-from-source
python3 scripts/build.py --camera-inputs out/camera-from-source/camera --sensor-inputs private/build-inputs/sensors --output FRESH_LINUX_OUTPUT --jobs 8
```

第二条默认source内核与hybrid；output必须是全新目录，源码构建输出使用Linux文件系统。
传感器包先用 `build-spx-sensor-bundle.py` 生成；参数见SENSORS。缺输入时修复输入，不跳过组件。

已完成组件使用 `full_image_reuse.py` 生成显式哈希清单，再执行 `python3 scripts/build.py --reuse-components REUSE.json --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT`。
此路径复用 Image/全部模块、camera、sensors、Mesa 和 glue，不再重编它们；详见 [DEPENDENCIES](DEPENDENCIES.md)。

若原复用包尚未包含最新 Venus 休眠修复，先执行 `rebuild-hybrid-venus.py`，不能直接沿用旧模块：

```sh
python3 scripts/rebuild-hybrid-venus.py --reuse-components PREVIOUS_REUSE.json --kernel-build MATCHED_KOUT --source PREVIOUS_VENUS_SOURCE --output FRESH_VENUS_OUTPUT
python3 scripts/build.py --reuse-components FRESH_VENUS_OUTPUT/REUSE.json --private-root PREPARED_INPUTS --output FRESH_IMAGE_OUTPUT
```

脚本核对 Image、配置及符号表后只重编 Venus；保留完整模块 stage。无受影响源码变化时，可用 `full_image_reuse.py --assembled-runtime PREVIOUS_INITRAMFS` 显式复用已编译小型 helper，不复制旧 init 或个人状态。

## 5. 产物层级

| 路径（相对OUTPUT） | 含义 |
| --- | --- |
| `kernel-build/`、`kernel-export/` | 内核构建树、Image、模块stage、DTB与命令记录 |
| `camera-sensors/`、`camera-camss/`、传感器模块目录 | 本轮匹配内核的组件输出 |
| `paired/EFI/SQ2Nat/` | 首轮配对启动包；不是最终整盘交付目录 |
| `paired/EFI/SQ2Nat/PRIVACY.json` | 去除身份与保留首次设置的处理记录 |
| `disk/work/esp-tree/` | 存储适配、正常/救援入口和SQ1/SQ2路由后的ESP树；具体工作目录以构建清单为准 |
| `disk/surface-pro-x-6.18-hybrid-generic.img` | 完整稀疏磁盘镜像 |
| `disk/surface-pro-x-6.18-hybrid-generic.img.gz` | 压缩整盘镜像 |
| `disk/IMAGE-MANIFEST.json`、`disk/LAYOUT.json` | 镜像结果与分区布局 |
| `FULL-IMAGE.json`、`BUILD-COMMANDS.json` | 全流程结果、命令及日志位置 |

个人SSH/Wi-Fi/密码与旧setup跳过脚本在最终组件安装后清除；不复制旧userdata/metadata、persist、FRP及原始转储。
首次配置交给Android，用户设置保持；闭源平台库/固件仍须按其许可分发。

## 6. 当前真实产物

`out/hybrid-runtime-20261116/common/`只有前轮的Image（35,588,608B）和initramfs（255,409,661B）。
它们基于较早运行包，不含本次最新摄像头/传感器完整集成，**不是当前通用镜像交付件**。
`/mnt/spx-workspace/chromebookOS/out/hybrid-generic-20261116`是上次失败尝试留下的空目录。
上述均为历史产物。最新含主线休眠修复的完整镜像在 `/mnt/spx-workspace/chromebookOS/out/hybrid-sleep-20261116/image/disk/`，原始 20 GiB，压缩 8,199,955,842 字节；全流/分区/ESP 回读校验通过，未真机启动。完整哈希、增量构建与边界见 [休眠整包记录](HYBRID-SLEEP-20261116.md)。前一份不含新休眠补丁的 `hybrid-assembly-20261116` 镜像保留，不覆盖。
