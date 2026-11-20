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
| `ota/surface-pro-x-6.18-hybrid-ota-*.tar.gz`、`ota/OTA-MANIFEST.json`、`ota/SHA256SUMS` | 升级包压缩包、文件清单描述与校验文件 |
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
路由器仍决定应用使用 QCOM 还是 Turnip。普通应用经 QCOM/GSL 打开 MSM 时默认启用 VM_BIND，
不读取包名白名单；系统、隔离和 SDK 沙箱 UID 不在默认范围。BO 共享策略保持 `exclude`。桥接复用已验证的 BO 查找结果，移交同步访问表；
handle、访问标志、队列和共享状态匹配时复用同步准备模板。每次提交仍重新收集 fence，
导入、导出和 handle 复用继续走登记和失效路径。未采用全量 NO_SHARE 或多核拆表。
SQ2 游戏测试不代替 SQ1 实机验收。

### OMX 低延迟远控

桥接层把下游 `LOWLATENCY_MODE` 映射为 Venus 的零显示延迟控制；非法值、
已活动的会话和驱动失败仍明确报错。解码尚未开始时，OMX 预热停止只归还实际由软件
持有的 VB2 缓冲，不向未启动的固件会话发送 FLUSH；活动会话仍等待真实 HFI 确认。
更新需重编并打包桥接库及与所选 Image 配对的 `venus-dec.ko`，复用旧载荷不会获得修复。
SQ2 热替换后 UU 连接已获用户确认正常；完整重启、SQ1 及跨设备回归尚未验收。

## 7. 边界

### 高级设置：Vulkan 配置、性能模式与触摸

Hybrid 整包默认从 `probe/advanced-control/` 编译并安装“高级设置 / Advanced Settings”（`SurfaceAdvancedControl.apk`），
并在 initramfs 中集成根桥接脚本 `/bin/spx-advanced-control-boot`，启动器与系统设置均提供入口，界面与入口文案随系统语言在中/英间切换（非中文默认英语）。

- **一级选择页**：包含 **Vulkan 配置**、**性能模式** 与 **触摸** 三个子页面入口。
- **Vulkan 配置**：
  - 支持修改默认 Vulkan 配置（自动策略 / MESA / Qcom），写入 `persist.sq2.vulkan.driver`；当默认切换为 Qcom 时，系统 UID（`< 10000`）与 `com.android.systemui` 仍受路由器保护并留在 Mesa Turnip。
  - 支持搜索当前 Android 用户应用并分别保存 `跟随默认 / MESA / Qcom`（写入 `persist.sq2.vulkan.app.<user>.<package>`）；所有已手动修改过的应用始终置顶显示。
  - 环境/启动强制项、救援模式（`sq2.rescue=1`）与 Zygote 保护、debug 列表仍优先；重新打开应用后生效，不热换活跃驱动。
- **性能模式**：
  - 动态读取各 cpufreq 频率域共同支持的内核调度策略（如 `performance`、`schedutil`、`ondemand`、`conservative`、`powersave`、`userspace` 等），不硬编码节点或策略列表。
  - 切换时通过内置 root/init 桥接立即应用到所有 cpufreq 策略并持久化至 `/data/vendor/spx-advanced-control/cpu_governor`，重启自动恢复（救援模式或 `sq2.cpufreq=0` 自动跳过），无需手工给应用授予 KernelSU 权限；硬件温控保护保持启用。
- **触摸**：
  - 提供**幽灵触控补丁**总开关：关闭时保留 `/data/vendor/sq2-touch-calibration/<device>.noise` 样本并写入 `.noise.disabled` 标记，同时清空 `/run/iptsd-noise/active` 并重载 `iptsd`（`touch-calibration-boot` 重启时同样遵守禁用标记）；重新开启无需重新采样。
  - 展示当前触摸校准数据摘要（触摸硬件 HID/设备指纹及 `iptsd` 状态、X/Y/顶部边缘曲线拟合点数、大触点/拇指阈值 `ContactSizeMax`、幽灵触控 `.noise` 模型及点按防抖 `.tap` 策略）。
  - 支持**重新校准**（打开 `org.surface.touchcalibration.unified` 并在返回后刷新摘要）、**导出校准数据**（将当前触摸坐标、幽灵触控补丁与点击策略统一导出为单个 `.spkcal` 校准文件）、**从文件应用校准数据**（选择并校验导出的单个 `.spkcal` 文件或单文件配置并即时恢复应用），以及带确认弹窗的**清除校准数据**。

单独构建 APK：`python3 scripts/full_image_advanced_control.py --output OUTPUT/SurfaceAdvancedControl.apk`。
签名密钥位于 ignored 的 `private/advanced-control/signing.keystore`（也可通过 `SPX_ADVANCED_CONTROL_KEYSTORE` 指定）。
复用构建会重编 Vulkan 路由器、高级设置 APK 并刷新桥接脚本。
当前为源码集成候选；尚未完成新整盘启动及 SQ1/SQ2 真机端到端验收。

### 救援模式（`spx-rescue` / `sq2.rescue=1`）

Hybrid 整包默认使用 `configs/kernel-6.18-hybrid.config`（内置 `r8152` 并以模块形式启用 `usbnet` / `ax88179_178a` / `cdc_ether` / `cdc_ncm` / `rndis_host` 等常见 USB 网卡驱动），并在 initramfs 中集成救援模式组件（`scripts/full_image_rescue.py`、`probe/rescue/`）：

- **在 Android 启动前停止**：发现启动盘与 `/media/esp` 后，在挂载 Android 分区与启动 `/system/bin/init` 之前停留在救援控制台，不自动挂载 `system`/`vendor`/`metadata`/`userdata`。
- **SSH（仅救援模式）**：自动启动 Dropbear（端口 22），同时允许公钥登录（`/root/.ssh/authorized_keys`，并自动导入 `/media/esp/rescue/authorized_keys` 或 `/media/esp/authorized_keys`）与默认密码 `000000`（仅在救援启动时在内存中为锁定的 `root` 设置，正常启动镜像仍保持 `root` 锁定且关闭 SSH）。
- **USB 网卡与 Wi-Fi TUI**：自动加载已接入的 USB 网卡驱动并运行 DHCP，同时拉起 `wifi-services`、`ath10k_snoc` 与 `wpa_supplicant`；提供静态编译的 ARM64 TUI 工具 `wifi-tui`（`/bin/spx-wifi-tui`）用于扫描、选择和登录 Open/WPA/WPA2/WPA3/隐藏 Wi-Fi，并实时显示 Wi-Fi/USB 网卡 IP 与 SSH 信息；另提供可选的 `spx-rescue-usb-gadget start|stop` 下口 USB NCM 网卡模式。
- **继续启动 Android 与控制权交接**：运行 `start-android`（别名 `continue-android`，或 `wifi-tui` 内按 `b`；支持 `--safe` / `--stop-ssh` / `--umount`）可通知 PID 1 退出救援循环；交接脚本 `spx-rescue-handoff` 会先停止救援 USB 监听、`wpa_supplicant`、所有救援 `udhcpc`、清空并关闭救援网卡接口、卸载可选下口 USB gadget 并恢复 host 角色，再恢复正常启动参数与跳过的早期驱动钩子并继续 `exec native-handoff /newroot`。
- 当前已在主机完成编译、ARM64 `qemu-aarch64` 运行测试与整条 initramfs 装配流水线单元测试；尚未在 SQ1/SQ2 实机完成端到端救援启动与继续进入 Android 验收。

### 系统更新（OTA）与升级包构建

- **全量构建默认同步产出升级包**：`python3 scripts/build.py --output FRESH_OUTPUT` 在完成 `disk/` 后自动调用 `scripts/build_ota_package.py` 在 `FRESH_OUTPUT/ota/` 生成升级包（支持 `--ota-mode auto|delta|esp|full`、`--ota-baseline PREV_BUILD_OR_MANIFEST`、`--ota-version VER`、`--skip-ota`）。
- **单独生成升级包**：`python3 scripts/build.py ota --build-output BUILD_OUT [--previous-manifest PREV/ota/OTA-MANIFEST.json] --output OTA_OUT`（或直接运行 `scripts/build_ota_package.py`）。
- **按 `super` 是否变化自动瘦身**：在 `auto` 模式下，若本次编译 `super` 无变化，压缩包中仅写入内容去重后的更新文件（`EFI/SQ2Nat` 与 `EFI/SQ2Res` 共用相同 `Image`/`initramfs` 载荷）及描述清单（`ota-manifest.json`、`ota-manifest.kv`、`ota-files.tsv`），不包含 `super.img`；若传入 `--previous-manifest`，仅打包变更/新增文件并记录 `DEL` 删除项；若 `super` 发生变化或显式指定 `--mode full`，则包含 `payload/super.img`。
- **设备端升级服务与设置页面替换**：
  - `SurfaceAdvancedControl.apk` 内置 Material You 风格的 `SystemUpdateActivity` 并声明 `android.settings.SYSTEM_UPDATE_SETTINGS`；开机后根守护进程 `/bin/spx-ota-boot` 动态停用其他包竞争该 Intent 的旧更新页面，使 **系统设置 → 系统 → 系统更新**（及高级设置首页入口）直接打开本系统更新页面。
  - **支持两类包**：读取含描述清单的 `.tar.gz` 按清单更新（`super` 无变化且保留数据时可直接在线安全写入 ESP 并提示重启；`super` 有变化或需清数据时自动切换至 initramfs 离线阶段）；直接提供完整镜像 `.img` / `.img.gz` 时解析 GPT 并由 initramfs 早期钩子 `/bin/spx-ota-stage`（配合静态 ARM64 工具 `/bin/spx-ota-tool`）在挂载 `super`/`userdata` 前离线执行全量更新。
  - **所有更新模式均支持手动切换数据策略**：默认保留用户数据（含应用、账户、Wi-Fi、触摸校准、高级设置与双系统 `grub.cfg`）；亦可手动选择 **不保留数据（全新安装）**，经二次确认后在离线阶段清空 `userdata`、`metadata` 与 `misc`（保留 ext4 特性与 `lost+found`）。
- **从 `v0.7.0` 等老镜像通过 root ADB 一键升级**：
  - Windows：`scripts/Upgrade-GooglebookViaAdb.ps1`（双击 `scripts/Upgrade-GooglebookViaAdb.cmd` 或传入 `-PackagePath`、可选 `-WipeData`）。
  - Linux / macOS：`sh scripts/upgrade-googlebook-via-adb.sh -p <升级包> [--wipe-data]`。
  - 自动校验包与设备兼容性（拦截在旧布局 `v0.7.0` 上误用不含 `super` 的增量包）、推送校验、备份原 `EFI/SQ2Nat` 并热替换新引导载荷后自动触发两段重启完成离线更新；默认保留用户数据与触摸校准。
- 当前已在主机完成 `spx-ota-tool` 交叉编译、打包、在线/离线应用、全新安装清数据、Linux `sh` 与 Windows PowerShell ADB 一键脚本及 APK Intent 清单自动化单元测试；尚未在 SQ1/SQ2 实机完成端到端重启升级验收。

- **写入内置硬盘（从 USB 全新安装）**：
  - `SurfaceAdvancedControl.apk` 首页提供 `InternalDiskInstallActivity`，经 `/bin/spx-ota-boot` 调用 `/bin/spx-internal-install` 与静态 ARM64 工具 `/bin/spx-ota-tool`（`write-gpt-5part`、`copy-verify`）。
  - 仅在运行时确认为 USB 启动、源盘为标准 5 分区布局且存在满足容量的内置非 USB 硬盘时开放；不硬编码设备号或单机白名单，自动排除当前启动盘、其他 USB 盘、可移动介质与虚拟块设备。
  - 在目标内置硬盘重建全新 5 分区 GPT（`super`、`metadata`、`misc`、`SQ2PROBE` 与铺满全盘的全新 `userdata`，使用全新随机 UUID），流式复制当前 `super` 并清缓存读回做 SHA-256 校验，格式化 FAT32 `SQ2PROBE` 并写入标准 `\EFI\BOOT\BOOTAA64.EFI` 与单系统 `EFI/BOOT/grub.cfg`（排除 USB 运行时状态文件），清零 `misc` 并格式化全新 `metadata`/`userdata`（不保留原内置硬盘数据，也不迁移 USB `userdata`）。
  - 完成后提示用户选择 **立即关机（推荐）** 或 **稍后手动关机**，待关机拔除 USB 后从内置硬盘冷启动。
  - 当前已在主机完成 `spx-ota-tool` ARM64 交叉编译与 `qemu-aarch64` 验证、磁盘筛选/安全拦截、GPT/ext4/ESP 端到端单元测试与 APK 构建；尚未在 SQ1/SQ2 实机完成“USB 启动写入内置 NVMe → 关机拔盘 → 内置硬盘冷启动”验收。

### 整盘验收边界

- 默认只构建产物：不运行测试套件、整盘解压校验或 SHA 清单；编译错误、必要格式/ABI/配对约束仍会中止。
- 构建成功不等于真机启动、SQ1 跨机、长期休眠或功耗验收；未启动的新整盘必须在文档中如实标注。
- 私有库/固件仍按其许可分发，来源见 [PROVENANCE](PROVENANCE.md)。
- 完整流程、输入清单和逐项状态见 [STANDARD-BUILD](STANDARD-BUILD.md)。
