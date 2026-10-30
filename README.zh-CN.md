# Surface Pro X SQ2 上的 Googlebook OS

[English](README.md) | **简体中文**

面向 **Surface Pro X SQ2 / Snapdragon 8cx Gen 2** 的 Android 系 Googlebook OS 移植。基于 mica **16471258（Android 17）** 和 Linux **6.3.8-sq2-android1**，**不是 ChromeOS Flex**。

## 功能状态

更新于 **2026-10-30** · ✅ 可用 · ⭕️ 部分可用 · ❌ 不可用或未确认

| 功能 | 状态 | 说明 |
| --- | :---: | --- |
| NVMe 启动与持久化存储 | ✅ | ARM64 UEFI / GRUB |
| Android 桌面与 GLES | ⭕️ | 桌面可用，图形兼容性仍有限 |
| 共享存储与下载 | ✅ | Files、Chrome 下载 |
| USB 存储 | ✅ | FAT32 便携存储；不保证所有文件系统和设备 |
| USB 音频 | ✅ | USB-C EarPods 播放、麦克风与自动路由 |
| Wi-Fi | ✅ | Android 管理已保存网络 |
| SSH | ✅ | Android 启动后自动提供公钥访问 |
| 蓝牙 | ⭕️ | 配对与开关状态保留；HID 兼容性仍有限 |
| 触摸屏 | ✅ | 多点触控与边缘幽灵触控修复 |
| 键盘盖与触控板 | ✅ | 支持热插拔恢复 |
| 内置扬声器 | ⭕️ | 可以播放，结束爆音与音质仍需改善 |
| 内置麦克风 | ✅ | 双声道/单声道录音与回放 |
| 硬件视频解码 | ⭕️ | AVC / HEVC / VP8 / VP9 SDR；有分辨率与 profile 限制 |
| HDR 视频 | ❌ | 尚不具备完整普通应用支持 |
| Vulkan 与游戏 | ⭕️ | Turnip Vulkan 1.3；存在应用兼容限制 |
| 高通闭源图形 | ⭕️ | 部分路径可用，全系统兼容性尚未完成 |
| 内屏显示 | ⭕️ | 原生 60 Hz；可选特定设备的约 65 Hz 档位 |
| 外接 USB-C DisplayPort | ❌ | DP / AltMode 适配未完成 |
| CPU / GPU 调频 | ✅ | CPU 最高 1.7664 / 3.1488 GHz，GPU 最高 661 MHz |
| 温控 | ✅ | CPU / GPU 温度监控与被动降频 |
| 电池与资源监控 | ⭕️ | 容量、电流、SOH 可用；容量为估算，无电池温度 |
| RTC | ✅ | 系统时钟支持 |
| 电源键与合盖关屏 | ✅ | 仅关屏，系统继续运行 |
| 睡眠 / 休眠 | ❌ | 已禁用，真正 suspend/resume 尚未解决 |
| 方向传感器 | ❌ | 不可用 |
| 内置摄像头 | ❌ | 不可用 |
| LTE | ❌ | 可用性未确认 |

上述状态针对文档中的 SQ2 配置，不代表所有外设或应用均兼容。长期稳定性与续航仍待完善。

## 构建与文档

默认源码构建已包含 **硬件视频解码、USB 存储/音频和蓝牙输入修复**，保留独立正常/救援策略与可选显示档位。

- [构建部署](docs/BUILD.md) · [最新交接](docs/HANDOFF.md) · [待办](docs/TODO.md)
- [USB 存储/音频](docs/USB-FUNCTIONS.md) · [蓝牙输入](docs/BLUETOOTH-INPUT.md) · [麦克风](docs/MICROPHONE.md)
- [视频解码](docs/VIDEO-DECODE.md) · [视频能力限制](docs/VIDEO-CAPABILITY-ALIGNMENT.md)
- [图形兼容性](docs/WILD-LIFE.md) · [闭源图形](docs/QUEST2-GPU-RESEARCH.md) · [显示档位](docs/DISPLAY-REFRESH.md)
- [来源与发布边界](docs/PROVENANCE.md) · [贡献者操作约定](AGENTS.md)

详细工程文档目前主要为中文。仓库保存适配源码、配置与构建工具，**不是开箱即用镜像，也不能全新克隆后一键构建**。构建依赖固定版本的专有镜像、固件与本地输入，这些内容不随仓库发布。

## 安全提示

- 不要格式化 userdata 修复挂载，也不要将安装器盲用于另一块磁盘或设备。
- 保留正常/救援入口及仅关屏保护；不要在此移植上热重启 `vold`。
- 通用构建档位与既有实机配置可能不同，替换启动文件前先阅读 BUILD。
- root/debug/permissive 实验配置不是生产安全方案，安全启动与硬件安全集成尚未完成。
- 密钥、凭据、专有二进制、整盘镜像和原始日志不进入 Git；各组件保留各自许可。
