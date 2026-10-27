# Googlebook OS on Surface Pro X SQ2

Surface Pro X（SQ2 / 8cx Gen 2、16 GB）上的实验性裸机移植。基于 mica Googlebook OS 恢复镜像 build **16471258**（Android 17）、linux-surface 衍生内核 **6.3.8-sq2-android1**、Freedreno GLES 及设备适配层。此系统运行 Android 框架，不是 ChromeOS Flex。

当前使用已替换的 **512 GB 内置 NVMe**：UEFI → ARM64 GRUB → 自定义 initramfs → 原 Android init。正常音频入口默认，等待 3 秒；另有独立救援和关机入口。原第 2 项无音频启动已删除。

## 当前进度（2026-10-27）

| 子系统 | 实测状态 |
| --- | --- |
| 桌面 / GLES | 已进入桌面；RGBA、YV12/多平面读回通过；视频触发的 SurfaceFlinger 导入崩溃已修复，经过有限时长观察 |
| 共享存储 | **已修复**：启动前加载 FUSE；用户 0/10 卷 MOUNTED；用户确认 Files 和 Chrome 下载正常 |
| Wi-Fi | 由 Android 管理已保存网络；正常启动取消内核阶段自动连接和 SSH，救援入口保留调试网络 |
| 蓝牙 | 已显示名称并配对；修复启动卡在开启中，ON/OFF 状态跨重启保留已验证；其他配置文件待验证 |
| 触摸 / 键盘 / 触控板 | 启动后重初始化触控；键盘盖热插拔自动恢复已实测；保留触摸状态修复及 AspectMax=2.0 |
| 扬声器 | Android 播放有声；结束约 10 秒后爆音、缺低频、声像偏右待优化 |
| 电池 / RTC | 已接入对应驱动，不代表完整电源管理已完成 |
| 合盖 / 电源键 | 仅关屏策略下，电源键与合盖/开盖恢复显示、触摸和键盘已实测；真正睡眠恢复未解决 |
| 休眠策略 | **仅关闭屏幕**：内核阻止系统 suspend，禁用 hibernation；真正休眠修复仍暂停，关屏期间系统继续运行 |
| LTE / Vulkan | 无 SIM，仅驱动探测；Turnip Vulkan 尚未验证 |

## 开发入口

- [交接说明](docs/HANDOFF.md)：补丁、证据边界、待办及现场检查。
- [构建与部署](docs/BUILD.md)：工具链、脚本关系和缓存依赖。
- [来源及发布范围](docs/PROVENANCE.md)：固定版本和未发布的输入。
- [Agent 工作约定](AGENTS.md)：后续开发的当前约束。

仓库保存补丁生成器、适配源码、配置和可复用诊断工具。**不是开箱即用安装镜像，也不是全新克隆一键构建的发行版**。脚本含早期 USB 实验路径及特定磁盘保护条件，运行前确认阶段和目标盘。

恢复镜像、固件、构建产物、SSH 密钥（含公钥）、Wi-Fi 凭据及原始测试日志均不发布。实验系统保留 root、调试内核和 permissive 配置；生产安全、硬件密钥及安全启动适配未完成。
