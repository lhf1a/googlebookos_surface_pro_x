# SQ2 开发交接

更新时间：2026-10-27。不包含原始日志、网络地址或凭据。

## 最近完成：共享存储

故障：Files、Chrome 下载及依赖 external_primary 的功能失败。/data ext4 已读写挂载，约 453 GiB 总容量；当前用户 10，用户 0/10 已解锁，但两个 emulated 卷均 UNMOUNTABLE。Settings 抛出 Volume external_primary not found。

根因：CONFIG_FUSE_FS=m，原启动流程未加载模块，缺少 /dev/fuse。运行中补载没有让失败卷自动恢复。修复在 probe/test2/init-googlebook-native 中于 Android init 前加载 FUSE 并检查设备节点，未格式化或重分区。

修复已写入当前内置 SSD，正常音频入口重启后两个卷均 MOUNTED。**用户确认系统“文件”打开下载/新建文件夹和 Chrome 下载均正常。** Android 接管后 /dev/fuse 为 root:root 0600，仍工作正常，不应据此盲目放宽权限。修复后 /data 约 450 GiB 可用。

现场 sm 独立 app_process 命令曾无输出并返回 0，不能据此判断挂载成功；以 dumpsys mount、实际 FUSE 挂载和应用访问为准。PID 1 的 /storage 绑定用户 0，从此命名空间直接访问用户 10 可能 Permission denied，不代表用户 10 应用故障。

## 当前待办

- 休眠按用户要求仅记录、暂停推进。
- 音频结束约 10 秒后爆音、缺低频、声像偏右暂缓，见 configs/audio-known-issues.json。
- 蓝牙其他配置文件、LTE 有 SIM 联网、Turnip Vulkan、长期稳定性尚未完成验证；不要默认开始这些新测试。

## 启动结构

- Surface Pro X SQ2、16 GB，已替换为实验用 512 GB NVMe；内核 6.3.8-sq2-android1，内置 NVMe 已启用。
- ESP 分区 6，userdata 分区 1；布局见 scripts/build-googlebook-internal-ssd.py。super 切片映射为只读 EROFS，适配经 RAM overlay 注入。
- ESP /EFI/BOOT/grub.cfg；内核/DTB/initramfs 在 /boot/sq2-android1/。正常音频、禁用音频、诊断和关机入口，正常默认、3 秒。
- initramfs 准备驱动、网络及保留的 Dropbear，再由原 Android init 成为 PID 1。SSH 根目录仍是调试 initramfs。
- Android 命令：nsenter -t 1 -m -r -- /system/bin/<command>；Android 文件树 /proc/1/root；ESP /media/esp；本次日志 /logs。
- sq2.audio=1 启用音频；禁用音频入口仍共享图形、触摸及存储修复。

## 图形与框架稳定性

scripts/patch-mesa-sq2-multiplane.py 修复 mapper5 辅助实例未导入 handle 导致元数据查询失败，以及单平面假设。RAII import/free，读取 FOURCC、modifier、plane layout，支持 1–4 平面并检查偏移/步长，返回原 handle 借用 FD，避免释放后 FD 失效。

此前 YV12 640×360 导入失败触发 SurfaceFlinger GaneshBackendTexture / SkiaRenderEngine::mapExternalTextureBuffer abort；后续 AudioService watchdog 为该轮级联现象，不应把全部重启归因于音频。新 RGBA、YV12/格式 35 GLES 读回通过。无音频视频观察 825 秒、音频开启观察 503 秒，框架 PID 未变化且用户确认有声；这是有限测试，不是长期保证。

保留 vendor.minigbm.debug=nocompression，解决早期文字/动画彩色损坏。Turnip 未验证。源码 probe/graphics/；集成 build-sq2-mesa.sh、build-googlebook-graphics-overlay.py、pack-sq2-stability.sh。

## 触摸及输入

IPTSd 3.1.1 加 scripts/patch-iptsd-sq2-touch.py 状态/连续性修复。configs/iptsd-sq2.conf 保留 Contacts AspectMax=2.0，触点检测与稳定性阈值默认；用户确认轻点、连续拖动、双指缩放正常且无自动点击。DFT ButtonMinMag=4000 是单独的笔配置。

输入节点发布时序/权限修复使 EventHub 接入设备。正常开机可用与休眠后恢复是独立结论，后者未解决。

## Wi-Fi 与蓝牙

Wi-Fi 使用内核接口及 Android 通用 HAL 适配：sq2-wifi-driver.c、sq2-wifi-hal.cpp、wifi-framework-boot。已在设置内扫描、连接，开发通过内网 SSH；有实验室联网引导跳过逻辑。

蓝牙由 Linux hci_uart/btqca 初始化 WCN3998，sq2-hci-bridge 将真实 HCI_USER socket 桥接到 /dev/sq2-bt-h4，Cuttlefish AIDL HAL 接管。patch-sq2-bluetooth.py 包含关闭 serdev 防护、特定 baud ACK 处理及诊断；bluetooth-framework-boot 和 configs/sq2-bluetooth.rc 持久化。用户确认名称显示、配对成功；A2DP、通话等全流程未验证。不要在线反复卸载 QCA transport 模块。

## 音频

ADSP → SLIM/WCD9340 → SoundWire/双 WSA881x。物理左右均有声；Android AIDL HAL 和 48 kHz S16 stereo policy 已接入。patch-sq2-audio-hal.py 去掉模拟器跳帧逻辑，校验原始/修改后哈希。audio-drivers-start、audio-speaker-route、audio-framework-boot 负责固件、路由、HAL。

调音问题仍开放。历史音频期间发生过整机重启；有限视频观察不能证明所有音频故障已解决。

## 合盖、电源与休眠（暂停）

SSAM 状态来自 01:0e:01:00:01/state。patch-sq2-lid.py 将 closed 映射 SW_LID，保留 tablet switch；加载 tabletsw、gpio_keys、pm8941-pwrkey；lab-input-posture 按真实状态桥接 mica posture。用户确认合盖关屏和电源短按关/亮屏，**开盖未唤醒**。

真实 s2idle 曾全黑、SSH 消失及触摸失效。最后一次由用户长按重启，并非系统自行重启。

旧 SPI-HID 缺少 PM 回调；PM devices 阶段出现 -108 I/O/复位循环。patch-sq2-spi-pm.py 新增 IRQ/work 暂停/恢复、保留 HID 对象、暴露 resume_count；touch-resume-watch 恢复后重启 IPTSd。devices/platform PM debug 阶段能返回且设备 ready，但真实 20 秒关屏/合盖仍失败，不能标为完成。

其他线索：USB xHCI resume 曾重置；SSAM D0 转换与 display 通知待核查；stock suspend_notification_service 缺少平台支持；lab-keep-awake 框架锁未可靠阻止实际 suspend。当前不继续测试。

## 现场检查与保存

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
