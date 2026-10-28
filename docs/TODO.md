# SQ2 适配待办

## 当前：同版 Quest 2 闭源图形栈接入

- 当前实际阻断：combined-v3已通过严格FP16布局/采样/写入150轮及RGBA8/Vulkan回归；真实SF第二轮仍黑屏或卡住，用户手动恢复后Mesa正常，pstore/SF tombstone缺失。先增加有限持久化kernel/SF证据并定位挂起，不原样重试全局切换。独立FP16通过不能替代桌面验证；私有ABI隔离和候选边界见 [QUEST2-FP16.md](QUEST2-FP16.md)。
- 已完成GLES pending历史fence淘汰候选的150轮异步合成与全屏回归，见QUEST2-SYSTEM-GRAPHICS-PLAN第7节。接下来以Vulkan v4九库为公共底座，加入fence-prune的五个GLES专属库；先验证同进程EGL→Vulkan、Vulkan→EGL两种加载顺序和真实绑定，再接SurfaceFlinger。独立两进程先后运行不能证明公共库全局状态兼容。
- v4 raw WSI 的完整节点/租约适配已通过64×64 150周期/1800帧及四尺寸至2880×1920各36帧；引用/字节归零、原AHB回归通过。真实桌面、deferred bind实机、AHB共享/采样/导出和其他格式仍待做，见 [QUEST2-VULKAN-WSI.md](QUEST2-VULKAN-WSI.md)。后文v3“待接raw交换链”为此前阶段记录。
- 完整vendor参考已提取，原18库与新增70项显示组件哈希已核对；来源与ABI差异见 [QUEST2-VENDOR-REFERENCE.md](QUEST2-VENDOR-REFERENCE.md)。尚缺同版system/odm内必要依赖的按需提取和完整内核接口审计，不跨版本随意补库。
- 原配GLES通过系统加载、RGBA窗口/YUV线性路径和有限可见SF合成；后者是闭源producer + Mesa SF，并非桌面整套替换。
- GLES已修复本轮精确读回矩阵：CPU4/bank15匹配实际布局、PBO优化回退、零面积校验修正后的私有`gles-linked-zero-area`候选，522项读回/错误检查全部通过；上传与采样各10项、YUV两种路径各3帧通过。继续覆盖RGBX/其他格式、实际视频与SF自身，不把RGBA矩阵当成整个系统通过。PBO同步回退的性能影响未量化，零阈值方案已否定。见 [QUEST2-READBACK-REFERENCE.md](QUEST2-READBACK-REFERENCE.md)。
- 同版Vulkan的QTI handle视图/事务/owner生命周期已接入私有system loader，v3候选属性/import各150轮、RGBA非均匀网格64×64连续150轮及四种尺寸至2880×1920各3轮通过。下一步覆盖AHB采样、共享/零应用引用/导出，接raw native-buffer和交换链；batch bind失败不能整体释放已导入视图。HAL close=NULL已正确缓存，不必绕过GDPA。见 [QUEST2-V77-VULKAN.md](QUEST2-V77-VULKAN.md) 与 [生命周期审计](QUEST2-VULKAN-AHB-LIFETIME.md)。
- 当前SF缓存使用GLES/非protected RGBA输出；优先补两context/native fence与多层premultiplied-alpha合成，含系统libGLESv1_CM导出入口。随后扩RGBX/BGRA与YUV DATASPACE，再接真实SF。具体接口及离线证据见 [QUEST2-SYSTEM-GRAPHICS-PLAN.md](QUEST2-SYSTEM-GRAPHICS-PLAN.md)。
- 后续覆盖RGBX等合成格式、真实视频及SF自身加载、持久化与重启。保留正常Mesa入口和所有已验证输入/音频/存储/温控/关屏保护。

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
