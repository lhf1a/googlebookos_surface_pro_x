# Type-C：双口 DP、原生多屏设置与下口 ADB

默认 Hybrid `full-image` 构建包含双口 DP、系统原生多屏排列/镜像/扩展以及下口 USB-C 自动角色切换。未保存设备选择时下口默认启用 `auto`（自动模式），`--save` 与**高级设置**中的选择会跨重启保留。ADB 保持 Android 授权校验（`ro.adb.secure=1`）。

## 使用

用数据线连接下口与电脑，并在设备上允许 USB 调试授权：

```powershell
adb devices -l
adb -s SERIAL shell
```

在**高级设置 → 调试 → 下口 USB-C 模式**中可视化切换，或在 initramfs root SSH 中查看与切换下口模式：

```sh
/bin/spx-lower-usb status
/bin/spx-lower-usb auto --save
/bin/spx-lower-usb adb --save
/bin/spx-lower-usb host --save
```

- `auto`（默认）：空闲、连接电脑（USB A-to-C / C-to-C）或纯 PD 充电器时保持 `device`（有线 ADB 待命）；接入 DP 显示器、带 DP 扩展坞或纯 USB/网卡扩展坞等外设时自动解绑 gadget 并切至 `host`，拔出并经短暂防抖后自动切回 `device`（若当前由下口 USB 盘启动，则锁定 `host`）。
- `adb`：固定下口为 USB device（有线 ADB）；`host`：固定恢复 DP、Hub 与存储等主机功能。手工切为 `adb` 前需先断开下口显示器或外设。不带 `--save` 仅对本次运行生效；持久配置保存在 `/data/vendor/surface-usb/lower-mode`。

## 接入机制

- 复用下口 DRD/UDC 与匹配当前 `Image` 的 FunctionFS 模块。
- `spx-typec` 在 `lower_auto_dp=1` 时收到下口 DP Alt Mode 请求会发出 `SPX_LOWER_DP=pending` uevent 并短暂等待用户态切为 `host`，随后直接配置 QMP PHY 与 DRM HPD 并向 SAM 回复成功 ACK（休眠时立即唤醒不阻塞 `s2idle`）。
- 事件驱动守护进程 `/bin/spx-lower-usb-watch` 监听 `NETLINK_KOBJECT_UEVENT`（零轮询），联合 `spx-typec` 的 `lower_dp_state`、UCSI `port0-partner` 与下口 xHCI 设备状态自动调度 `/bin/spx-lower-usb apply-role`。
- DWC3 role 异步切换完成后，等待实际 UDC 注册再设置 `sys.usb.controller`。
- 停止 QTI USB HAL，避免与主线 configfs 争用 gadget 所有权。
- 将 `ffs.adb` 接入 Android 自带的 `sys.usb.configfs=1` init 规则与 `adbd`。
- Android `boot_completed` 且 gadget function 创建后恢复已保存模式，未设置时默认启用 `auto`。

## 构建与输入

`full_image_typec.py` 在标准 `build.py` 路径默认执行：

```sh
python3 scripts/full_image_typec.py --services ORIGINAL_SERVICES_JAR --output PRIVATE_ROOT/typec
```

源码构建仅需 `ORIGINAL_SERVICES_JAR`；显式 prebuilt 构建额外指定 `--modules MATCHED_MODULE_DIR --image EXACT_IMAGE`。请使用新输出目录，避免覆盖或混用输入。

构建流程：

1. 将 `system/framework/services.jar` 准备到 `PRIVATE_ROOT/typec/services.jar`，通过 `baksmali`/`smali` 与 `zipalign` 修补 `InputManager` 两处首次 `UNKNOWN SW_LID` 通知。
2. 源码/复用模式下，`build-spx-typec.py` 从内核构建审计获取 `source`/`KOUT`/`cross`，应用 QMP 生命周期补丁并重编五个模块；prebuilt 模式读取 `typec/prebuilt.json`。
3. 从 `probe/typec/settings-tile` 编译安装 Settings 入口，需配置 Android SDK（`SQ2_ANDROID_SDK`）、Java、`baksmali`、`smali`、`zipalign` 与本地签名密钥（`SQ2_TYPEC_KEYSTORE`）。
4. `patch-spx-composer.py` 对已知 ELF 布局的 QTI composer 适配模式组与 DRM 热插拔过滤（等价源码补丁见 `patches/spx-hwc-multidisplay.patch`）。
5. 从板级 DT 派生两路 DP/PHY 图与下口 OTG，同时保留内屏、保留内存与固件配置。
