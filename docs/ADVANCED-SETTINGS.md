# 高级设置与设备端管理（Surface Advanced Settings）

> 相关文档：[README（中文）](../README.zh-CN.md) · [README (EN)](../README.md) · [霍尔 + IR 人脸解锁方案](IR-HALL-FACE-UNLOCK-PLAN.md) · [系统更新（OTA）与刷机移交说明](OTA-HANDOFF.md) · [触控校准](TOUCH-CALIBRATION.md) · [Type-C 与 USB 调试](TYPEC.md)

Surface Pro X 移植版在 Android 系统内置 **高级设置（`SurfaceAdvancedControl.apk`，包名 `org.surface.advancedcontrol`）**，提供从日常性能/图形调优、触控校准备份、无线 ADB / root SSH 开发调试，到本机 OTA 升级及 USB LiveCD 一键安装至内置 NVMe 硬盘的一站式图形化管理能力。

---

## 1. 整体架构与安全设计

- **界面与本地化**：采用 Material 3 / Material You 风格设计，支持系统浅色 / 深色主题、动态取色及中英双语自动切换（非中文系统默认显示英文）。
- **无人工授权提权的 Root 桥接**：应用本身仅运行于普通系统应用权限，无需用户手动向 KernelSU 授权。所有特权操作均通过内置于 initramfs root 上下文的守护脚本转发执行：
  - `/bin/spx-advanced-control-boot`：负责 Vulkan 策略、CPU 调速器、触控停用/防误触/校准、霍尔传感器与 OV7251 红外人脸解锁开关、网络 ADB 与 Dropbear SSH。
  - `/bin/spx-ota-boot`、`spx-ota-stage`、`/bin/spx-internal-install`、`/bin/spx-ota-tool`：负责 OTA 校验/暂存升级以及 USB LiveCD 到内置硬盘的全盘写入。
- **结构化数据边界**：应用与 root 桥接脚本之间仅通过带随机 nonce 的结构化请求交互（`/run/spx-advanced-control-request`、`/run/spx-ota-boot-request`），严格校验包名、调速器名、公钥格式、块设备路径与文件大小上限，不拼接或 `eval` 任何不可信命令字符串。
- **配置持久化与救援保护**：配置保存于 `/data/vendor/spx-advanced-control` 与 `/data/vendor/sq2-touch-calibration`，每次正常开机由桥接服务自动恢复；若从 GRUB 进入救援模式（`sq2.rescue=1`）或显式传入 `sq2.cpufreq=0`，将按策略跳过调试/停用触控/调速器恢复，避免影响救援排查。

---

## 2. 主页功能模块总览

高级设置主页包含七个一级卡片入口：

| 模块 | 英文名称 | 核心用途 |
| :--- | :--- | :--- |
| **Vulkan 配置** | Vulkan Configuration | 全局与分应用切换 Mesa Turnip / 高通 QTI Vulkan 驱动 |
| **性能模式** | Performance Mode | 实时切换并持久化全核 cpufreq 调度策略 |
| **触控** | Touch | 停用触控屏、切换防误触补丁、查看状态及导入/导出/重校准 |
| **调试** | Debugging | 开关网络 ADB（5555）、initramfs `root` SSH（22）、管理密码与公钥 |
| **霍尔与红外解锁** | Lid & IR Face Unlock | 开关霍尔开盖唤醒、监测键盘姿态、控制 OV7251 IR 人脸解锁（Class 2 / WEAK）及自检 |
| **系统更新** | System update | 选择本地 `.tar.gz` / `.img` / `.img.gz` 进行无损或清空数据的系统升级 |
| **写入内置硬盘** | Install to Internal Disk | 从 USB LiveCD 启动后，将系统完整写入 Surface Pro X 内置 NVMe SSD |

---

## 3. 各模块详细说明

### 3.1 Vulkan 配置（Vulkan Configuration）

- **全局默认驱动**：可在 **自动策略（Auto Policy）**、**MESA (Turnip)** 与 **Qcom (QTI)** 间切换全局默认行为。
  - 当全局切换为 **Qcom (QTI)** 时，系统仍强制保护系统 UID（`< 10000`）及 `com.android.systemui` 使用 Mesa Turnip，防止桌面与系统 UI 异常。
- **分应用覆盖与置顶**：按应用列表可单独指定每个软件使用 默认策略 / Mesa Turnip / QTI Vulkan，所有已手工覆盖的应用会自动置顶显示。
- **生效方式**：提供 **保存**（下次启动该应用生效）与 **保存并停止应用**（写入策略并立即强停目标应用以便即刻重载驱动）。普通应用默认已配合开启 QCOM `VM_BIND` 缓存提交路径。

### 3.2 性能模式（Performance Mode）

- **动态能力发现**：桥接脚本实时遍历 `/sys/devices/system/cpu/cpufreq/policy*/scaling_available_governors`，列出当前内核全部 CPU 簇共同支持的调度器（例如 `schedutil`、`performance`、`ondemand`、`conservative`、`powersave`、`userspace` 等）。
- **即时生效与跨重启保留**：选中后立即写入全部 CPU 簇，并将策略记录到 `/data/vendor/spx-advanced-control/cpu_governor`，后续每次正常启动自动恢复；需要恢复系统默认行为时，点击 **恢复默认** 即可。

### 3.3 触控（Touch）

- **停用触控屏（Disable Touchscreen）**：提供二次确认对话框；确认后立即停止 `iptsd` 服务并持久化停用标志，在桌面外接键鼠使用或屏幕物理故障排查误触时非常实用（可在高级设置中重新开启；救援模式下自动忽略此停用状态）。
- **防误触补丁（Ghost-Touch Patch）**：切换噪声过滤补丁的启用/停用状态（通过 `.noise.disabled` 标志控制，切换时保留已采集的噪声校准样本不删除）。
- **实时校准摘要**：展示当前生效的 `SPXNOISE1` / `SPXNOISE2` / `SPXNOISE3` / `SPXTAP1` 参数状态、`ContactSizeMax`、`iptsd` 运行状态及触控硬件 ID。
- **校准与备份恢复**：
  - **重新校准**：直接拉起系统统一触控校准向导（`org.surface.touchcalibration.unified`）。
  - **导出校准**：将当前完整触控与防误触配置文件打包导出为 `.spkcal` 备份，方便跨安装迁移。
  - **导入校准**：支持读取 `.spkcal`、`.profile` 与 `.noise` 文件（单文件大小上限 `132096` 字节），导入后自动验证并重载 `iptsd`。
  - **清除校准**：清空已保存的校准及噪声样本以重新初始化。

### 3.4 调试（Debugging）

#### 实时调试地址展示
界面顶部会展示通过系统网口扫描到的当前可用调试地址，并自动过滤 `lo`、`rmnet*`、`tun*` 等无关接口：
- **LAN IP**：物理 USB 网卡或 Wi-Fi 分配的局域网 IPv4 地址，可用于同一局域网电脑执行 `adb connect` 或 `ssh`。
- **VM IP**：Linux 虚拟化网桥（`avf_tap*`、`vmnic*`、`crosvm*`）地址。

#### 网络 ADB（端口 `5555`）
- 一键开启或关闭 TCP `5555` 端口网络 ADB，持久化至 `/data/vendor/spx-advanced-control/adb_tcp_enabled`，同时自动配置系统防火墙放行或收紧 `5555` 入站规则。
- 在开发机端连接（首次连接仍需在平板屏幕确认 RSA 授权，因镜像启用了 `ro.adb.secure=1`）：
  ```sh
  adb connect <LAN_IP>:5555
  ```
- **与下口 USB-C ADB 共存**：Surface Pro X 下方 USB-C 口接入电脑时通过 `/vendor/bin/adb-usb-bridge` 转发本地环回 ADB。关闭“网络 ADB”时，桥接脚本会自动检测 `adb-usb-bridge` 并仅将 `service.adb.listen_addrs` 收窄为 `tcp:localhost:5555`，确保在封闭局域网网络 ADB 的同时不影响下口 USB ADB 连接。

#### SSH 服务（initramfs `root`，端口 `22`）
- 管理运行在 initramfs 根命名空间下的轻量 BusyBox **Dropbear** SSH 服务（端口 `22`），即使 Android 用户态或图形桌面卡死仍可登录内核/initramfs 排障。
- **持久化开机自启**：开关状态与防火墙放行规则自动跨重启保留。
- **密码认证管理**：默认出厂镜像 `/etc/shadow` 内 `root` 密码为锁定状态（`!`）；可在应用内点击 **修改密码**（哈希存储为 SHA-512，不保存明文）或 **清除密码**（恢复锁定并仅允许公钥登录）。
- **公钥管理与 `ssh-copy-id` 自动持久化**：
  - 应用内支持 **导入公钥 / 证书**：可从文件选择或直接粘贴 OpenSSH 公钥（`ssh-ed25519`、`ssh-rsa`、`ecdsa-sha2-nistp256/384/521`、`sk-ssh-ed25519@openssh.com`、`sk-ecdsa-sha2-nistp256@openssh.com`，单次限制 `65536` 字节）及 X.509/PEM 证书，自动校验去重并持久化。
  - 外部 `ssh-copy-id` 兼容：`/root/.ssh` 在系统启动及启用 SSH 时自动软链接到持久化目录 `/data/vendor/spx-advanced-control/ssh/root_ssh`，Dropbear 的 Ed25519 主机私钥亦持久化保存于 `/data/vendor/spx-advanced-control/ssh/dropbear_ed25519_host_key`。因此直接在电脑执行：
    ```sh
    ssh-copy-id root@<LAN_IP>
    ```
    写入的公钥与服务端主机指纹在重启后依然全部有效。
  - **清除密钥**：一键移除全部已授权 `authorized_keys`。

> 提示：若在 Android 启动前即发生故障（无法进入桌面），可在 GRUB 启动菜单选择救援模式，参见 [BUILD — 第 7 节（救援模式与边界）](BUILD.md#7-边界)。

### 3.5 霍尔与红外解锁（Lid & IR Face Unlock）

详见架构设计文档：[霍尔传感器 + OV7251 IR 人脸解锁实施计划](IR-HALL-FACE-UNLOCK-PLAN.md)。

- **霍尔开盖唤醒与合盖休眠（`SW_LID`）**：
  - 通过动态发现 `/sys/devices/platform/gpio-keys-lid`（或对应 `gpio-keys` `switches` 含 `0` 的节点）读写 `disabled_switches`，可在不停用内核其他按键的前提下关闭或恢复 `SW_LID`（使用第三方强磁皮套误触发休眠时尤其实用，配置持久化于 `/data/vendor/spx-advanced-control/hall_lid_enabled`）。
  - 若在内核命令行传入 `spx.hall_lid=0`（或以救援模式 `sq2.rescue=1` 启动），桥接服务将按命令行强制禁用 `SW_LID` 并阻止界面开启。
  - **实时状态与反折保护**：同页实时展示 `OPEN` / `CLOSED` / `FOLDED_IGNORED` / `DISABLED` 以及 SSAM 键盘姿态（`laptop` / `closed` / `folded-back` / `folded-canvas` / `book` / `disconnected`）；当键盘反折至机身背面时，内核与 HAL 协同屏蔽误吸触发。
- **OV7251 850nm 红外人脸解锁（Class 2 / `WEAK`）**：
  - 当系统启动检测到 `ov7251` I2C 传感器且未通过 `spx.face=0` / `sq2.camera=0` 禁用时，`/bin/spx-face-stage` 自动挂载 `android.hardware.biometrics.face.IFace/default`（AIDL V4，`WEAK`）。
  - **总开关与旋转角**：支持即时切换 `persist.spx.face.enabled`（禁用后 HAL 在打开相机前即拒绝认证/录入，保证 850nm 红外补光灯零点亮），以及按需调节 OV7251 传感器顺时针旋转角度（`0°` / `90°` / `180°` / `270°`，默认 `90°`，写入 `persist.spx.face.rotation`），两项配置均持久保存并在开机恢复。
  - **快捷跳转与一键自检**：提供 **打开系统人脸解锁设置**（优先尝试 `android.settings.FACE_SETTINGS`，不可用时回退安全设置），以及通过 `spx-face-cli`（`--self-test`、`--status` 与开盖态 `--benchmark-ir 5`）一键核验 HMAC/HAT 密码原语、活体引擎、霍尔开关与 OV7251 预处理延迟的诊断按钮。

### 3.6 系统更新（System update）

- 同时接入 Android 系统设置中的 **系统更新** 入口（`android.settings.SYSTEM_UPDATE_SETTINGS`）与高级设置主页。
- 支持选择本机存储上的升级包（`.tar.gz`）或完整压缩/原始镜像（`.img.gz` / `.img`）：
  - **保留用户数据（默认）**：保留 `/data`（应用、登录态、触控校准、高级设置 SSH/调试/调速器配置等）及 Windows 同盘双启动 `grub.cfg` 菜单与 ESP 保留文件。
  - **清除数据并重装**：重新格式化 `userdata` 与 `metadata`（仅保留新系统基础分区状态）。
- 支持两种底层写入通道：纯 ESP 更新可在必要校验后快速写入并重启；涉及 `super` 或完整镜像升级时，会通过 `/bin/spx-ota-stage` 写入 `SQ2PROBE` 与启动阶段标识，在重启早期 initramfs（Android 挂载 `super` 前）安全完成全量更新与 SHA-256 校验。完整用法与 PC 端一步脚本见 [OTA-HANDOFF](OTA-HANDOFF.md)。

### 3.7 写入内置硬盘（Install to Internal Disk，LiveCD 模式）

当你先将 Googlebook OS 刷入外接 USB SSD / U 盘试用（LiveCD 模式），确认硬件与使用习惯均符合预期后，可直接在运行中的 USB Android 桌面使用该功能将系统安装到 Surface Pro X 内置 NVMe 硬盘：

1. **适用前提与准入校验**（由 `/bin/spx-internal-install` 自动检测）：
   - 当前系统确实是从 USB 设备启动（`BOOT_TRANSPORT=usb`）；
   - 当前磁盘及镜像为标准 5 分区兼容布局（`super`、`metadata`、`misc`、`SQ2PROBE`、`userdata`）且源 ESP 文件完整；
   - 本机存在容量满足最小布局要求（含 ESP、5 个分区及最小 `userdata`）的**非 USB 内部块设备**（如 `/dev/nvme0n1`）。
2. **操作步骤与安全确认**：
   - 进入 **高级设置 -> 写入内置硬盘**，确认检查通过且建议已接入 Surface 原装充电器 / AC 电源；
   - 选择目标内置 NVMe 磁盘，勾选高风险不可逆确认（告知**将彻底抹除目标内置硬盘全部现有分区与数据，包括 Windows 及其全部个人文件**，且**不会迁移当前 USB 启动盘上的 `userdata`**）；
   - 开始安装后，进度条依次经过：**清理残留挂载 -> 重建内置硬盘 5 分区 GPT 表 -> 写入 ESP（采用通用内置独立启动 `grub.cfg`）-> 复制 `super` 并逐字节回读校验 SHA-256 -> 初始化 `metadata`/`misc`/`SQ2PROBE`/`userdata`**。
3. **完成与首启**：
   - 安装成功后强烈建议直接点击界面的 **立即关机**（不要直接“重启”，避免 UEFI 仍优先从外置 USB 引导）；
   - 待屏幕与电源完全关闭后**拔下 USB 启动盘**，再按电源键开机，即可直接从内置 NVMe 启动 Googlebook OS，并在首次开机时自动将 `userdata` 扩容至整盘剩余空间及运行触控校准向导。
   - **若希望保留 Windows 同盘双启动**，请勿使用“写入内置硬盘”，改用 Windows 端 `scripts/Install-GooglebookDualBoot.cmd` / `.ps1`。
