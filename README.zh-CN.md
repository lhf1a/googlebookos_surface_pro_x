# Surface Pro X SQ2 上的 Googlebook OS

[English](README.md) | **简体中文**

面向 **Surface Pro X SQ2 / Snapdragon 8cx Gen 2** 的 Android 系 Googlebook OS 移植。基于 mica **16471258（Android 17）** 和 Linux **6.3.8-sq2-android1**，**不是 ChromeOS Flex**。

## 最新进展 — 2026-11-02

- 主线提交 `fee67b2` 已将 USB 网卡和 Android USB 输入热插拔默认纳入双启动构建。USB 载荷与实机重启验收包一致，30 项 USB 测试通过；不代表实测了 46 种网卡，也不代表完成了新的全套 EFI 重编译，后者仍需固定私有输入缓存。
- 正常 EL2 保留显式指定的已有无密码用户登录，但开机**不自动打开终端、不运行 KVM 见证探针**；仍可手动打开终端。
- LTE 实验环境已绑定 IPA、出现数据接口，并接受本机自生成 NV/量产 MCFG；QMI EFS 元数据可读，但文件读取被拒绝、基带仍处于 factory-test，**尚无蜂窝注册和数据服务**。实验载荷未提升为正常启动。

## 功能状态

更新于 **2026-11-02** · ✅ 可用 · ⭕️ 部分可用 · ❌ 不可用或未确认

| 功能 | 状态 | 说明 |
| --- | :---: | --- |
| NVMe 启动与持久化存储 | ✅ | ARM64 UEFI / GRUB |
| Android 桌面与 GLES | ⭕️ | 桌面可用，图形兼容性仍有限 |
| 共享存储与下载 | ✅ | Files、Chrome 下载 |
| USB 存储 | ✅ | FAT32 便携存储；不保证所有文件系统和设备 |
| USB 音频 | ✅ | USB-C EarPods 播放、麦克风与自动路由 |
| USB 有线网卡 | ⭕️ | AX88179A 经 CDC NCM 驱动，Android DHCP、联网验证及重启通过；新增 46 个驱动及依赖，其他型号未实测；不含通用 USB Wi-Fi HAL 支持 |
| USB 鼠标输入 | ✅ | 罗技 C539 移动、按键、滚轮经用户确认；重启后自动接入 Android 输入服务 |
| Wi-Fi | ✅ | Android 管理已保存网络 |
| SSH | ✅ | Android 启动后自动提供公钥访问 |
| 蓝牙 | ⭕️ | 配对与开关状态保留；HID 兼容性仍有限 |
| 触摸屏 | ✅ | 多点触控与边缘幽灵触控修复 |
| 键盘盖与触控板 | ✅ | 支持热插拔恢复 |
| 内置扬声器 | ✅ | EL2 停播爆音已修复，播放、间隔10秒重开及原应用经用户确认；默认 RX 0 dB |
| 内置麦克风 | ✅ | 双声道/单声道录音与回放 |
| 硬件视频解码 | ⭕️ | 当前 EL2 已验证 Android H.264 1080p；HEVC / VP8 / VP9 结果来自 EL1 |
| Linux 终端 | ⭕️ | 手动打开；EL2 Debian shell 与 HTTPS 已验证；guest 内部硬解未验证 |
| HDR 视频 | ❌ | 尚不具备完整普通应用支持 |
| Vulkan 与游戏 | ⭕️ | 默认高通 Vulkan，Mesa 回退；存在应用兼容限制 |
| 高通闭源图形 | ⭕️ | 部分路径可用，全系统兼容性尚未完成 |
| 内屏显示 | ⭕️ | 当前默认原生 60 Hz；特定设备的约 65 Hz 档位仍为实验功能 |
| 外接 USB-C DisplayPort | ❌ | DP / AltMode 适配未完成 |
| CPU / GPU 调频 | ✅ | CPU 最高 1.7664 / 3.1488 GHz，SQ2 GPU 默认 700 MHz（RPMh432） |
| 温控 | ✅ | CPU / GPU 温度监控与被动降频 |
| 电池与资源监控 | ⭕️ | 容量、电流、SOH 可用；容量为估算，无电池温度 |
| RTC | ✅ | 系统时钟支持 |
| 电源键与合盖关屏 | ✅ | 仅关屏，系统继续运行 |
| 睡眠 / 休眠 | ❌ | 已禁用，真正 suspend/resume 尚未解决 |
| 方向传感器 | ❌ | 不可用 |
| 内置摄像头 | ❌ | 不可用 |
| LTE | ❌ | 实验 IPA 接口及量产 MCFG 已就绪；factory-test/EFS 读取问题未解决，尚无蜂窝服务 |

上述状态针对文档中的 SQ2 配置，不代表所有外设或应用均兼容。长期稳定性与续航仍待完善。后续一次启动曾短暂无声，用户在未做音频改动前确认恢复；记录中的 SoundWire 传输错误根因仍未定位，与已验收的停播爆音修复分开记录。

## 构建与文档

当前主线 SQ2 默认构建为 **EL2、高通 Vulkan、原生 60 Hz、GPU700 MHz / RPMh432**，恰好保留新默认和可工作的 Mesa 回退两项；已纳入 Linux 终端、EL2 H.264 硬解、ADSP 接管音频/停播爆音修复、USB 网卡模块及 Android USB 输入恢复。旧 EL1 安装构建须显式使用 `--legacy-el1`；固定私有输入和设备限制见 BUILD。

本文记录的 LTE 研究快照基于 `73ea6ee`，尚未包含 `fee67b2` 的 USB 默认构建集成；不要用其旧启动包覆盖当前已验收设备配置。

- [构建部署](docs/BUILD.md) · [最新交接](docs/HANDOFF.md) · [待办](docs/TODO.md)
- [USB 存储/音频](docs/USB-FUNCTIONS.md) · [蓝牙输入](docs/BLUETOOTH-INPUT.md) · [麦克风](docs/MICROPHONE.md)
- [LTE 开发](docs/LTE.md) · [LTE Windows 原版参考](docs/LTE-WINDOWS-REFERENCE.md)
- [视频解码](docs/VIDEO-DECODE.md) · [视频能力限制](docs/VIDEO-CAPABILITY-ALIGNMENT.md)
- [图形兼容性](docs/WILD-LIFE.md) · [闭源图形](docs/QUEST2-GPU-RESEARCH.md) · [显示档位](docs/DISPLAY-REFRESH.md)
- [来源与发布边界](docs/PROVENANCE.md) · [贡献者操作约定](AGENTS.md)

详细工程文档目前主要为中文。仓库保存适配源码、配置与构建工具，**不是开箱即用镜像，也不能全新克隆后一键构建**。构建依赖固定版本的专有镜像、固件与本地输入，这些内容不随仓库发布。

## 安全提示

- 不要格式化 userdata 修复挂载，也不要将安装器盲用于另一块磁盘或设备。
- 恰好保留新默认和可工作的 Mesa 回退两项、两个载荷、共享 GRUB/固件及仅关屏保护；不要在此移植上热重启 `vold`。
- 通用构建档位与既有实机配置可能不同，替换启动文件前先阅读 BUILD。
- root/debug/permissive 实验配置不是生产安全方案，安全启动与硬件安全集成尚未完成。
- 密钥、凭据、专有二进制、整盘镜像和原始日志不进入 Git；各组件保留各自许可。
