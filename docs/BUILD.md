# 构建

当前唯一默认产品线为 **Hybrid**：SQ1/SQ2 共用 Linux `6.18.60-sq2-android1`，
Mesa GLES/Turnip + QTI gralloc/mapper5/HWC + 原生 OMX。历史 6.3.8 模板、baseline/mesa
分支和分组装入口已退休，不属于当前命令行。通用交付规则见 [DEVELOPMENT](DEVELOPMENT.md)。

## 1. 入口

内置音频默认在运行库替换（含组件复用）之后应用
`prepare-sq2-primary-audio.py`：48 kHz PRIMARY 使用 10 ms 框架写入，
物理播放代理保留至少 5760 帧缓冲请求。输入为准备好的麦克风修正版
donor HAL，重复复用已修复版本不重复打补丁；未知输入在构建时明确报错。
这是构建适配，不增加启动时身份或内容哈希准入。
SQ2 的 NTE、鸣潮和视频已经完整重启与用户实听确认；
不代表 SQ1、所有 USB 设备或长期休眠功耗均已复测。
完整边界见 [内置扬声器](SPEAKER-LOUDNESS.md)。

```sh
python3 scripts/build.py --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT
```

- 无子命令等同 `full-image`；`--graphics` 只接受 `hybrid`。
- 默认启用精简 5 分区输出（`super` 仅打包有效 `_a` EROFS 切片、`metadata`、`misc`、`SQ2PROBE` 1 GiB、`userdata`，默认整盘 `8 GiB`，可用 `--size-gib` / `--esp-mib` 调整）；传入 `--preserve-template-layout` 可回退至原 39 分区 `20 GiB` 模板布局。
- `compile` 只构建内核组件；`verify`/`package` 只核验/复制历史固定 v21 包；`--esp-only` 只输出启动分区。三者都不是整机产品线。
- 输出目录必须是全新目录，并位于 Linux 文件系统；不要在 NTFS/9p 上直接跑源码构建。
- 依赖准备在 SQ1/SQ2 原 Windows 安装上执行，见 [DEPENDENCIES](DEPENDENCIES.md)。
- 启动来源 EFI 从 GNU-EFI 源码构建。准备 [对应修订](https://github.com/ncroxon/gnu-efi/tree/aeee48d838d40e8ef787748d5dc066ea2fb99dbe) 到 `PRIVATE_ROOT/kernel-6.18/gnu-efi`，或传 `--boot-gnu-efi DIR`。Linux 主机需 `patchelf`、`make`、AArch64 GCC/binutils（支持 `efi-app-aarch64`）。
- 如复用或指定预构建的内核，必须已包含 `patches/spx-portable-boot/` 的修复，并提供配套模块；旧内核不能仅靠同名版本号通过。启动 ESP 必须可写以记录本次启动来源。

## 2. 私有输入

默认根 `private/build-inputs/`（不进 Git，可用 `--private-root` 重定位）：

```text
template.img                       # 原 20 GiB 布局模板，不修改
kernel-6.18/
  source/SOURCE.json, source.tar.gz
  toolchain.tar.gz, board.dts, firmware/, EFI/
full-image-runtime/
  INPUTS.json, records.json, files/
camera/CAMERA.json + ELF
sensors/sensors-bundle.json + 文件
suspend/desktop-services.jar       # 构建时 patch AC 息屏等待
typec/services.jar                # 共用 Android JAR，修正未知合盖状态
```

组件输入来源：

| 组件 | 准备方式 | 说明 |
| --- | --- | --- |
| 摄像头 | `scripts/build-spx-camera-bundle.py --inputs private/camera-build.json` | 源码生成；需要匹配的 AIDL/平台头/链接库，见 [CAMERA-BUILD](CAMERA-BUILD.md) |
| 传感器 | `scripts/build-spx-sensor-bundle.py` | ALS/加速度/SLPI/RPC/overlay，见 [SENSORS](SENSORS.md) |
| Type-C / 多屏 / ADB | 默认 `full_image_typec.py`，源码重编匹配模块 + 原生 Settings 入口 | 共用 services.jar 与 Java/smali/SDK 工具，见 [TYPEC](TYPEC.md) |
| 休眠 | `scripts/full_image_suspend.py --desktop-services ... --output private/build-inputs/suspend` | 同一 Android system 的原 JAR，见 [SUSPEND](SUSPEND.md) |

缺输入时明确报错，不静默丢组件；不通过改版本号或复用旧模块绕过 ABI/配对检查。

## 3. 构建顺序

```text
共用平台依赖 + 匹配 6.18 内核构建树
 ├─ 摄像头源码 → 驱动/AIDL/HAL/ISP/mapper/allocator → camera/CAMERA.json
 └─ 传感器源码 + 共用 OEM 资料 → 模块/SubHal/overlay/RPC/SLPI EFI → sensors-bundle.json
                                   ↓
full-image：准备内核源码 → 显示/A6xx 补丁 → Image/modules/DTB → 外挂模块/depmod
                                   ↓
重编匹配模块（或显式复用组件）→ Hybrid 图形集成 → 触控向导 → 安装 camera/sensors
                                   ↓
Venus 休眠握手 + desktop-services.jar 补丁 + 一次性休眠设置迁移
                                   ↓
清理个人凭据/跳过首次设置 → 重建 initramfs → 配对 EFI/SLPI 固件/机型 DT
                                   ↓
模板系统分区 + 新 userdata/metadata → GPT/ESP/文件系统读回 → IMG/IMG.GZ/清单
```

摄像头和传感器包必须在完整内核构建之前准备好；`full-image` 不会从零引导这些依赖。

## 4. 复用已完成组件

```sh
python3 scripts/build.py --private-root PREPARED_INPUTS --reuse-components REUSE.json --output FRESH_LINUX_OUTPUT --jobs 8
```

复用清单由 `full_image_reuse.py` 生成并记录显式哈希；复用 Image/全部模块、camera、sensors、
Mesa 与 glue，但仍完整装配并验证整盘。仅向导 Java/Manifest 变化时旧 APK 自动失效并重编。

仅需刷新 Venus 时先重编再复用，不能沿用旧模块：

```sh
python3 scripts/rebuild-hybrid-venus.py --reuse-components PREV_REUSE.json \
  --kernel-build MATCHED_KOUT --source PREV_VENUS_SOURCE --output FRESH_VENUS_OUTPUT
python3 scripts/build.py --reuse-components FRESH_VENUS_OUTPUT/REUSE.json \
  --private-root PREPARED_INPUTS --output FRESH_IMAGE_OUTPUT
```

## 5. 产物

| 路径（相对 OUTPUT） | 含义 |
| --- | --- |
| `kernel-build/`、`kernel-export/` | 内核构建树、Image、模块 stage、DTB 与命令记录 |
| `camera-sensors/`、`camera-camss/` | 本轮匹配内核的组件输出 |
| `paired/EFI/SQ2Nat/` | 首轮配对启动包，不是最终整盘交付目录 |
| `paired/EFI/SQ2Nat/PRIVACY.json` | 身份清理与跳过首次设置的处理记录 |
| `disk/work/esp-tree/` | 存储适配、正常/救援入口和 SQ1/SQ2 路由后的 ESP 树 |
| `disk/surface-pro-x-6.18-hybrid-generic.img(.gz)` | 完整稀疏整盘与压缩包 |
| `disk/IMAGE-MANIFEST.json`、`disk/LAYOUT.json` | 镜像结果与分区布局 |
| `FULL-IMAGE.json`、`BUILD-COMMANDS.json` | 全流程结果、命令与日志位置 |

## 6. 首次启动与隐私

`surface-first-boot` 在 Android 启动完成且当前为用户 0 后设置 `device_provisioned=1` /
`user_setup_complete=1`，只停用实际安装的初始设置向导，然后启动 HOME；成功写
`/data/system/surface-first-boot-v1`，失败不写标记、下次重试。

镜像清除继承的账户、Wi-Fi 和 SSH 凭据，锁定 root、默认关闭 SSH，使用全新 userdata/metadata；
不关闭 captive portal 探测、不预置网络。跳过 Android 初始设置不等于跳过独立的触摸校准向导。
详细策略与当前成品校验值见 [IMAGE](IMAGE.md)。

### Hybrid 源码构建：QCOM 提交优化

默认源码构建把 MSM 共用提交补丁和 GPUVM reservation v2 补丁追加到原有 Adreno 补丁序列。
QCOM 用户态由仓库源码和私有输入中的 GSL donor 生成：需要 Android NDK AArch64 API 35 clang
（可用 `SQ2_ANDROID_CC` 指定）、readelf 和支持 `--rename-dynamic-symbols` 的 patchelf。
不在仓库分发高通运行库。显式选择旧 prebuilt 或复用已有内核时，不会凭空补入新的内核代码。

Hybrid 继续使用 Mesa GLES/Turnip、QTI gralloc/mapper5/HWC 和原生 OMX。
默认对 NTE 选择 QCOM 并启用 VM_BIND 的 `exclude` 分配策略：这是已测试的应用选择，
不是按包名判断 BO 是否能共享。桥接复用已验证的 BO 查找结果，移交同步访问表；
handle、访问标志、队列和共享状态匹配时复用同步准备模板。每次提交仍重新收集 fence，
导入、导出和 handle 复用继续走登记和失效路径。未采用全量 NO_SHARE 或多核拆表。
SQ2 游戏测试不代替 SQ1 实机验收。

## 7. 边界

- 默认只构建产物：不运行测试套件、整盘解压校验或 SHA 清单；编译错误、必要格式/ABI/配对约束仍会中止。
- 构建成功不等于真机启动、SQ1 跨机、长期休眠或功耗验收；未启动的新整盘必须在文档中如实标注。
- 私有库/固件仍按其许可分发，来源见 [PROVENANCE](PROVENANCE.md)。
- 完整流程、输入清单和逐项状态见 [STANDARD-BUILD](STANDARD-BUILD.md)。
