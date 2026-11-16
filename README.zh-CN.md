# Surface Pro X Googlebook OS

基于 mica **16471258（Android 17）** 与 Linux **6.18.60-sq2-android1** 的 Surface Pro X
**SQ1 / SQ2**（骁龙 8cx Gen 1/2）Android 移植，不是 ChromeOS Flex。

唯一默认整盘构建为 **hybrid**，SQ1/SQ2 共用：Mesa GLES/Turnip + QTI gralloc/mapper5/HWC + 原生 OMX。

[English](README.md) · [构建](docs/BUILD.md) · [依赖准备](docs/DEPENDENCIES.md) ·
[当前镜像与首启策略](docs/IMAGE.md) · [通用交付规则](docs/DEVELOPMENT.md)

## 构建

```sh
python3 scripts/build.py --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT
```

在 SQ1 或 SQ2 原 Windows 上运行 `scripts/Prepare-HybridDependencies.ps1`，统一下载固定上游输入、
提取共用 OEM 资料并打包，再把 tar 与校验文件复制到 Linux 构建主机。公开下载与 Git 版本均已固定；
依赖包目前仍不能单独生成全部 Android/QTI runtime/模板，缺口见依赖文档。

已完成组件可用 `--reuse-components` 复用；`rebuild-hybrid-venus.py` 核对匹配 KOUT 后只重编 Venus。
完整输入、顺序、产物与复用规则见 [STANDARD-BUILD](docs/STANDARD-BUILD.md)。

## 状态

更新于 2026-11-16。✅ 可用 · ⭕️ 部分可用/证据有限 · ❌ 不可用

| 领域 | 状态 | 说明 |
| --- | :---: | --- |
| NVMe 启动与存储 | ✅ | ARM64 UEFI / GRUB，稀疏整盘镜像 |
| Android 桌面与 GLES | ⭕️ | 桌面可用，图形兼容性仍有限 |
| 前后摄像头 | ⭕️ | V4L2 + 软件 ISP + Camera2，原生 JPEG、H.264/AAC；IR/Hello 与 AF 待做 |
| 触摸 | ⭕️ | 共用校准向导；学习配置按设备绑定 |
| 自动亮度/旋转 | ⭕️ | SQ2 已验证，SQ1 待验收 |
| 休眠（s2idle） | ⭕️ | SQ2 修复验证过一次；仍有定时唤醒，长时功耗未测 |
| 音频（扬声器/麦克风/USB） | ✅ | SQ2 已验收 |
| 视频硬解（Venus） | ⭕️ | H.264/HEVC + QTI OMX；未启用 HDR |
| LTE | ⭕️ | SQ1/SQ2 数据已打通；双机全新镜像验收待做 |
| USB 网络/存储 | ⭕️ | 仅验证过的适配器 |
| Wi-Fi / 蓝牙 / SSH | ⭕️ | Wi-Fi 由 Android 管理；BT HID 有限；SSH 默认仅密钥 |
| Root / KernelSU | ✅ | KSU Next + Zygisk，实验选项仅限本机 |

## 文档

构建与交付：

- [BUILD](docs/BUILD.md) —— 当前构建入口、输入与产物。
- [STANDARD-BUILD](docs/STANDARD-BUILD.md) —— 完整流程、输入状态与组件复用。
- [DEPENDENCIES](docs/DEPENDENCIES.md) —— Windows 依赖采集与离线交接。
- [IMAGE](docs/IMAGE.md) —— 首启策略、触摸向导、当前镜像校验值。
- [SUSPEND](docs/SUSPEND.md) —— s2idle 修复及其验证边界。
- [DEVELOPMENT](docs/DEVELOPMENT.md) —— 任意 SQ1/SQ2 通用交付规则。
- [TODO](docs/TODO.md) —— 仅列未完成事项。
- [PROVENANCE](docs/PROVENANCE.md) —— 固定运行库来源与许可。

功能：[摄像头](docs/CAMERA-BUILD.md)、[传感器](docs/SENSORS.md)、
[触摸校准](docs/TOUCH-CALIBRATION.md)、[触控适配](docs/TOUCH-DEVICE-COMPAT.md)、
[LTE](docs/LTE-PRODUCT.md)、[扬声器](docs/SPEAKER-LOUDNESS.md)、[麦克风](docs/MICROPHONE.md)、
[视频](docs/VIDEO-CAPABILITY-ALIGNMENT.md)、[USB](docs/USB-FUNCTIONS.md)、
[USB-NVMe 安装](docs/USB-NVME-DEPLOY.md)、[蓝牙输入](docs/BLUETOOTH-INPUT.md)、
[Wild Life 规避](docs/WILD-LIFE.md)、[Root](docs/ROOT-MANAGEMENT.md)、
[电源策略](docs/UNRESTRICTED-POWER.md)、[统一 SQ1/SQ2](docs/UNIFIED-SQ1-SQ2.md)、
[默认密度](docs/DEFAULT-DISPLAY-DENSITY.md)、[4 KB 页面通知](docs/PAGE-MODE-NOTIFICATION.md)。

## 范围与限制

私有固件、运行库、凭据、镜像和原始日志不入 Git；明确审核的外部内核源码/DT 输入见
[归档说明](source-dependencies/kernel-6.18/README.md)。原组件版权/许可保持。
构建成功不代表真机启动、跨机验收、长期休眠或功耗已通过，未验证项见 [TODO](docs/TODO.md)。