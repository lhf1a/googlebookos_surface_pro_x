# SQ2 Wild Life 花屏排查（2026-10-28）

3DMark 2.7.5093 / 5093，Android 用户 10，Mesa Turnip 26.2.4，芯片 ID 0x06080001（A680.1）。原有计算、图像/AHardwareBuffer 读回和彩色三角形通过，不代表复杂渲染正确。

## 实机对照

| 配置 | 观察 |
| --- | --- |
| 原驱动，661 MHz | 场景大面积白/粉色横纹，UI/FPS 仍正常，能完成跑分 |
| 原驱动，514 MHz | 同类花屏；降频没有修复 |
| A680.1 图像创建中明确禁用 UBWC，514 MHz | 核对驱动 inode 后仍花屏；不是仅依赖 Android 属性的测试 |
| 强制 sysmem，514 MHz | 前、中、后段采样画面正常，4311 分 / 25.82 FPS |
| 强制 sysmem，661 MHz | 前、中、后段采样画面正常，4920 分 / 29.46 FPS |
| 仅匹配 A680.1 + 3DMark 的兼容补丁，661 MHz | 实际驱动 hash/inode 已核对，前、中、后段采样正常，5279 分 / 31.61 FPS |
| 同一兼容补丁重启后，661 MHz | 驱动自动加载，前、中、后段采样正常，5264 分 / 31.52 FPS |

sysmem 指 Vulkan 仍由 GPU 渲染、绕过片上 GMEM 的分块渲染路径，不是 CPU 软件渲染。用户已对单独固定 sysmem 的 661 MHz 轮次确认“这轮画面正常”。此前在混合路径试验期间回复的“仍有异常”对应时段确实再次出现花屏；不能混淆这些轮次。

`debug.mesa.tu.debug=startup,noubwc` 最初只在独立原生探针中确认读取，未证实 3DMark 中生效。因此后续使用单独实验二进制，检查实际映射 inode/hash。强制 sysmem 驱动的日志确认应用内 `TU_DEBUG=0x9`。动态切换 flushall/nobin/sysmem 的混合轮仅用于定位，不能将其跑分当作性能结果；nolrz 对应截图还在加载，不能宣称已排除 LRZ。

## 有限范围兼容补丁

`scripts/patch-sq2-turnip-gmem.py` 对已知 Mesa 26.2.4 源文件作严格哈希校验。在 `use_sysmem_rendering` 中只匹配芯片 `0x06080001` 与进程名 `com.futuremark.dmandroid.application`，使用既有 sysmem 路径。其他应用、GLES、硬件频率及温控保持原逻辑。显式 `TU_DEBUG=gmem` 可供后续研发选择原路径；需要验证选项实际被目标进程读取。

这是 **3DMark/A680.1 兼容绕过措施**，不声称修复 GMEM 根因或通过 Vulkan 一致性认证；也不把较高但花屏的旧跑分视为有效性能基准。sysmem 可能增加带宽/功耗，长期影响尚未测量。在线完整一轮及普通计算/图像/AHB 探针通过。一次性重启后驱动自动加载、SHA256 一致，原生探针再次通过；麦克风 HAL、触摸哈希及 screen-only guard、GPU 上限仍正确，用户已解锁联网。

兼容驱动 SHA256：`0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c`。默认镜像：`fa64305d7400ea3c7d262e3649c90dc4627c74dc7fd6cb563d52adda9d505275`。测试期间没有新 GPU fault/hang 日志；没有日志不等于全面稳定性证明。

一次性启动后的完整 Wild Life 回归为 5264 分 / 31.52 FPS，约 10.0、33.4、58.4 秒采样场景正常。随后将同一已验证镜像提升为默认，恢复原正常/救援/关机菜单，没有再做第二次重启。Image、DTB 不变；原麦克风基线 9dd3ff7a… 保留在本机忽略目录中供回滚。屏幕关闭与锁定延迟均为 30 分钟，保留用户 PIN 和仅关屏保护。

`build-sq2-turnip.sh` 仅重建 Turnip，不覆盖已验证的 GLES。`pack-sq2-turnip-update.py` 从麦克风基线 9dd3ff7a… 只替换 Vulkan HAL，逐项审计其它内容不变，并将本地密码 SSH addon 保持最后。原始日志、截图、实验二进制和带凭据镜像均位于忽略的 out/wild-life。

## 待继续定位

- 制作能逐像素校验的 GMEM/MRT/input-attachment 最小复现；区分 tile 布局、load/store、LRZ、CCU 缓存及 render-pass 转换。
- 上游 [A680 UBWC/macrotile 修正](https://www.mail-archive.com/dri-devel@lists.freedesktop.org/msg504111.html) 要求配套内核 UBWC 查询接口，不能单独写 macrotile 寄存器。当前未回移该系列；禁用 UBWC 未消除本次异常。
- 对比更多 Vulkan 应用及 Wild Life Extreme；当前应用级 workaround 不代表这些路径已通过。
- 用户目视固定配置和重启回归已通过；长期温度/功耗与更多应用验证仍待进行，不自动扩大适用范围。
