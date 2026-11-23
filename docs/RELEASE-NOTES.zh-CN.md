# 更新日志

预编译磁盘镜像（`*.img.gz`）、ESP OTA 升级包（`*-ota-esp-*.tar.gz`）与 `SHA256SUMS` 发布于 [GitHub Releases](https://github.com/lhf1a/googlebookos_surface_pro_x/releases)、[Google Drive](https://drive.google.com/drive/folders/1uVAA6UnydSyUHQAO8Y5rTm8SxZ62Zykr) 与 [百度网盘](https://pan.baidu.com/s/51GjLdmTGir0PelomZvboYw)。

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
