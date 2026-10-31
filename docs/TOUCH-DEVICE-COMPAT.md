# Surface Pro X 触控构建适配

## SQ2 热图处理开销优化（2026-10-30，未部署）

`patch-iptsd-sq2-heatmap.py` 不再每帧复制 46×68 个 double（25024 字节）。
改为保存四个候选结果，全部读取完成后再写回，避免相邻修复互相影响；
低于 239/255 的候选跳过邻域采集和中位数计算。设备/尺寸限定、阈值、
四点位置及原始邻域语义不变，未增加跨帧状态或调整检测/稳定阈值。

连续性补丁本身不等待额外帧：首个稳定接触立即上报，消失/无效立即释放；
不稳定样本保留上一稳定位置，这可能表现为短暂位置停顿，但本轮不放宽其判定。
没有依据将可感知延迟全部归因于四点热图修复。

在新解包 IPTSd 3.1.1 上应用补丁，AArch64 静态编译及 QEMU 测试通过：
原边缘/角落/设备与尺寸保护 fixture、2000 组随机与边界热图对旧算法逐点精确
比较，以及完整触控连续性测试。此为源码行为回归，不是实机延迟或防误触验证。
未构建/部署新的完整守护进程或启动镜像；实际输入到显示延迟及长时间空闲误触
仍需在目标设备确认其型号、正在运行二进制/配置后测量。

同日设备 192.168.8.194 只读确认 045E:0C21、guard Y、IPTSd PID6608，
运行二进制 SHA256 e4610813bd86d7506385894db3cf07c61d399cdeec788224d0b9bfa058aae9a3，
配置仍为默认检测/稳定阈值及 AspectMax=2.0。独立静态 AArch64 微基准在
/tmp 运行（15秒 timeout，每轮10万次、两种输入各三对新旧算法）：旧版首轮
1.161微秒，后续约0.615–0.623微秒/帧；新版无尖峰约0.021微秒、四点尖峰
约0.105微秒。仅为热缓存合成输入的局部 CPU 开销，未固定频率/绑核，
不包含真实采样、拟合、输入分发或显示，不是端到端延迟。节省约半微秒不足以
解释可感知迟滞；没有因此替换正在运行的服务。前后 boot ID
fcda7cff-dc7e-4552-84c8-12a751b79595 和 PID 均不变。

用户随后明确症状为首次点击慢。只读检查：Android 无障碍总开关和触摸探索
关闭、DispatchEnabled=true/DispatchFrozen=false；SPI/HID runtime_status
均为 unsupported（不能仅凭 control=auto 认定休眠唤醒延迟）。上游稳定器对
没有历史的新 contact 直接置 stable=true，没有两帧确认；不能据此排除检测前延迟。

用户配合点击采集：初始30秒窗口在开始提示前结束，无点击；SSH一次连接超时
后重连，延长的50秒 event8 被动窗口明确 READY 后提示用户点击。该窗口取得
11组 DOWN/UP 和11组 tracking start/end，每组 BTN_TOUCH 与对应 tracking
边沿时间戳完全相同，按下至释放19.628–92.796毫秒（这是接触时长，不是响应
延迟）。boot未变，未替换服务或调整阈值。用户随后确认实际多点了一次，
11组与11次手动点击一致，本轮没有额外触点证据。原始日志及analysis.json
留 out/touch-latency/taps-20261030T125638Z。

本次 getevent 只覆盖 IPTSd 输出，不提供物理落指/原始热图或画面呈现时间。
因此只能确认 BTN_TOUCH 没有落后于同一输出帧的 MT 接触，不能排除检测/拟合
或 Android/应用显示阶段的迟滞，也不能证明端到端延迟正常。

2026-10-30：新镜像在设备 192.168.5.79 启动后，SPI HID 为
045E:0996。旧 touch-services 仅接受 045E:0C21，退出 2，导致 IPTSd
未启动；不是关闭幽灵触控补丁导致。

已在该设备临时验证：启动匹配增加 0996，输入节点刷新同步接受其虚拟
Touchscreen/Stylus。IPTSd 成功运行；Android EventHub/InputReader 枚举
并启用 event20 触摸屏，用户确认触控正常。未重启、未改启动盘。
原脚本 BASELINE 退出2；恢复原字节的 ROLLBACK 副本退出2；修改版
MODIFIED 退出0，进入 Touchscreen 模式。完整记录留在忽略的 out/touch-0996。

现在源码支持两种设备，且 build-source-boot 显式包含四个触控助手，
不再仅依赖历史底包中已有文件。此适配是基本启动支持，与幽灵触控开关
独立；--ghost-touch-fix off 继续关闭连续性补丁及四格热图补丁。
未扩大热图补丁原有的 045E:0C21/46x68 适用范围。

回归：python3 tests/test_surface_touch_device_ids.py。
随后用户授权写入 SQ1 启动镜像：pack-surface-touch-ids.py 基于实机
82979058…镜像，仅追加两个助手的变更，完整 last-entry-wins 审计通过，
IPTSd 仍为无幽灵补丁的5c0fd552…，密码SSH addon仍最后。
新镜像SHA256为c5c64cda4095f081ce3ff42eaa3e2b4a3aba03ceae2f93364b5e8639b0a04769。
核对Toshiba128GB NVMe、ESP/data UUID、marker、系统盘来源及guardY后，
备份旧文件至ESP同目录的native-combined-initramfs.cpio.gz.before-0996-20261030，
gzip/hash检查、sync后在同目录重命名替换。新旧文件哈希均匹配，
Image/DTB/GRUB不变，boot ID不变，未主动重启或格式化。
下次自然重启自动启动触控及关屏后恢复仍需验证。
