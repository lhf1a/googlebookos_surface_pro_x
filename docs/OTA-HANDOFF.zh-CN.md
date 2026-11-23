# OTA 与 ADB 一键升级系统架构说明

## 1. 核心架构与文件清单

### 1.1 设备端 OTA 运行时（initramfs）
- `probe/ota/spx-ota-tool.c`：零依赖 C11 工具，静态编译为 ARM64 ELF，提供：
  - `sha256`（文件 / stdin）
  - `detect`（识别 `full-img` / `gzip` / `manifest`）
  - `inspect-gpt`（解析完整磁盘镜像 GPT，`super` 与 `SQ2PROBE` 偏移与大小）
  - `extract-stream`（单遍流式解压/提取并计算 `super` 与 `esp.fat` 的 SHA-256）
- `probe/ota/spx-ota-stage`：initramfs 早期离线升级器，在 `prepare-native-ssd` 发现磁盘与挂载 `/media/esp` 之后、`sq2-loop-slice` 挂载 `super` 之前执行，救援模式 `sq2.rescue=1` 下自动跳过：
  - 消费 `/media/esp/SPX-OTA-PENDING` 并原子重命名为 `SPX-OTA-INPROGRESS`，防止重启死循环
  - 支持增量/全量 `manifest` `.tar.gz` 与完整 `.img` / `.img.gz`
  - 校验包体 SHA-256、按需刷写 `super`、同步 ESP、写入 `SPX-OTA-RESULT.txt` 与 `SPX-OTA-LAST.log`
  - 支持 `WIPE_DATA=0`（默认保留 `userdata` 与触控校准）或 `WIPE_DATA=1`（全新安装：清空 `userdata`、`metadata`、`misc`）
- `probe/ota/spx-ota-boot`：Android 启动后后台 root OTA 桥接守护进程：
  - 接管 `android.settings.SYSTEM_UPDATE_SETTINGS`，启用 `org.surface.advancedcontrol/.SystemUpdateActivity` 并停用冲突更新页
  - 处理 `ota-refresh` / `ota-inspect` / `ota-install` / `ota-cancel` / `ota-clear-result` / `ota-reboot`
  - `manifest` 且 `super` 未变化且 `WIPE_DATA=0` 时走在线 ESP 原子更新；其余情况暂存 `/data/ota/pending.pkg` 并写入 `SPX-OTA-PENDING` 走重启离线更新
- `scripts/full_image_ota.py`：交叉编译 `spx-ota-tool`，写入 `/bin/spx-ota-{tool,stage,boot}`、`/etc/spx-ota-info`（含当前 `super` SHA-256），并 patch `/init`。
- `probe/test2/prepare-image-ssd`：将非 `userdata` 分区大小校验放宽为 `<`，兼容旧 39 分区布局在升级后引导 5 分区通用 initramfs。

### 1.2 系统更新 UI（`SurfaceAdvancedControl.apk`）
- `probe/advanced-control/SystemUpdateActivity.java`、`OtaClient.java`、`OtaPolicy.java`
- `probe/advanced-control/AndroidManifest.xml`：声明 `android.settings.SYSTEM_UPDATE_SETTINGS`（`priority="999"`）
- `probe/advanced-control/MainActivity.java`、`SettingsTheme.java`、`Strings.java`、`res/values*/strings.xml`：提供系统更新入口、中英双语支持及保留数据/全新安装选择与二次确认。

### 1.3 构建与 OTA 打包集成
- `scripts/build_ota_package.py`：支持 `--mode auto|delta|full`。全量构建自动生成 OTA 包；相对基线 `super` 未变化时仅打包变更的 ESP 文件与描述清单 `ota-manifest.kv` + `ota-files.tsv`；`super` 变化或 `--mode full` 时包含 `super` 镜像与校验信息。
- `scripts/full_image.py` & `scripts/build-lts.py`：`build.py full-image` 默认生成 `ota/` 产物，也可单独执行 `python3 scripts/build.py ota ...`。

### 1.4 PC 端 Root ADB 一键升级脚本
- Windows PowerShell：`tools/Upgrade-GooglebookViaAdb.ps1` + 启动器 `tools/Upgrade-GooglebookViaAdb.cmd`
- Linux POSIX sh：`tools/upgrade-googlebook-via-adb.sh`
- 能力：
  - 支持 `.img.gz` / `.img` / `.tar.gz`，自动识别包类型并解析 GPT / manifest
  - 支持保留数据或全新安装（`-WipeData` / `--wipe-data`）
  - 校验目标设备 `super` 容量与 `/data` 剩余空间；旧版无 `/etc/spx-ota-info` 时拒绝仅含 ESP 的 delta 包
  - 复用设备端已校验的 `/data/ota/pending.pkg`，命中 `check-staged-pkg` 时跳过重复 `adb push`
  - **双系统安全的 GRUB 修复（`ensure_grub_boots_sq2nat`）**：备份并清理残留 `EFI/BOOT/grubenv`；检测到含 `bootmgfw.efi` 的 Windows 双系统菜单时完整保留 Windows 入口并补全 `/EFI/SQ2Nat/start.efi`；单系统旧菜单自动备份为 `EFI/BOOT/grub.cfg.ota-bak` 并更新为标准 `/EFI/SQ2Nat/start.efi` 菜单。

---

## 2. 关键构建提示

- `--boot-gnu-efi` 须指向兼容修订 `aeee48d838d40e8ef787748d5dc066ea2fb99dbe`，避免旧 `gnu-efi` 触发 PE32+ shim 魔数报错。
- 保持 Turnip 运行库干净（`vulkan.freedreno.so`、`libdrm.so`），并传入签名密钥与完整相机/传感器输入目录。
