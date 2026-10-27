# SQ2 开发交接

更新时间：2026-10-27。不包含原始日志、网络地址或凭据。

## 最近完成：共享存储

故障：Files、Chrome 下载及依赖 external_primary 的功能失败。/data ext4 已读写挂载，约 453 GiB 总容量；当前用户 10，用户 0/10 已解锁，但两个 emulated 卷均 UNMOUNTABLE。Settings 抛出 Volume external_primary not found。

根因：CONFIG_FUSE_FS=m，原启动流程未加载模块，缺少 /dev/fuse。运行中补载没有让失败卷自动恢复。修复在 probe/test2/init-googlebook-native 中于 Android init 前加载 FUSE 并检查设备节点，未格式化或重分区。

修复已写入当前内置 SSD，正常音频入口重启后两个卷均 MOUNTED。**用户确认系统“文件”打开下载/新建文件夹和 Chrome 下载均正常。** Android 接管后 /dev/fuse 为 root:root 0600，仍工作正常，不应据此盲目放宽权限。修复后 /data 约 450 GiB 可用。

现场 sm 独立 app_process 命令曾无输出并返回 0，不能据此判断挂载成功；以 dumpsys mount、实际 FUSE 挂载和应用访问为准。PID 1 的 /storage 绑定用户 0，从此命名空间直接访问用户 10 可能 Permission denied，不代表用户 10 应用故障。

## 当前待办

- 真正休眠修复暂停；用户改为要求“只关闭屏幕”，具体保护策略见下节。
- 音频结束约 10 秒后爆音、缺低频、声像偏右暂缓，见 configs/audio-known-issues.json。
- 蓝牙其他配置文件、LTE 有 SIM 联网、Turnip Vulkan、长期稳定性尚未完成验证；不要默认开始这些新测试。

## 关于设备信息

SettingsGoogle 的“型号”读取 Build.MODEL；CPU Info 由 DiagnosticsPrebuilt 的 HardwareInfoProvider 提供，其 DefaultCpuDataSource 读取 Build.SOC_MODEL。原镜像显示 XPS Googlebook，CPU 为 unknown。apply-device-info 在原 Android init 加载属性之前，为现有 build.prop 创建 RAM 文件绑定覆盖：型号 Surface Pro X、品牌/制造商 Microsoft、ro.soc.model=Microsoft SQ2。保留用户设置的设备名称、mica 产品代号、android-desktop 硬件分派、ABI、构建指纹及软件版本，不修改只读 EROFS 镜像。

内核版本页通过 Os.uname() 和 DeviceInfoUtils 正则解析；原构建日期“2026-10-27 00:00:00 UTC”缺少星期，解析失败。build-sq2-screen-only.sh 改为“Tue Oct 27 00:00:00 UTC 2026”，内核 release 保持 6.3.8-sq2-android1、配置及模块 ABI 不变。新 Image 已通过 QEMU 只关屏保护测试。实机正常重启后，设置页面截图确认 CPU 为 Microsoft SQ2（8 个线程）、型号 Surface Pro X，Android 版本子页显示 6.3.8-sq2-android1 和 #17 Tue Oct 27 00:00:00 UTC 2026。用户原有设备名称保留。

## 启动结构

- Surface Pro X SQ2、16 GB，已替换为实验用 512 GB NVMe；内核 6.3.8-sq2-android1，内置 NVMe 已启用。
- ESP 分区 6，userdata 分区 1；布局见 scripts/build-googlebook-internal-ssd.py。super 切片映射为只读 EROFS，适配经 RAM overlay 注入。
- ESP /EFI/BOOT/grub.cfg；内核/DTB/initramfs 在 /boot/sq2-android1/。菜单现在为正常音频、独立救援、关机，正常默认、3 秒；原无音频第 2 项已删除。
- 正常 initramfs 准备必要驱动、固件与 Android 挂载，不执行测试网络连接；用户随后要求保留自动 SSH，现由 ssh-framework-boot 等待 Android boot_completed 后后台启动 Dropbear。SSH 根目录仍是调试 initramfs。
- Android 命令：nsenter -t 1 -m -r -- /system/bin/<command>；Android 文件树 /proc/1/root；ESP /media/esp；本次日志 /logs。
- sq2.audio=1 启用音频。如需临时隔离音频，可在 GRUB 编辑正常入口去掉该参数，不再提供单独菜单项。

### 正常启动去除调试等待

正常启动的内核准备阶段不执行 network-up、start-ssh、keep-debug-route、wifi-framework-boot/rollback、memfd 自测、boot-kmsg 或 native-monitor。不再等待实验室 SSID 关联和 DHCP，也不强制打开 Wi-Fi 或覆盖已保存网络。helper 输出只写 initramfs RAM，避免同步写 ESP；必要的 QCOM QRTR/PD mapper/RMTFS 固件服务、ath10k、音频/输入/GPU/RTC/FUSE 等仍保留。触控早期初始化并行执行，boot_completed 后的触控恢复仍生效。

sq2.rescue=1 自动启用 debug，保留独立救援联网和 SSH；sq2.debug=1 可临时恢复完整 Android 调试启动。QEMU smoke 模式仍保留验证日志。用户随后要求保留自动 SSH：正常模式的 ssh-framework-boot 在 boot_completed 后启动公钥认证服务，使用 Android netd 自身路由。实机无需额外路由即可连接，故不维护诊断子网规则，也不打开 Wi-Fi、不关联 SSID、不运行 DHCP。sq2.ssh=0 禁用该助手。显式 debug 模式继续走旧调试路径，不重复启动 SSH 助手。最终正常入口重启后，已通过原公钥自动连接；启动参数没有 sq2.debug=1，且没有 network-up/monitor 日志。

此前禁用自动 SSH 的精简入口重启后，用户确认桌面、Wi-Fi、触摸和键盘正常；当时 TCP 22 不接受连接。未进行严格启动耗时基准测试，不宣称具体节省秒数。

## 图形与框架稳定性

scripts/patch-mesa-sq2-multiplane.py 修复 mapper5 辅助实例未导入 handle 导致元数据查询失败，以及单平面假设。RAII import/free，读取 FOURCC、modifier、plane layout，支持 1–4 平面并检查偏移/步长，返回原 handle 借用 FD，避免释放后 FD 失效。

此前 YV12 640×360 导入失败触发 SurfaceFlinger GaneshBackendTexture / SkiaRenderEngine::mapExternalTextureBuffer abort；后续 AudioService watchdog 为该轮级联现象，不应把全部重启归因于音频。新 RGBA、YV12/格式 35 GLES 读回通过。无音频视频观察 825 秒、音频开启观察 503 秒，框架 PID 未变化且用户确认有声；这是有限测试，不是长期保证。

保留 vendor.minigbm.debug=nocompression，解决早期文字/动画彩色损坏。Turnip 未验证。源码 probe/graphics/；集成 build-sq2-mesa.sh、build-googlebook-graphics-overlay.py、pack-sq2-stability.sh。

## 触摸及输入

IPTSd 3.1.1 加 scripts/patch-iptsd-sq2-touch.py 状态/连续性修复。configs/iptsd-sq2.conf 保留 Contacts AspectMax=2.0，触点检测与稳定性阈值默认；用户确认轻点、连续拖动、双指缩放正常且无自动点击。DFT ButtonMinMag=4000 是单独的笔配置。

输入节点发布时序/权限修复使 EventHub 接入设备。正常开机可用与休眠后恢复是独立结论，后者未解决。

只关屏内核首次启动后曾出现触摸完全无响应：SPI ready、resume_count=0、IPTSd 存活且 Android 已枚举设备，但连续操作采集无事件。保留同一二进制/配置重启 IPTSd 后用户确认恢复。新增 touch-framework-ready，在 boot_completed 后延迟 5 秒重新初始化一次；这是启动阶段的恢复措施，具体丢失原始触摸模式的原因尚未证实，不归因为真实系统休眠。

后续重启已执行 SQ2_TOUCH_FRAMEWORK_REINITIALIZED，用户确认触摸正常。键盘盖热插拔另有独立权限竞态：内核已创建 09AE/09AF/09B0 输入设备，但 EventHub 先于 ueventd 设置权限打开节点并报 Permission denied，此后没有重试。cover-input-watch 只匹配这些 SSAM 键盘盖节点，按 sysfs input 身份识别每次重新枚举，以 root:input 0660 权限重新发布 IN_CREATE；不能仅缓存 eventN，因为热插拔会复用编号。

用户再次拔插后确认键盘、触控板均能自动恢复。随后电源键关屏约 20 秒再亮屏、合盖等待后开盖，用户确认显示、触摸与键盘都正常。

## Wi-Fi 与蓝牙

Wi-Fi 使用内核接口及 Android 通用 HAL 适配：sq2-wifi-driver.c、sq2-wifi-hal.cpp。正常启动由 Android 自身恢复已保存网络；wifi-framework-boot 仅在显式 debug 启动用于调试连接及回退。保留实验室联网引导跳过逻辑。

蓝牙由 Linux hci_uart/btqca 初始化 WCN3998，sq2-hci-bridge 将真实 HCI_USER socket 桥接到 /dev/sq2-bt-h4，Cuttlefish AIDL HAL 接管。patch-sq2-bluetooth.py 包含关闭 serdev 防护、特定 baud ACK 处理及诊断；bluetooth-framework-boot 和 configs/sq2-bluetooth.rc 持久化。用户确认名称显示、配对成功；A2DP、通话等全流程未验证。不要在线反复卸载 QCA transport 模块。

后续启动曾在过早的 QCA 固件下载阶段出现异常回复，HCI_USER bind 返回 EBUSY，桥接退出且 Android 卡在 BLE_TURNING_ON。bluetooth-framework-boot 现等待 sys.boot_completed=1 后再延迟 8 秒启动控制器；桥接对 EBUSY/EAGAIN 最多等待 120 秒，其他错误仍失败。新流程只发布 HAL transport ready，不写 bluetooth_on，不调用 enable/disable；由 Android BluetoothManager 保存并恢复用户开关状态及飞行模式策略。开机加载控制器/注册 HAL 不等于替用户打开蓝牙开关。

已完成两次重启验证：保存值 1 时自动恢复 ON；保存值 0 时，控制器和 HAL 就绪后仍为 OFF，BluetoothManager 显示 never enabled。随后手动 enable 在约 0.85 秒转为 ON，恢复用户原来的开启状态；观察到的框架蓝牙崩溃数为 0。重复运行启动助手保持桥接 PID 与 OFF 保存值不变，不重新关闭已接管的 HCI 控制器。此验证覆盖开关/保存状态，不代表所有蓝牙配置文件已回归。

## 音频

ADSP → SLIM/WCD9340 → SoundWire/双 WSA881x。物理左右均有声；Android AIDL HAL 和 48 kHz S16 stereo policy 已接入。patch-sq2-audio-hal.py 去掉模拟器跳帧逻辑，校验原始/修改后哈希。audio-drivers-start、audio-speaker-route、audio-framework-boot 负责固件、路由、HAL。

调音问题仍开放。历史音频期间发生过整机重启；有限视频观察不能证明所有音频故障已解决。

## 合盖、电源与休眠（暂停）

### 现行策略：只关屏

用户要求禁用有问题的 s2idle，只保留显示关屏。scripts/patch-sq2-screen-only.py 在 pm_suspend 入口、冻结进程之前返回 EOPNOTSUPP。suspend.screen_only 默认开启且启动后只读，GRUB 显式指定 suspend.screen_only=1；nohibernate 同时禁用磁盘休眠。不会触发原先导致触摸失效的系统 suspend/resume 路径；显示和设备 runtime PM 保留。

lab-keep-awake 仍持有 partial wakelock 以减少自动休眠尝试，但内核保护才是最终屏障。删除强制点亮屏幕逻辑，只把旧调试用 2147483647 毫秒超时迁移为 120000 毫秒；已有其他有限设置保持不变。电源键、合盖和超时只使显示关闭，系统仍运行，关屏耗电高于真正休眠。

QEMU 已启动实际新 Image：只读开关为 Y，freeze/mem/disk 写入均失败，没有 PM suspend entry。/sys/power/state 仍可能显示 freeze/mem；它是支持状态列表，不代表策略允许进入。不要仅凭列表认为保护失效。

实机确认 guard=Y，freeze/mem/disk 同样被拒绝；共享存储仍 MOUNTED。关屏/合盖期间 SSH 持续可用，Android 显示 Asleep，但内核 boot ID 不变且 SPI resume_count 始终为 0。用户确认电源键及合盖两组恢复测试均正常。这验证的是“只关屏”，不表示真正 s2idle 已修复。

恢复旧策略必须同时回退 Image/initramfs/GRUB，或明确在后续受控研发中更改启动策略；当前不要关闭 guard。下面保留原始故障记录。

SSAM 状态来自 01:0e:01:00:01/state。patch-sq2-lid.py 将 closed 映射 SW_LID，保留 tablet switch；加载 tabletsw、gpio_keys、pm8941-pwrkey；lab-input-posture 按真实状态桥接 mica posture。用户确认合盖关屏和电源短按关/亮屏，**开盖未唤醒**。

真实 s2idle 曾全黑、SSH 消失及触摸失效。最后一次由用户长按重启，并非系统自行重启。

旧 SPI-HID 缺少 PM 回调；PM devices 阶段出现 -108 I/O/复位循环。patch-sq2-spi-pm.py 新增 IRQ/work 暂停/恢复、保留 HID 对象、暴露 resume_count；touch-resume-watch 恢复后重启 IPTSd。devices/platform PM debug 阶段能返回且设备 ready，但真实 20 秒关屏/合盖仍失败，不能标为完成。

其他线索：USB xHCI resume 曾重置；SSAM D0 转换与 display 通知待核查；stock suspend_notification_service 缺少平台支持；lab-keep-awake 框架锁未可靠阻止实际 suspend。当前不继续测试。

## 现场检查与保存

### 无密码锁屏

当前用户 10 未设置 PIN/密码，关屏亮屏直接进入桌面。镜像的 android:bool/config_disableLockscreenByDefault 为 true；单独执行 lock_settings set-disabled false 未恢复锁屏。已创建 com.android.shell:Sq2Lockscreen fabricated overlay，将该资源覆盖为 false，并对用户 0/10 启用，再显式启用当前用户锁屏。

用户确认关屏再亮屏显示锁屏且能正常解锁；SystemUI 显示 KEYGUARD 和“点击或滑动即可解锁”。未创建或清除任何凭据，只关屏内核策略不变。cmd lock_settings get-disabled 在本次在线修改后仍返回 true，与实际 SystemUI 行为不一致，不能单独作为验证依据。

复现脚本 scripts/enable-sq2-lockscreen.sh 需在 Android root 环境中手动执行一次（诊断 SSH 中通过 nsenter -t 1 -m -r -- /system/bin/sh 执行已复制到 Android 的脚本）。它不随启动运行，不持续覆盖用户设置。overlay 已记录到 userdata 的 /data/system/overlays.xml，资源文件在 /data/resource-cache；后续实机重启发现该 fabricated overlay 被丢弃。现由 lockscreen-framework-boot 在 Android 启动后恢复默认资源覆盖，不调用 set-disabled，也不改动用户的密码或锁定选择。手动启用脚本仍用于首次显式选择无密码锁屏。使用 cmd power sleep 验证时需等待默认锁定延迟（本次等待 8 秒），不能用 1–3 秒立即唤醒认定无效。最终重启后无需手动执行恢复命令，关屏 8 秒再唤醒，Keyguard showing=true、inputRestricted=true；只关屏 guard 仍为 Y。回退资源覆盖可对用户 0 和实际前台用户执行 cmd overlay disable --user USER com.android.shell:Sq2Lockscreen；如需恢复原来的无锁屏选择，使用系统设置，不自动清除凭据。

使用自己的 SSH key 并确认主机指纹；连接脚本支持 -IdentityFile 或 SQ2_SSH_KEY。

```sh
cat /proc/sys/kernel/random/boot_id
nsenter -t 1 -m -r -- /system/bin/getprop sys.boot_completed
nsenter -t 1 -m -r -- /system/bin/dumpsys mount
nsenter -t 1 -m -r -- /system/bin/df -h /data
ls -l /dev/fuse
grep fuse /proc/filesystems
```

故障同时比较 boot ID、框架 PID 和日志，不只看开机动画。存储回归需实际应用操作，不只 root 写 /data。

本地 out/ 保留镜像、备份和证据，private/ 保留身份及凭据，均不入 Git。公开仓库保留可复用测试程序，不保留原始日志。部署哈希见 configs/deployment-status.json。
