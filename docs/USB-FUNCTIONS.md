# SQ2 USB 存储与 USB 音频

## 已验证状态（2026-10-30）

当前 SQ2 默认入口为 `sq2-usb-test`（菜单标题 **SQ2 USB storage and audio**）。
原有四个入口和载荷全部保留；USB 更新没有替换 Image、DTB 或音频 HAL。

- FAT32 USB 盘能够生成 Android PUBLIC 卷；用户确认 Files 打开、创建与读写正常。
- USB-C EarPods `05ac:110b` 的播放和录音均可用。用户确认左/右声道、人声与回放速度正确。
- 一次性启动验证后，提升相同 USB 载荷为默认，再正常重启验证了持久化。
- 默认启动时 EarPods 预插，仍分配为 ALSA card1；内置音频保留 card0。
- 重启后未指定 preferred device 的标准 AudioTrack/AudioRecord 也自动选择 USB headset。
- 默认启动后再次换回 USB 盘，PUBLIC 卷和 Files 操作仍通过。

一次启动 boot `2bdc010d…`，默认重启 boot `8abfe33c…`。48 kHz 测试：
立体声左右音各 2 秒，输出 192000 帧；单声道录音 240000 sample，随后输出 240000 帧。
最后一次自动路由录音约 5202 ms，两端实际 routed type 均为 USB_HEADSET（22）。
这是当前 U 盘、EarPods 和测试范围的通过，不证明所有 USB 外设/文件系统或长期稳定性。
exFAT 仅在早期原盘上做过内核只读挂载，不等同于本轮 Android 自动挂载验证。
Type-C DP/AltMode 不在此次完成范围。

## 修复机制与保护

`probe/test2/usb-functions-stage` 在 RAM vendor overlay 准备好、Android init 之前执行：

1. 校验 screen_only、overlay、原 fstab/音频策略哈希及成套载荷哈希。
2. 加载匹配的 `snd_usb_audio`，全部 32 个 card index 使用 `-2` 位掩码，排除 card0。
3. 为三个已确认的 USB 控制器补充 `voldmanaged=usbN:auto` 发现规则。
   不匹配 NVMe/loop，不加 formattable、encryptable、adoptable 或格式化行为；
   `/data` 和 `/misc` 两行保持原样。原 FUSE 早期加载保留。
4. 将 USB 音频策略引用和 AIDL v4 `IModule/usb` 声明配对安装到 RAM overlay。
   现用 HAL 本身已包含 USB 实现，无需修改其代码或 stock transport。

只有 `sq2.usb=experimental` 启用；无标记、`sq2.usb=0` 或 `sq2.rescue=1` 不执行。
HAL 保持 `774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea`，
内置 DMIC0/1、CARD_0_DEV_1、增益和 SoundWire 配置不改。

非 adoptable USB 的 mountFlags=0/没有普通 `/storage/UUID` 路径不等于 Files 不可用：
ExternalStorageProvider 可通过 SAF 暴露属于当前用户的已挂载 PUBLIC 卷。
本次以实际 Files 操作与 PUBLIC MOUNTED 为依据，没有为了“可见”开启存储采用/擦盘。

## 构建与测试

WSL，固定已测 runtime-v1 和 caps 输入，输出必须是新路径：

```sh
python3 scripts/pack-sq2-usb-functions.py \
  out/video-decode/runtime-v1/boot-v1.cpio.gz \
  out/video-decode/vp8-limits/caps-addon.cpio.gz \
  out/NEW/usb-functions-addon.cpio.gz
python3 tests/test_sq2_usb_boot.py -v
bash scripts/build-sq2-usb-audio-app.sh
```

打包器拒绝未知输入/既有输出，逐条审计合并后的 last-entry-wins 结果，保留私有 SSH
成员最后；含凭据的结果只能留在忽略目录。本机源码重建 addon 与实测载荷字节完全相同。
四组主机测试包括实际 helper 的 9 个临时文件系统场景，以及配置和输入哈希拒绝检查。

以上打包器保留为现机固定基线增量路径。主线默认 `build-source-boot.py` 现通过
`sq2-usb-boot-payload.py` 直接打入相同 USB 载荷和当前蓝牙输入 helper，
`build-source-disk.py` 为正常入口自动加入 USB 启用标记、为救援加入 `sq2.usb=0`，
并验证源文件/构建 audit 一致。`--require-usb-bluetooth` 可让部署前检查拒绝旧镜像；
检查读取镜像内实际 ESP 菜单和 initramfs，而不是旁边的缓存文件。
通用源码镜像仍有既存的显示/触控构建选择，不能未经审查覆盖当前实机的全部非视频配置。

测试 APK `org.surface.sq2.usbtest` 使用标准公开 API；可操作 `probe`、`tone`、
`roundtrip`，以及不设置 preferred device 的 `auto-tone`、`auto-roundtrip`。
必须实际路由到 USB，不能偷偷回退到内置设备。音调约 -24 dBFS，不改系统音量；
前台录音仅 5 秒，原始音频只保留在内存，结果 JSON 留在应用私有目录。
签名使用操作者本地测试密钥，`SQ2_USB_AUDIO_KEY` 可指定路径，不提交密钥。

## 部署证据和回退

本机证据目录：`out/usb-c-20261030/`，原 XML 四角色持续保留，storage/、boot-v1/
增加对应的 fstab/init/菜单副本、补丁、回滚和实际日志，没有把副本检查冒充实机测试。

- runtime `8b6c1f9a…` + caps `fb1831a9…` + USB addon `46896b1f…`（645195 B）。
- 默认菜单最终 SHA `44461c172d488625ddae4fbe8b135571fd8310eedc8ed05dac1a8c18f651ee85`。
- 菜单提升时检测到另一路新增 Bluetooth input addon，哈希保护中止旧菜单写入；
  审计确认其只改两个蓝牙 helper 后，保留其 `3ecfc42e…` 载荷再完成提升。
- ESP 菜单备份 `grub.cfg.before-usb-v1`、`grub.cfg.before-usb-default` 保留。
  菜单 3 为原 Codec2（不启 USB stage）的回退；不要用旧备份抹掉后续蓝牙更新。
- 若需要禁用 stage，临时启动参数 `sq2.usb=0`；**不能热重启 vold 来验证回退**。

部署前必须重新核对物理磁盘、ESP marker/UUID、userdata UUID/root source，校验 gzip/hash、
sync 并原子替换；本次没有格式化 userdata 或删除旧镜像。

## 已发生的验证事故

最初尝试 RAM bind fstab 后直接 `ctl.restart vold` 导致设备重启。
实机 `/system/etc/init/vold.rc` 含 `reboot_on_failure reboot,vold-failed`，重启后的
boot reason 也为 `reboot,vold-failed`。禁止重复该过程，不把它解释成已证实的内核崩溃。
随后 `USB-serial` 停机来自旧 USB 启动路径而不是正常 `sq2.native=ssd` 分支；
用户自行将旧启动 U 盘改成 FAT32 后，已恢复 NVMe 启动。不要绕过磁盘/serial 身份保护。
后续两轮验证均使用正常 Android reboot，未再重启 vold。
