# OTA & ADB 一键升级系统 Handoff

## 1. 当前分支与提交基线

- **工作分支**: `feat/ota-system-and-adb-upgrade`（基于 `main` / `0d06096` 切出）
- **相关提交记录**（按时间倒序）：
  1. `0d06096` `ota: prevent copy_esp_atomic from clobbering _exp_sha in ADB upgrade helper`
  2. `a9e4134` `ota: repair legacy/trial grub.cfg and stale grubenv on upgrade while preserving Windows dual-boot`
  3. `80a9aef` `Fix 32-bit mksh integer overflow in ADB upgrade & OTA helper scripts and use FAT32-safe cp -r`
  4. `3360959` `Fix PowerShell 5.1 NativeCommandError on adb stderr output and stream push progress`
  5. `e00b5cd` `Allow touch calibration device-key sha256sum in spx-advanced-control-boot runtime policy`
  6. `d75b4c8` `Add device-side OTA system, Material You System Update UI, touch settings/calibration improvements, and ADB upgrade scripts`

---

## 2. 已实现的核心架构与文件清单

### 2.1 设备端 OTA 运行时（initramfs）
- `probe/ota/spx-ota-tool.c`：零依赖 C11 工具，静态编译为 ARM64 ELF，提供：
  - `sha256`（文件 / stdin）
  - `detect`（识别 `full-img` / `gzip` / `manifest`）
  - `inspect-gpt`（解析完整磁盘镜像 GPT，`super` 与 `SQ2PROBE` 偏移与大小）
  - `extract-stream`（单遍流式解压/提取并计算 `super` 与 `esp.fat` 的 SHA-256）
- `probe/ota/spx-ota-stage`：initramfs 早期离线升级器（在 `prepare-native-ssd` 发现磁盘与挂载 `/media/esp` 之后、`sq2-loop-slice` 挂载 `super` 之前执行；`sq2.rescue=1` 救援模式下自动跳过）：
  - 消费 `/media/esp/SPX-OTA-PENDING`（立即原子重命名为 `SPX-OTA-INPROGRESS` 防止重启死循环）
  - 支持增量/全量 `manifest` `.tar.gz` 与完整 `.img` / `.img.gz`
  - 校验包体 SHA-256、按需刷写 `super`、同步 ESP、写入 `SPX-OTA-RESULT.txt` 与 `SPX-OTA-LAST.log`
  - 所有更新模式均支持 `WIPE_DATA=0`（默认保留 `userdata` 与触控校准）或 `WIPE_DATA=1`（全新安装：清空 `userdata`、`metadata`、`misc`）
- `probe/ota/spx-ota-boot`：Android 启动后后台 root OTA 桥接守护进程：
  - 接管系统 `android.settings.SYSTEM_UPDATE_SETTINGS`（启用 `org.surface.advancedcontrol/.SystemUpdateActivity`，停用冲突的 GMS 更新页）
  - 处理 `ota-refresh` / `ota-inspect` / `ota-install` / `ota-cancel` / `ota-clear-result` / `ota-reboot`
  - `manifest` 且 `super` 未变化且 `WIPE_DATA=0` 时走在线 ESP 原子更新；其余情况（`super` 变化、全量包、或 `WIPE_DATA=1`）自动暂存 `/data/ota/pending.pkg` 并写入 `SPX-OTA-PENDING` 走重启离线更新
- `scripts/full_image_ota.py`：交叉编译 `spx-ota-tool`，写入 `/bin/spx-ota-{tool,stage,boot}`、`/etc/spx-ota-info`（含当前 `super` SHA-256），并自动 patch `/init`。
- `probe/test2/prepare-image-ssd`：将非 `userdata` 分区大小校验从 `!=` 放宽为 `<`，使旧 39 分区布局（如 v0.7.0 的 12 GiB `super`、2 GiB `SQ2PROBE`）在升级后可无缝引导 5 分区通用 initramfs。

### 2.2 现代化 Material You 系统更新 UI（`SurfaceAdvancedControl.apk`）
- `probe/advanced-control/SystemUpdateActivity.java`、`OtaClient.java`、`OtaPolicy.java`
- `probe/advanced-control/AndroidManifest.xml`：声明 `android.settings.SYSTEM_UPDATE_SETTINGS`（`priority="999"`）
- `probe/advanced-control/MainActivity.java`、`SettingsTheme.java`、`Strings.java`、`res/values*/strings.xml`：首页新增系统更新入口，中英双语支持，所有更新包均提供“保留用户数据与触控校准 / 不保留数据（全新安装）”手动选择与二次确认。

### 2.3 构建与 OTA 打包集成
- `scripts/build_ota_package.py`：支持 `--mode auto|delta|full`
  - 全量构建时自动联动生成 OTA 包；若相对基线 `super` 未变化，仅打包变更的 ESP 文件与描述清单 `ota-manifest.kv` + `ota-files.tsv`（不含 `super`），显著缩小包体；若 `super` 变化或显式指定 `--mode full`，则包含 `super` 镜像与校验信息。
- `scripts/full_image.py` & `scripts/build-lts.py`：`build.py full-image` 默认自动生成 `ota/` 产物，也可单独执行 `python3 scripts/build.py ota ...`。

### 2.4 PC 端 Root ADB 一键升级脚本（跨 v0.7.0 升级）
- Windows PowerShell：`tools/Upgrade-GooglebookViaAdb.ps1` + 双击启动器 `tools/Upgrade-GooglebookViaAdb.cmd`（已同步导出到 `D:\`）
- Linux POSIX sh：`tools/upgrade-googlebook-via-adb.sh`（已同步导出到 `D:\`）
- 特性：
  - 支持 `.img.gz` / `.img` / `.tar.gz`，自动识别包类型并解析 GPT / manifest
  - 交互/命令行支持保留数据或全新安装（`-WipeData` / `--wipe-data`）
  - 自动校验目标设备 `super` 容量与 `/data` 剩余空间；旧版（无 `/etc/spx-ota-info`）拒绝仅含 ESP 的 delta 包，强制要求全量包
  - 复用设备端已校验的 `/data/ota/pending.pkg`（`check-staged-pkg` 命中后跳过重复 `adb push`）
  - **双系统安全的 GRUB 修复（`ensure_grub_boots_sq2nat`）**：
    - 备份并清理残留 `EFI/BOOT/grubenv`（防止 `next_entry` 劫持重启入口）
    - 检测到 Windows 双系统菜单（含 `bootmgfw.efi`）时完整保留 Windows 入口，仅在缺少 `/EFI/SQ2Nat/start.efi` 时替换旧 Googlebook 链式加载路径
    - 单系统旧/测试 `grub.cfg`（如指向 `SQ2N19` / `SQ2KSU` 等）自动备份为 `EFI/BOOT/grub.cfg.ota-bak` 并更新为标准 `/EFI/SQ2Nat/start.efi` 菜单

---

## 3. 实机排障中发现并修复的问题

1. **PowerShell 5.1 将 `adb.exe` stderr 进度当成终止错误**（已修 `3360959`）：在 `Invoke-Adb` 内临时切换 `$ErrorActionPreference = 'Continue'`，并对大文件 `adb push` 启用 `-StreamOutput`。
2. **Android 32-bit `mksh` 整数溢出与 FAT32 `cp -a` 权限报错**（已修 `80a9aef`）：`12 GiB` / `2 GiB` 分区在 `$(( _s * 512 ))` 中溢出为 `0` / 负数；改用 `blockdev --getsize64` + `awk` 64 位运算，FAT32 备份改为 `cp -r`。
3. **保留旧 `EFI/BOOT/grub.cfg` 导致重启回退到旧实验入口（`SQ2N19`），未进入新 `SQ2Nat` initramfs**（已修 `a9e4134`）：已改为 `ensure_grub_boots_sq2nat` 逻辑并覆盖全部 4 处实现（Windows/Linux ADB 脚本、`spx-ota-boot`、`spx-ota-stage`）。
4. **POSIX sh 全局变量被 `copy_esp_atomic` 覆盖导致 `SPX-OTA-PENDING` 中 `PACKAGE_SHA256=` 为空**（已修 `0d06096`）：已为 ADB 设备端助手所有函数局部变量添加独立前缀（`_ca_*` / `_gb_*` / `_bk_*` / `_vi_*` 等），并补充回归测试。

---

## 4. 验证状态与诚实边界

### 已验证（主机自动化测试）
- `python3 -m unittest discover -s tests -p 'test_ota_system.py' -v`：**10/10 通过**（覆盖 `spx-ota-tool`、OTA 打包、`full_image_ota`、`spx-ota-boot`、`spx-ota-stage`、`prepare-image-ssd`、Java `OtaPolicy`/`Strings` 与 APK manifest 意图、Linux `sh` mock-ADB、`check-staged-pkg` 复用、4 种 GRUB 场景 + `_exp_sha` 非覆盖回归、Windows `powershell.exe` 语法解析与内嵌 C# GPT/流提取）。
- `tests/test_disk_image.py`（6/6）与 `tests/test_advanced_control.py`（13/13）通过。

### 已构建产物（位于本地工作区，未入 Git）
- 输出目录：`/mnt/spx-workspace/chromebookOS/out/release-e00b5cd-20261119/`
  - 完整镜像：`disk/surface-pro-x-6.18-hybrid-generic.img.gz`（SHA-256: `7632295064d794b3e3b98d2b9680a71616ade8a56c6f84ffa1f70846de331d52`）
  - ESP 增量 OTA 包：`ota/surface-pro-x-6.18-hybrid-ota-esp.tar.gz`
  - 注意：该产物构建于 `e00b5cd`；`80a9aef` / `a9e4134` / `0d06096` 对 `spx-ota-stage` 与 `spx-ota-boot` 的增量改进（64 位 `awk` 分区大小、`ensure_grub_boots_sq2nat`）需要下一次重编全量镜像后进入新镜像包。

### 实机验证状态（部分完成，尚未最终闭环）
- 已在测试样机上验证了旧 39 分区磁盘经 `EFI/SQ2Nat/initramfs.cpio.gz` 触发离线 `spx-ota-stage` 刷写 `super` 并二次重启进入新通用镜像：重启后 Wi-Fi 自动回连且 22 端口呈 `Connection refused`（符合 `anonymous=True` 正式版移除 `dropbear`、同时保留原用户 Wi-Fi 配置的行为）。
- **尚未完成的实机闭环验证**：
  1. 通过重新授权的 USB/无线 ADB 读取实机 `/etc/spx-ota-info`、`/media/esp/SPX-OTA-RESULT.txt`、`/media/esp/SPX-OTA-LAST.log`，确认 `STATUS=OK` 与新 `super` 实际生效；
  2. 实机目视验收 **设置 → 系统 → 系统更新** 现代化 Material You UI、触控校准数据保留、以及一次完整设备端增量 OTA（`ota-esp.tar.gz`）与全新安装（`WIPE_DATA=1`）回归；
  3. 尚未在 SQ1 机型与 Windows 双系统机型上做跨设备实机回归。

---

## 5. 下一步建议（Next Steps）

1. **完成本次升级实机核验**：
   - 将平板通过下方 USB-C 口连接电脑并重新授权 USB 调试（正式版已移除 `SQ2TEST` 固定测试序列号），核对 `SPX-OTA-RESULT.txt`、`SPX-OTA-LAST.log`、`/etc/spx-ota-info`、用户应用与触控校准保留情况。
2. **决定是否合并 `device_faker_gfx` 修复并重编正式包**：
   - 本地 `/home/kanoyami/df-gfx-build` 中存在未提交的 2 处实机修复；如需纳入发布，应先提交推送该仓库并更新预置 `30-device_faker.zip`，再基于当前 `HEAD`（含 `0d06096` 的 `spx-ota-stage` / `spx-ota-boot` 改进）重新构建 `full-image` + `ota` 包。
3. **重编完整镜像时的关键构建提示**：
   - `--boot-gnu-efi` 需指向兼容版本（`aeee48d838d40e8ef787748d5dc066ea2fb99dbe`，如 `/mnt/spx-workspace/chromebookOS/out/release-v0.7.5/disk/work/boot-source/gnu-efi`），避免旧 `gnu-efi` 触发 PE32+ shim 魔数报错。
   - 保持 Turnip 运行库干净（`out/sq2-turnip/runtime/{vulkan.freedreno.so,libdrm.so}` 不含旧实验标记），并传入官方签名密钥与完整相机/传感器输入目录。
