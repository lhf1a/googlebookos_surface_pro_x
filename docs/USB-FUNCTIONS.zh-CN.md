# SQ2 USB 存储与 USB 音频

当前 Type-C、默认入口及下口 ADB 策略见 [TYPEC](TYPEC.zh-CN.md)。

## 功能范围

- **USB 存储**：FAT32 U 盘生成 Android `PUBLIC` 卷，由 `ExternalStorageProvider` 通过 SAF 暴露给当前用户，不开启存储采用或擦盘。
- **USB 音频**：USB-C 音频外设分配为 ALSA `card1`，内置音频保留 `card0`，未指定 preferred device 的标准 `AudioTrack`/`AudioRecord` 自动路由至 `USB_HEADSET`。

## 实现机制

`probe/test2/usb-functions-stage` 在 RAM vendor overlay 准备好后、Android `init` 之前执行，仅在设置 `sq2.usb=experimental` 时启用，无标记、`sq2.usb=0` 或 `sq2.rescue=1` 时跳过：

1. 校验 `screen_only`、overlay、原 `fstab`/音频策略哈希及成套载荷哈希。
2. 加载匹配的 `snd_usb_audio`，全部 32 个 card index 使用 `-2` 位掩码排除 `card0`。
3. 为三个 USB 控制器补充 `voldmanaged=usbN:auto` 发现规则，不匹配 NVMe/loop，不添加 `formattable`、`encryptable`、`adoptable`；`/data`、`/misc` 与早期 FUSE 加载保持原样。
4. 将 USB 音频策略引用和 AIDL v4 `IModule/usb` 声明配对安装到 RAM overlay；HAL 保持 `774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea`，内置 DMIC0/1、`CARD_0_DEV_1`、增益和 SoundWire 配置不变。

## 构建与测试

```sh
python3 scripts/pack-sq2-usb-functions.py \
  out/video-decode/runtime-v1/boot-v1.cpio.gz \
  out/video-decode/vp8-limits/caps-addon.cpio.gz \
  out/NEW/usb-functions-addon.cpio.gz
python3 tests/test_sq2_usb_boot.py -v
bash scripts/build-sq2-usb-audio-app.sh
```

- 打包器审计合并后的 last-entry-wins 结果，保留私有 SSH 成员在最后，含凭据结果留在忽略目录。
- 主线 `build-source-boot.py` 通过 `sq2-usb-boot-payload.py` 注入相同 USB 载荷与蓝牙输入 helper，`build-source-disk.py` 为正常入口加入 USB 启用标记、为救援入口加入 `sq2.usb=0`；部署前可使用 `--require-usb-bluetooth` 校验镜像内实际 ESP 菜单和 initramfs。
- 测试 APK `org.surface.sq2.usbtest` 使用标准公开 API 提供 `probe`、`tone`、`roundtrip`、`auto-tone` 与 `auto-roundtrip`，通过 `SQ2_USB_AUDIO_KEY` 指定本地测试密钥签名。

## 回退注意事项

- 若需禁用 stage，使用启动参数 `sq2.usb=0`。
- **禁止热重启 `vold`**：`/system/etc/init/vold.rc` 包含 `reboot_on_failure reboot,vold-failed`，直接执行 `ctl.restart vold` 会触发系统重启。
