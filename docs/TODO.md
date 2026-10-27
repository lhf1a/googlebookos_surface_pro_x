# SQ2 适配待办

## P1：3DMark Wild Life 花屏（2026-10-28）

- 用户报告 Wild Life 可运行完成并生成结果，但测试画面花屏。完成跑分不能作为 Vulkan 渲染正确的证据。
- 当前应用 2.7.5093 / versionCode 5093，ARM64；Turnip 26.2.4，SC8180X/A680.1，GPU 上限 661 MHz。
- 已采得异常帧：3D 场景大面积白/粉色横纹，而 FPS/UI 正常。661 MHz 与原 514 MHz 上限均复现；暂无 GPU fault/hang 证据，降频未修复。
- `debug.mesa.tu.debug=startup,noubwc` 最初仅在独立原生探针中确认生效。后续用明确限定 A680.1 的禁压缩二进制、核对实际映射后仍花屏。
- 绕过 GMEM 的固定配置在 514/661 MHz 下采样正常，用户确认固定 661 MHz 轮次画面正常；已实现只匹配 A680.1 + 3DMark 的 sysmem 兼容补丁，在线 661 MHz 完整一轮采样正常，5279 分 / 31.61 FPS。一次性重启自动加载和原生计算/图像/AHB 通过；重启后完整 Wild Life 为 5264 分 / 31.52 FPS，前中后段采样正常。同一镜像已设为默认，花屏阻塞通过限定范围的兼容绕过缓解，根因未关闭。
- GMEM 根因、其他应用兼容性、带宽/功耗影响仍需处理。详细对照与补丁边界见 [WILD-LIFE.md](WILD-LIFE.md)。
- 保持当前桌面 GLES、触摸、CPU/GPU 温控及音频。上游 [A680 macrotile 修正](https://www.mail-archive.com/dri-devel@lists.freedesktop.org/msg504111.html) 明确需要配套 UBWC 参数查询接口，不能孤立复制寄存器值并宣称修复。
- 简单 Vulkan 计算、图像/AHB 读回和彩色三角形通过，只覆盖有限路径，不能替代 Wild Life 回归。

## 方向传感器（按用户要求暂缓）

- 内核没有 IIO 设备；Android sensorservice 只有 Dynamic Sensor Manager，没有加速度计/陀螺仪。
- ADSP/CDSP/MPSS 已启动，但没有 SLPI remoteproc；SC8180X 设备树/remoteproc 驱动仍缺对应实例。
- 上游记录指向 SCSS/SLPI、qcslpi8180.mbn；现有固件只有 SC8280XP 的 qcslpi8280.mbn，不能混用。
- TODO：取得本机匹配的 Windows 固件和 ACPI/资源信息，补齐 SLPI 保留内存、时钟/中断/电源及 QMI/FastRPC 通道，之后实现 Android sensors HAL 并验证实际旋转/坐标轴。
- 不直接恢复会无限等待的 mica QSH 插件，不伪造方向事件。
- 参考：[linux-surface #38](https://github.com/linux-surface/surface-pro-x/issues/38)。

## 前后摄像头（按用户要求暂缓）

- 当前没有 /dev/video*、/dev/media*、V4L2 subdev；Android camera service 枚举为 0。
- 现有 provider 为 USB camera，不能替代内置 CSI 摄像头驱动。
- TODO：从本机硬件/Windows 信息确认两颗图像传感器、CCI/I2C 地址、供电与 GPIO；适配 SC8180X CAMSS/CSI/ISP 及传感器驱动，再接 Camera HAL、方向/色彩/曝光并分别验证预览和拍照。
- 参考：[linux-surface #23](https://github.com/linux-surface/surface-pro-x/issues/23)。
