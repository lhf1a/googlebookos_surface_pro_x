# 高级设置与设备端管理（Surface Advanced Settings）

> 相关文档：[README（中文）](../README.zh-CN.md) · [README (EN)](../README.md) · [霍尔 + IR 人脸解锁方案](IR-HALL-FACE-UNLOCK-PLAN.zh-CN.md) · [系统更新（OTA）与刷机移交说明](OTA-HANDOFF.zh-CN.md) · [触控校准](TOUCH-CALIBRATION.zh-CN.md) · [Type-C 与 USB 调试](TYPEC.zh-CN.md)

Surface Pro X 移植版内置 **高级设置（`SurfaceAdvancedControl.apk`，`org.surface.advancedcontrol`）**，提供性能与图形调优、触控校准管理、无线 ADB / root SSH 调试、本机 OTA 升级及 USB LiveCD 安装至内置 NVMe 硬盘的一站式图形化管理。

---

## 1. 整体架构与安全设计

- **界面与本地化**：采用 Material 3 / Material You 风格，支持浅色/深色主题、动态取色及中英双语自动切换（非中文系统默认英文）。
- **Root 桥接**：应用运行于普通系统应用权限，无需手动向 KernelSU 授权。特权操作由 initramfs root 上下文守护脚本执行：
  - `/bin/spx-advanced-control-boot`：负责 Vulkan 策略、CPU 调速器、触控停用/防误触/校准、霍尔传感器与 OV7251 红外人脸解锁开关、网络 ADB 与 Dropbear SSH。
  - `/bin/spx-ota-boot`、`spx-ota-stage`、`/bin/spx-internal-install`、`/bin/spx-ota-tool`：负责 OTA 校验/暂存升级及 USB LiveCD 到内置硬盘的全盘写入。
- **结构化请求边界**：应用与桥接脚本间通过带随机 nonce 的请求文件交互（`/run/spx-advanced-control-request`、`spx-ota-boot-request`），严格校验包名、调速器名、公钥格式、块设备路径与文件大小上限，不拼接或 `eval` 不可信命令字符串。
- **持久化与救援保护**：配置保存于 `/data/vendor/spx-advanced-control` 与 `/data/vendor/sq2-touch-calibration`，每次正常开机自动恢复；在救援模式 `sq2.rescue=1` 或 `sq2.cpufreq=0` 下跳过调试、停用触控与调速器恢复。

---

## 2. 主页功能模块总览

| 模块 | 英文名称 | 核心用途 |
| :--- | :--- | :--- |
| **Vulkan 配置** | Vulkan Configuration | 全局与分应用切换 Mesa Turnip / 高通 QTI Vulkan 驱动 |
| **性能模式** | Performance Mode | 实时切换并持久化全核 cpufreq 调度策略 |
| **触控** | Touch | 停用触控屏、切换防误触补丁、查看状态及导入/导出/重校准 |
| **调试** | Debugging | 开关网络 ADB（5555）、initramfs `root` SSH（22）、管理密码与公钥 |
| **霍尔与红外解锁** | Lid & IR Face Unlock | 开关霍尔开盖唤醒、监测键盘姿态、控制 OV7251 IR 人脸解锁（Class 2 / WEAK）及自检 |
| **系统更新** | System update | 选择本地 `.tar.gz` / `.img` / `.img.gz` 进行保留数据或清数据的系统升级 |
| **写入内置硬盘** | Install to Internal Disk | 从 USB LiveCD 启动后，将系统完整写入内置 NVMe SSD |

---

## 3. 各模块说明

### 3.1 Vulkan 配置

- **全局默认驱动**：支持在 **自动策略**、**MESA (Turnip)** 与 **Qcom (QTI)** 间切换。切换为 **Qcom (QTI)** 时，系统 UID（`< 10000`）与 `com.android.systemui` 仍强制使用 Mesa Turnip，防止桌面与系统 UI 异常。
- **分应用覆盖与置顶**：可单独指定每个应用使用默认策略 / Mesa Turnip / QTI Vulkan，已覆盖的应用自动置顶。
- **生效方式**：提供 **保存**（下次启动生效）与 **保存并停止应用**（立即强停目标应用以重载驱动）。普通应用默认开启 QCOM `VM_BIND` 缓存提交路径。

### 3.2 性能模式

- **动态能力发现**：遍历 `/sys/devices/system/cpu/cpufreq/policy*/scaling_available_governors`，列出全 CPU 簇共同支持的调度器（如 `schedutil`、`performance`、`ondemand`、`conservative`、`powersave`、`userspace`）。
- **即时生效与持久化**：选中后立即写入全簇并持久化至 `/data/vendor/spx-advanced-control/cpu_governor`，开机自动恢复；点击 **恢复默认** 可还原系统默认行为。

### 3.3 触控

- **停用触控屏**：二次确认后停止 `iptsd` 并持久化停用标志，可在高级设置中重新开启（救援模式下自动忽略）。
- **防误触补丁**：通过 `.noise.disabled` 标志开关噪声过滤补丁，切换时保留已采集的噪声样本。
- **实时校准摘要**：展示当前 `SPXNOISE1` / `SPXNOISE2` / `SPXNOISE3` / `SPXTAP1` 状态、`ContactSizeMax`、`iptsd` 运行状态及触控硬件 ID。
- **校准与备份恢复**：
  - **重新校准**：拉起统一触控校准向导 `org.surface.touchcalibration.unified`。
  - **导出校准**：将当前触控与防误触配置导出为 `.spkcal` 文件。
  - **导入校准**：读取 `.spkcal`、`.profile` 与 `.noise` 文件（单文件上限 `132096` 字节），校验后自动重载 `iptsd`。
  - **清除校准**：清空已保存的校准及噪声样本。

### 3.4 调试

#### 实时调试地址
界面顶部扫描可用网口并过滤 `lo`、`rmnet*`、`tun*` 等接口：
- **LAN IP**：物理 USB 网卡或 Wi-Fi 的局域网 IPv4 地址。
- **VM IP**：Linux 虚拟化网桥（`avf_tap*`、`vmnic*`、`crosvm*`）地址。

#### 网络 ADB（端口 `5555`）与下口 USB-C 模式
- **网络 ADB**：一键开关 TCP `5555` 网络 ADB，持久化至 `/data/vendor/spx-advanced-control/adb_tcp_enabled` 并同步更新防火墙规则。
- 镜像启用了 `ro.adb.secure=1`，首次连接需在屏幕确认 RSA 授权：
  ```sh
  adb connect <LAN_IP>:5555
  ```
- **下口 USB-C 模式**（**自动** / **仅 ADB** / **仅主机**）：调用 `/bin/spx-lower-usb` 与 `/bin/spx-lower-usb-watch` 控制下口在有线 ADB 与 DP/USB 主机间自动或手动切换，持久化于 `/data/vendor/surface-usb/lower-mode`（下口承载启动盘时自动锁定为主机模式）。
- **下口 USB-C ADB 共存**：下口通过 `/vendor/bin/adb-usb-bridge` 转发本地环回 ADB。关闭网络 ADB 时，桥接脚本将 `service.adb.listen_addrs` 收窄为 `tcp:localhost:5555`，不影响下口 USB ADB。

#### SSH 服务（initramfs `root`，端口 `22`）
- 管理运行在 initramfs 根命名空间下的 BusyBox **Dropbear** SSH 服务，系统 UI 异常时仍可登录排查。开关状态与防火墙规则跨重启保留。
- **密码管理**：默认镜像 `/etc/shadow` 中 `root` 为锁定状态 `!`；应用内支持 **修改密码**（SHA-512 哈希存储）与 **清除密码**（恢复锁定，仅允许公钥登录）。
- **公钥管理与 `ssh-copy-id`**：
  - **导入公钥 / 证书**：支持文件选择或粘贴 OpenSSH 公钥（`ssh-ed25519`、`ssh-rsa`、`ecdsa-sha2-nistp256/384/521`、`sk-ssh-ed25519@openssh.com`、`sk-ecdsa-sha2-nistp256@openssh.com`，上限 `65536` 字节）及 X.509/PEM 证书。
  - `/root/.ssh` 自动软链接至 `/data/vendor/spx-advanced-control/ssh/root_ssh`，Dropbear Ed25519 主机私钥保存于 `/data/vendor/spx-advanced-control/ssh/dropbear_ed25519_host_key`，可直接执行 `ssh-copy-id root@<LAN_IP>` 并在重启后保持有效。
  - **清除密钥**：移除全部已授权 `authorized_keys`。

> Android 启动前故障可在 GRUB 选择救援模式，参见 [BUILD — 第 7 节](BUILD.zh-CN.md#7-边界)。

### 3.5 霍尔与红外解锁

架构详见 [霍尔传感器 + OV7251 IR 人脸解锁实施计划](IR-HALL-FACE-UNLOCK-PLAN.zh-CN.md)。

- **霍尔开关（`SW_LID`）**：
  - 动态发现 `/sys/devices/platform/gpio-keys-lid`（或 `switches` 含 `0` 的 `gpio-keys` 节点）并读写 `disabled_switches`，持久化于 `/data/vendor/spx-advanced-control/hall_lid_enabled`。
  - 传入 `spx.hall_lid=0` 或 `sq2.rescue=1` 时强制禁用 `SW_LID`。
  - **实时状态与反折保护**：展示 `OPEN` / `CLOSED` / `FOLDED_IGNORED` / `DISABLED` 及 SSAM 键盘姿态（`laptop` / `closed` / `folded-back` / `folded-canvas` / `book` / `disconnected`）；键盘反折至背面时屏蔽误吸触发。
- **OV7251 850nm 红外人脸解锁（Class 2 / `WEAK`）**：
  - 启动检测到 `ov7251` 且未被 `spx.face=0` / `sq2.camera=0` 禁用时，`/bin/spx-face-stage` 挂载 `android.hardware.biometrics.face.IFace/default`。
  - 支持切换 `persist.spx.face.enabled`（禁用后 HAL 在打开相机前即拒绝认证/录入，不点亮红外补光灯）与顺时针旋转角 `persist.spx.face.rotation`（`0°` / `90°` / `180°` / `270°`，默认 `90°`）。
  - 提供 **打开系统人脸解锁设置** 及基于 `spx-face-cli`（`--self-test`、`--status`、`--benchmark-ir 5`）的一键诊断自检。

### 3.6 系统更新

- 接管系统设置 **系统更新** 入口（`android.settings.SYSTEM_UPDATE_SETTINGS`）与高级设置主页入口。
- 支持 `.tar.gz` 升级包与 `.img.gz` / `.img` 完整镜像，提供 **保留用户数据（默认）**（保留 `/data` 及 Windows 双启动 `grub.cfg`）与 **清除数据并重装**（重格式化 `userdata` 与 `metadata`）。
- 纯 ESP 更新在线写入后提示重启；涉及 `super` 或完整镜像时通过 `/bin/spx-ota-stage` 暂存并在重启早期 initramfs 离线完成更新与 SHA-256 校验。详见 [OTA-HANDOFF](OTA-HANDOFF.zh-CN.md)。

### 3.7 写入内置硬盘（LiveCD 模式）

从外置 USB 启动后，可直接将系统安装至内置 NVMe 硬盘：

1. **准入条件**（由 `/bin/spx-internal-install` 检测）：当前为 USB 启动（`BOOT_TRANSPORT=usb`）、源盘为标准 5 分区布局且源 ESP 完整、存在满足最小容量的非 USB 内部块设备（如 `/dev/nvme0n1`）。
2. **安全确认与写入**：建议接入 AC 电源，选择目标内置磁盘并勾选不可逆抹除确认（将清除目标盘全部现有分区与数据，且不迁移 USB `userdata`）。安装依次执行：清理残留挂载 -> 重建 5 分区 GPT -> 写入独立启动 ESP -> 复制 `super` 并回读校验 SHA-256 -> 初始化 `metadata`/`misc`/`SQ2PROBE`/`userdata`。
3. **完成与首启**：安装完成后点击 **立即关机**，完全断电后拔下 USB 启动盘再开机，即可从内置 NVMe 启动并在首启时自动扩容 `userdata`。如需保留 Windows 同盘双启动，请使用 Windows 端 `tools/Install-GooglebookDualBoot.cmd` / `.ps1`。
