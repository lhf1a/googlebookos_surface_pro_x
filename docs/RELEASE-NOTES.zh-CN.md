# 更新日志

预编译磁盘镜像（`*.img.gz`）、跨底包增量 OTA 包（`*-ota-super-delta-*.tar.gz`）、ESP OTA 升级包（`*-ota-esp-*.tar.gz`）与 `SHA256SUMS` 发布于 [GitHub Releases](https://github.com/lhf1a/googlebookos_surface_pro_x/releases)、[Google Drive](https://drive.google.com/drive/folders/1uVAA6UnydSyUHQAO8Y5rTm8SxZ62Zykr) 与 [百度网盘](https://pan.baidu.com/s/51GjLdmTGir0PelomZvboYw)。

---

## v0.9.10 (2026-11-25) *（面向 `v0.9.9` / `286.R2` 的 ESP 在线修复包）*

- **修复触摸校准静置检测倒计时卡住与归零后“服务无响应”**：为静态 ARM64 事件工具 `spx-event-wait`（`probe/test2/spx-event-wait.c`）新增多目录 `inotify` 监听（`--inotify-extra DIR`，兼容旧二进制回退），并更新 `touch-calibration-boot`：在 `noise-learn`/`noise-retest`/`noise-slide`/`tap-begin` 等请求回复 `OK` **前**立即同步发布 `noise-status`/`tap-status`，学习期间同时监听 `$files/request` 与 `/run/iptsd-noise/{status,tap-status}`（毫秒级响应，平时空闲仍保持 20 秒零轮询休眠）。彻底消除静置检测前 ~17–20 秒倒计时不动（跳至 28s 开始）及归零后因 20 秒睡眠错过 60 秒超时阈值的问题。
- **修复旧 5 分区（`3,481 MiB`）设备全量包升级兼容与全量更新 `dalvik-cache` 刷新**：为 `spx-ota-tool`、`spx-ota-stage`、`spx-ota-boot` 及双端 PC ADB 升级脚本（`tools/upgrade-googlebook-via-adb.{sh,ps1}`）新增 `inspect-super-used` 只读扫描与 `extract-stream` 尾部全零校验裁剪，允许有效载荷为 `3,460 MiB`、GPT 预留 `4,096 MiB`（尾部全 `0x00`）的全量镜像直接原地无损升级旧 `3,481 MiB` 5 分区设备（已在 SQ1 `3,481 MiB` 实机保留 `/data` 升级验收通过）；同步修复全量/manifest 刷写 `super` 时未清空完整 ART `dalvik-cache` 的问题、`build_ota_package.py --mode full` 紧凑裁剪尾部零填充，并将 ADB 脚本 ESP 备份门槛由 `350 MiB` 提升至 `650 MiB` 以避免 1 GiB ESP 原子复制空间不足。

---

## v0.9.9 (2026-11-24)

- **升级 Android 17 `mica 16833189`（`CL3B.260622.286.R2`）基线与 `SPXDLT01` 跨底包增量 OTA**：将 Android 17 底包由 `16471258`（`273.R1`）升级至 `16833189`（`286.R2`，安全补丁 `2026-11-01`，并继承蓝牙 HFP 软件数据通路与 A2DP offload 修复）。裁剪 `vendor` 时仅剔除 Dell 专用 `/vendor/etc/recovery/ota_recovery.img`（`119.42 MiB`）并无损保留全部原版 EROFS `uid/gid/mode` 与 SELinux/capability xattrs，使紧凑 `super`（`3,460 MiB`）可在既有 5 分区 `3,481 MiB` 与 39 分区 `12 GiB` 设备上不重分区、不清空数据原地升级，新 5 分区整盘镜像同步将 GPT `super` 预留提升至 `4,096 MiB`。新增零依赖 C11 `SPXDLT01` 4 KiB 块级增量引擎、`spx-ota-stage` 单次解包与同哈希跳过优化、全量 SHA-256 回读校验及非 Dell 硬件 `ota_recovery_updater.rc` 禁用（已在 SQ1 与 SQ2 两套实机完成端到端升级验证）。
- **SC8180X CPU EAS、PowerHint 调优与事件驱动去轮询**：向 SQ1/SQ2 设备树注入 SC8180X 动态功耗系数（小核 `140` / 大核 `440`）激活 Linux EAS 能量感知调度（`sched_energy_aware=1`），调优 `powerhint.sq2.json`，新增静态 ARM64 `spx-event-wait`（inotify/sysfs/uevent）全面替换输入/USB/触控/OTA/高级设置守护进程的定时轮询，并为 `sq2-game-opt` 加入自适应空闲退避与 64 槽位 PID LRU 缓存。
- **GPU、Mesa 与 DPU 性能优化**：合入异步 `msm_gem_close()`（`0001-drm-msm-async-gem-close.patch`）消除 RenderThread 同步等待卡顿、Adreno 680 GPU OPP 峰值 DDR 带宽投票（`0017` + `prepare-spx-gpu-bw-dtb.py`）、Mesa 26.2.4 UBWC/MSAA GMEM/二步式着色器缓存修复、安全 DPU RGB 硬件图层合成（`patches/sm8350-dpu-rgb-overlay.patch`），并启用 SurfaceFlinger 客户端合成缓存（`debug.sf.disable_client_composition_cache=0`）。
- **整机深度空闲功耗优化与 `s2idle` 唤醒风暴修复**：为 Venus 增加 5 秒空闲自动挂起（空闲时关闭 `venus_gdsc`/`vcodec0_gdsc`/`vcodec1_gdsc` 与 `videocc` 时钟），新增未使用 UFS GDSC 断电（`0018`）、NVMe APST（`25ms`）、USB Host 运行时挂起及 SLPI 传感器批处理；移除 `power-input-boot` 强制开启 `surface_aggregator` `wakeup=enabled` 的旧逻辑，消除 `0.43s` 周期 `s2idle` 频繁唤醒风暴（SQ2 实测连续无中断休眠 23 分 52 秒、0 次 BAT1 异常唤醒）。
- **首次触控校准一键跳过与关机/唤醒稳健性修复**：首次开机触控校准向导（`org.surface.touchcalibration.unified` v2/1.1）支持屏幕正常用户一键跳过并二次确认关闭防误触补丁，**高级设置**（`org.surface.advancedcontrol` v6/2.4）同步展示已停用状态及随时重开入口；修复关机外接 DP EDID 释放后使用竞争（`edid_lock` + 关机 HPD 保护）、`spx-tcs3430` 环境光传感器关机 `-ESHUTDOWN` 保护及 `spi-hid` 短休眠后触控自愈重试。

---

## v0.9.3 (2026-11-24)

- **GNSS / GPS XTRA3 PSDS 注入与 5 星座定位收敛修复**：新增 QMI LOC `0x00A7` XTRA3 PSDS 分片注入（`xtra3grcej.bin`，结合 `0x0038` UTC 注入与 `0x0037` 168 小时有效性校验）及 `/system/etc/gps_debug.conf` 高通 XTRA3 服务器挂载；补全 5 星座映射（GPS、GLONASS、Galileo、北斗 `system=6`、QZSS `system=7` 及 SBAS/NavIC）与 6 种信号类型，按 `(constellation, svid)` 准确追踪 `USED_IN_FIX`，并将 `IN_PROGRESS` 水平误差门槛收紧至 `<= 500 m` 且 `>= 4` 颗卫星。
- **下口 USB-C 自动角色切换与外接 DP `s2idle` 唤醒修复**：下口 USB-C 默认改为 `auto` 自动模式，新增事件驱动 `spx-lower-usb-watch`、**高级设置** 模式切换（自动 / 主机 / ADB）以及 `spx-typec` 角色串行化、`-ETIMEDOUT` 重试与 KMS 前 QMP mux 同步；通过 `patches/spx-dp-s2idle.patch` 保持链路/像素时钟并延迟 HPD 重训修复外接 DP 休眠唤醒卡顿；补全 `SurfaceDisplaySettings` 中英本地化，并将构建脚本默认并行度调整为 `max(1, nproc - 1)`。
- **SC8180X `s2idle` 防卡死与调度/中断性能优化**：合入 `0013`–`0016` 内核补丁（`mm0` 保活、GCC RPM、UCSI 休眠取消恢复队列、SPMI PDC 唤醒设备树补丁）；启用 `CONFIG_PREEMPT`、`CONFIG_NO_HZ_IDLE`、`CONFIG_HZ_250`、`CONFIG_UCLAMP_TASK`、`CONFIG_CPU_IDLE_GOV_TEO` 与 `CONFIG_QCOM_ICC_BWMON`；新增 `spx-irq-balance-boot` 将高频 GICv3 外设中断均衡分散至小核 `cpu1..cpu3`，并增强 `device_faker` LKM 跨内核 `vermagic` ABI 自动重载。
- **850nm 红外人脸实时预览录入 UI 与多方向解锁**：新增 Settings RRO `face_enrolling` 布局（`org.surface.face.overlay` v2），支持实时红外预览环、双语提示及保存 v3 模板前静默复验；新增基于重力传感器的 `SpxOrientationMonitor` 与多方向回退匹配（单次录入支持横屏 `0°`、音量键朝上竖屏 `270°` 与电源键朝上竖屏 `90°` 解锁），并升级侧边偏航与定向补光下的 NIR 检测、活体与对齐逻辑。
- **AVF 虚拟机宿主 VPN 共享与 OTA 保留数据 APK 缓存刷新**：新增空闲零开销 `NETLINK_ROUTE` 监听器（`spx-avf-vpn-watch`）与通用路由同步脚本（`spx-avf-vpn-reconcile`，支持 BusyBox awk DNS 解析、清理残留 `tc` 卸载并提升回包路由优先级），在 **高级设置** 中按需开关（默认关闭）；消除 `/vendor/app` 与 `/vendor/overlay` 打包时的 `1970-01-01` 时间戳，并在在线/离线保留数据 OTA 后自动清理 `/data/system/package_cache` 与过期 vendor `dalvik-cache`。
- **双语文档完善与仓库精简**：补全 `docs/` 下全部文档的 EN / ZH-CN 双语版本、修正 README 链接，并移除归档的 `reference/` 二进制文件。

---

## v0.9.0 (2026-11-21)

- **OV7251 850nm 红外人脸解锁（Class 2）与霍尔合盖休眠（`SW_LID`）**：新增 `IFace` AIDL V4 HAL（`android.hardware.biometrics.face-service.default`）及红外预览浮层（`spx-face-overlay.apk`），支持正脸录入与锁屏快速匹配；集成 TLMM GPIO 121 硬件霍尔 `SW_LID`（标记为 `Lid Switch` 适配 `touchpad-inhibitor`），联动 Type Cover 状态控制合盖休眠。
- **系统相册视频播放与硬件编码**：修复 `MediaCodec.setOutputSurface()` 因 `libgui.so` 中 `Surface::disconnect()` 清零 `mGenerationNumber` 引发的 `attachBuffer` 失败；合入 Codec2 AIDL V2 Venus 硬件编码服务、Venus OUTPUT 裁剪/NV12 对齐修复及 Gralloc5 `0x10303` 编码 Buffer 用途支持。
- **Wi-Fi 热点（SoftAP）**：实现 `nl80211` SoftAP、STA/AP 并发及 Android 17 Wi-Fi HAL 函数表 ABI。
- **GNSS / GPS（`IGnss` AIDL V4 + `SpxGnssPsds`）**：实现按需通过 `AF_QIPCRTR` 连接的 QMI LOC V4 HAL（空闲零轮询，不影响 `s2idle`），启动定位时自动清除 MPSS MI 引擎锁并过滤粗糙 CELLID 占位结果。
- **摄像头 / ISP / 传感器 / SLPI 加固**：整合 OV7251 850nm 补光、OV13858 翻转与 CAMSS 多 IFE、隐私指示灯计数、`spx-isp` 色温插值 3×3 CCM，以及 `mapper.quest2.so` 双目标同步安装修复；新增 `spx-tcs3430` 1x–64x 自动增益、SLPI LSM6DSO 陀螺仪 / AK0991x 磁力计支持，并通过 `DISABLE_REQ` 停流与复用空闲 QRTR socket 修复 SLPI 崩溃。
- **背光、显示、输入与系统卫生**：改用 0..511 线性 9-bit LPG PWM 表及 `0.006` 最低亮度下限，消除亮度条 21% 以下黑屏；合入 DPU CRTC 待决帧竞态修复、`spi-hid` 冷启鲁棒性、Slim Pen 2（MPP2）阈值、移除供体 OEM Dell/SupportAssist 残留应用及高频日志噪音收敛；内置硬盘安装关机改走标准 `zygote` 关机链路。
- **脚本目录整理**：将刷机、双启动与 PC ADB 升级等用户脚本迁至 [`tools/`](../tools/)，并移除 [`scripts/`](../scripts/) 下 209 个无引用遗留脚本。

---

## v0.8.0 (2026-11-20)

- **USB LiveCD -> 内置 NVMe 安装器**：支持从外置 5 分区 USB 桌面通过 **高级设置 -> 写入内置硬盘** 安装到内置 NVMe SSD（含 SHA-256 校验与独立 UEFI 引导）。
- **完整版高级设置**：新增 **触控**（停用触控、防误触开关、校准导入导出、无感迁移）、**调试**（LAN/VM IP 显示、网络 ADB `5555` + 下口桥接、initramfs Dropbear SSH `22` 及 `ssh-copy-id` `/root/.ssh` 持久化）、**系统更新** 与 **写入内置硬盘**。
- **设备端 OTA 与 PC ADB 一键升级**：新增 `SystemUpdateActivity`、`/bin/spx-ota-tool` 与双端升级脚本，默认保留 `/data`、校准与双系统 `grub.cfg`。
- **GRUB 救援模式、KernelSU 与 Type-C DP 4K@60Hz `s2idle`**：救援模式内置端口 `22` SSH、USB 有线网卡与 `/bin/spx-wifi-tui`；更新 KernelSU-Next `v3.4.2` + ZygiskNext `1.5.3` 与 `poipu` 标识；支持 DP 1.4 HBR3 `4K@60Hz` 与多次 `s2idle` 休眠唤醒恢复。

---

## v0.7.5 (2026-11-19)

- **高级设置（初版）**：Material You 风格应用支持系统/分应用 Vulkan 路由（保护系统与 `systemui` 留在 Mesa）及动态枚举/持久化 CPU 调度策略。
- **精简 5 分区布局与 Windows 同盘双启动**：整盘压缩至 8 GiB 5 分区 GPT（首次启动自动扩容 `userdata`），新增 Windows 同盘双启动脚本。
- **全应用 `VM_BIND` 与多媒体/引导修复**：面向全普通应用 UID 默认开启 QCOM `VM_BIND` + 缓存提交；修复 OMX 低延迟控制、48 kHz 扬声器缓冲节奏及通用便携启动/触控向导。

---

## v0.7.0 (2026-11-17)

- 基于 Android 17 `mica 16471258` + Linux `6.18.60` 构建 SQ1 / SQ2 统一 Hybrid 预览版，默认集成双 Type-C DP 输出、原生多屏设置磁贴与下口有线 USB ADB。
