# 构建

当前唯一默认产品线为 **Hybrid**：SQ1/SQ2 共用 Linux `6.18.60-sq2-android1`、Mesa GLES/Turnip + QTI gralloc/mapper5/HWC + 原生 OMX。通用交付规则见 [DEVELOPMENT](DEVELOPMENT.zh-CN.md)。

## 1. 入口

内置音频默认在运行库替换后应用 `prepare-sq2-primary-audio.py`：48 kHz PRIMARY 使用 10 ms 框架写入，物理播放代理保留至少 5760 帧缓冲请求；输入为麦克风修正版 donor HAL，重复复用不重复打补丁，未知输入构建时报错。完整边界见 [内置扬声器](SPEAKER-LOUDNESS.zh-CN.md)。

```sh
python3 scripts/build.py --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT
```

- 无子命令等同 `full-image`；`--graphics` 只接受 `hybrid`。
- 默认启用精简 5 分区输出（`super` 仅打包有效 `_a` EROFS 切片、`metadata`、`misc`、`SQ2PROBE` 1 GiB、`userdata`，默认整盘 `8 GiB`，可用 `--size-gib` / `--esp-mib` 调整）；`--preserve-template-layout` 可回退至原 39 分区 `20 GiB` 模板布局。
- `compile` 只构建内核组件；`verify`/`package` 只核验/复制历史固定包；`--esp-only` 只输出启动分区。
- 输出目录必须是位于 Linux 文件系统的全新目录，勿在 NTFS/9p 上直接跑源码构建。
- 依赖准备在 SQ1/SQ2 原 Windows 安装上执行，见 [DEPENDENCIES](DEPENDENCIES.zh-CN.md)。
- 启动来源 EFI 从 GNU-EFI 源码构建：准备 [对应修订](https://github.com/ncroxon/gnu-efi/tree/aeee48d838d40e8ef787748d5dc066ea2fb99dbe) 到 `PRIVATE_ROOT/kernel-6.18/gnu-efi`，或传 `--boot-gnu-efi DIR`。主机需 `patchelf`、`make`、支持 `efi-app-aarch64` 的 AArch64 GCC/binutils。
- 复用或指定预构建内核时，须已包含 `patches/spx-portable-boot/` 修复并提供配套模块；启动 ESP 须可写以记录启动来源。

## 2. 私有输入

默认根 `private/build-inputs/`（可用 `--private-root` 重定位）：

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
typec/services.jar                 # 共用 Android JAR，修正未知合盖状态
```

组件输入来源：

| 组件 | 准备方式 | 说明 |
| --- | --- | --- |
| 摄像头 | `scripts/build-spx-camera-bundle.py --inputs private/camera-build.json` | 源码生成；需匹配 AIDL/平台头/链接库，见 [CAMERA-BUILD](CAMERA-BUILD.zh-CN.md) |
| 传感器 | `scripts/build-spx-sensor-bundle.py` | ALS/加速度/SLPI/RPC/overlay，见 [SENSORS](SENSORS.zh-CN.md) |
| Type-C / 多屏 / ADB | 默认 `full_image_typec.py`，源码重编匹配模块 + 原生 Settings 入口 | 共用 services.jar 与 Java/smali/SDK 工具，见 [TYPEC](TYPEC.zh-CN.md) |
| 休眠 | `scripts/full_image_suspend.py --desktop-services ... --output private/build-inputs/suspend` | 同一 Android system 的原 JAR，见 [SUSPEND](SUSPEND.zh-CN.md) |

缺输入时明确报错，不通过改版本号或复用旧模块绕过 ABI/配对检查。

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

摄像头和传感器包须在完整内核构建前准备好。

## 4. 复用已完成组件

```sh
python3 scripts/build.py --private-root PREPARED_INPUTS --reuse-components REUSE.json --output FRESH_LINUX_OUTPUT --jobs 8
```

复用清单由 `full_image_reuse.py` 生成并记录显式哈希；复用 Image/全部模块、camera、sensors、Mesa 与 glue，但仍完整装配并验证整盘。仅向导 Java/Manifest 变化时旧 APK 自动失效并重编。

仅刷新 Venus 时先重编再复用：

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
| `paired/EFI/SQ2Nat/` | 首轮配对启动包 |
| `paired/EFI/SQ2Nat/PRIVACY.json` | 身份清理与跳过首次设置的处理记录 |
| `disk/work/esp-tree/` | 存储适配、正常/救援入口和 SQ1/SQ2 路由后的 ESP 树 |
| `disk/surface-pro-x-6.18-hybrid-generic.img(.gz)` | 完整稀疏整盘与压缩包 |
| `disk/IMAGE-MANIFEST.json`、`disk/LAYOUT.json` | 镜像结果与分区布局 |
| `ota/surface-pro-x-6.18-hybrid-ota-*.tar.gz`、`ota/OTA-MANIFEST.json`、`ota/SHA256SUMS` | 升级包、文件清单与校验文件 |
| `FULL-IMAGE.json`、`BUILD-COMMANDS.json` | 全流程结果、命令与日志位置 |

## 6. 首次启动与隐私

`surface-first-boot` 在 Android 启动完成且当前为用户 0 后设置 `device_provisioned=1` / `user_setup_complete=1`，停用已安装的初始设置向导并启动 HOME；成功写 `/data/system/surface-first-boot-v1`，失败则下次重试。

镜像清除继承的账户、Wi-Fi 与 SSH 凭据，锁定 root、默认关闭 SSH，使用全新 userdata/metadata，不关闭 captive portal 探测或预置网络。跳过 Android 初始设置不跳过独立触摸校准向导。详细策略见 [IMAGE](IMAGE.zh-CN.md)。

### Hybrid 源码构建：QCOM 提交优化

默认源码构建将 MSM 共用提交补丁和 GPUVM reservation v2 补丁追加到 Adreno 补丁序列。QCOM 用户态由仓库源码和私有输入 GSL donor 生成，需 Android NDK AArch64 API 35 clang（可用 `SQ2_ANDROID_CC` 指定）、readelf 与支持 `--rename-dynamic-symbols` 的 patchelf。

Hybrid 继续使用 Mesa GLES/Turnip、QTI gralloc/mapper5/HWC 和原生 OMX。普通应用经 QCOM/GSL 打开 MSM 时默认启用 `VM_BIND`；系统、隔离和 SDK 沙箱 UID 不在默认范围。BO 共享策略保持 `exclude`，handle/访问标志/队列/共享状态匹配时复用同步准备模板，每次提交仍重新收集 fence。

### OMX 低延迟远控

桥接层将 `LOWLATENCY_MODE` 映射为 Venus 零显示延迟控制。解码尚未开始时，OMX 预热停止只归还软件持有的 VB2 缓冲，不向未启动固件发送 FLUSH；活动会话仍等待真实 HFI 确认。更新需重编并打包桥接库及配对的 `venus-dec.ko`。

## 7. 边界

### 高级设置：Vulkan 配置、性能模式与触摸

Hybrid 整包默认从 `probe/advanced-control/` 编译安装 `SurfaceAdvancedControl.apk`，并在 initramfs 集成 `/bin/spx-advanced-control-boot`，界面随系统语言在中/英间切换。

- **一级选择页**：包含 **Vulkan 配置**、**性能模式** 与 **触摸** 入口。
- **Vulkan 配置**：支持修改全局默认驱动（自动策略 / MESA / Qcom，写入 `persist.sq2.vulkan.driver`；系统 UID `< 10000` 与 `com.android.systemui` 强制保留在 Mesa Turnip）及分应用覆盖（`persist.sq2.vulkan.app.<user>.<package>`，已修改应用置顶）；环境/启动强制项、救援模式与 Zygote 保护仍优先，重新打开应用后生效。
- **性能模式**：动态读取各 cpufreq 频率域共同支持的调度器，通过内置 root 桥接即时应用并持久化至 `/data/vendor/spx-advanced-control/cpu_governor`，重启自动恢复（救援模式或 `sq2.cpufreq=0` 跳过），硬件温控保护保持启用。
- **触摸**：提供**幽灵触控补丁**总开关（关闭时保留 `.noise` 样本并写 `.noise.disabled`、清空 `/run/iptsd-noise/active` 并重载 `iptsd`）、当前校准摘要展示、**重新校准**（`org.surface.touchcalibration.unified`）、**导出校准数据**（`.spkcal`）、**从文件应用校准数据**及**清除校准数据**。

单独构建 APK：`python3 scripts/full_image_advanced_control.py --output OUTPUT/SurfaceAdvancedControl.apk`。签名密钥位于 `private/advanced-control/signing.keystore`（或通过 `SPX_ADVANCED_CONTROL_KEYSTORE` 指定）。复用构建会重编 Vulkan 路由器、高级设置 APK 并刷新桥接脚本。

### 救援模式（`spx-rescue` / `sq2.rescue=1`）

默认内核启用 `r8152`（内置）及 `usbnet` / `ax88179_178a` / `cdc_ether` / `cdc_ncm` / `rndis_host`（模块），initramfs 集成救援模式组件（`scripts/full_image_rescue.py`、`probe/rescue/`）：

- **Android 启动前驻留**：发现启动盘与 `/media/esp` 后，在挂载 Android 分区与启动 `/system/bin/init` 前停留在救援控制台。
- **SSH（仅救援模式）**：启动 Dropbear（端口 22），允许公钥（`/root/.ssh/authorized_keys`，自动导入 `/media/esp/rescue/authorized_keys` 或 `/media/esp/authorized_keys`）与仅救援模式内存生效的默认密码 `000000`。
- **USB 网卡与 Wi-Fi TUI**：自动加载 USB 网卡并运行 DHCP，拉起 `wifi-services`、`ath10k_snoc` 与 `wpa_supplicant`；提供静态 ARM64 工具 `/bin/spx-wifi-tui` 连接 Wi-Fi 并实时显示网络与 SSH 信息，另提供 `spx-rescue-usb-gadget start|stop` 下口 USB NCM 网卡模式。
- **继续启动 Android**：运行 `start-android`（别名 `continue-android` 或 `wifi-tui` 内按 `b`；支持 `--safe` / `--stop-ssh` / `--umount`）触发 `spx-rescue-handoff` 清理救援网络与 gadget、恢复正常参数并执行 `exec native-handoff /newroot`。

### 系统更新（OTA）与升级包构建

- **全量构建默认产出升级包**：`python3 scripts/build.py --output FRESH_OUTPUT` 在完成 `disk/` 后调用 `scripts/build_ota_package.py` 生成 `FRESH_OUTPUT/ota/`（支持 `--ota-mode auto|delta|esp|full`、`--ota-baseline PREV_BUILD_OR_MANIFEST`、`--ota-version VER`、`--skip-ota`）。
- **单独生成升级包**：`python3 scripts/build.py ota --build-output BUILD_OUT [--previous-manifest PREV/ota/OTA-MANIFEST.json] --output OTA_OUT`。
- **自动瘦身**：`auto` 模式下若 `super` 无变化，仅打包内容去重后的更新文件与清单（`ota-manifest.json`、`ota-manifest.kv`、`ota-files.tsv`），不含 `super.img`；传入 `--previous-manifest` 时仅打包变更/新增文件并记录 `DEL` 删除项；`super` 变化或 `--mode full` 时包含 `payload/super.img`。
- **设备端升级服务**：`SurfaceAdvancedControl.apk` 内置 `SystemUpdateActivity`（`android.settings.SYSTEM_UPDATE_SETTINGS`），开机后 `/bin/spx-ota-boot` 动态停用冲突的旧更新页面。支持清单 `.tar.gz`（`super` 无变化且保留数据时在线写入 ESP；否则走 initramfs 离线阶段）及完整 `.img` / `.img.gz`（由 `/bin/spx-ota-stage` 配合 `/bin/spx-ota-tool` 在挂载前执行全量更新），均支持保留数据或清空 `userdata`/`metadata`/`misc` 全新安装。
- **ADB 一键升级**：Windows 使用 `tools/Upgrade-GooglebookViaAdb.ps1`（或 `.cmd`），Linux / macOS 使用 `sh tools/upgrade-googlebook-via-adb.sh -p <升级包> [--wipe-data]`，自动校验兼容性与空间、备份 `EFI/SQ2Nat` 并触发两段重启完成离线更新。
- **写入内置硬盘（从 USB 全新安装）**：`InternalDiskInstallActivity` 经 `/bin/spx-ota-boot` 调用 `/bin/spx-internal-install` 与 `/bin/spx-ota-tool`。仅在 USB 启动、源盘为标准 5 分区布局且存在足够容量的非 USB 内置盘时开放；重建全新 5 分区 GPT、流式复制并 SHA-256 校验 `super`、写入单系统 `EFI/BOOT/grub.cfg` 并格式化 `metadata`/`userdata`/`SQ2PROBE`，完成后提示立即关机并拔除 USB 冷启动。

### 整盘验收边界

- 默认构建只生成产物，不运行测试套件、整盘解压校验或 SHA 清单；编译错误及格式/ABI/配对约束仍会中止构建。
- 私有库与固件按各自许可分发，来源见 [PROVENANCE](PROVENANCE.zh-CN.md)；详细流程见 [STANDARD-BUILD](STANDARD-BUILD.zh-CN.md)。
