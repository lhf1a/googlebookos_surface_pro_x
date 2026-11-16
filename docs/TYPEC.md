# Type-C：双口 DP、原生多屏设置与下口 ADB

默认 Hybrid full-image 构建包含两口 DP、系统原生多屏排列/镜像/扩展，以及下口 USB ADB。
无设备保存选择时下口默认 ADB；`--save` 的 host/adb 选择会保留，不在每次启动强制覆盖。
ADB 保留 Android 授权（`ro.adb.secure=1`），不预置任何电脑授权或密钥。

SQ2 已实测两口出图、换口/重插、拔线后内屏保持、排列/镜像/扩展及 Windows ADB shell/重启恢复。
当前实机默认入口为 `SQ2DP2`，原 `sq2-sensors`、Hybrid/Mesa 回退保留。
SQ1、DP 音频、长期外屏休眠和稳定 4K60 性能基准未验收；主机构建不代替这些结论。

## 使用

用数据线连接下口与电脑，在设备上允许 USB 调试授权：

```powershell
adb devices -l
adb -s SERIAL shell
```

`SERIAL` 使用 `adb devices` 列出的实际 serial；不是构建/启动准入条件。

以下在 initramfs root SSH 中执行：

```sh
/bin/spx-lower-usb status
/bin/spx-lower-usb adb --save
/bin/spx-lower-usb host --save
```

`adb` 将下口设为 USB device；`host` 恢复下口 DP、Hub、存储等主机用途。
切换前先断开下口显示器/外设；活动 DP 的 role setter 会拒绝切为 device。
不同时承诺同一物理下口既连电脑做 ADB 又接显示器做 DP。
不带 `--save` 只修改本次运行。保存值位于设备自己的
`/data/vendor/surface-usb/lower-mode`，未放入通用包。

## 接入机制

- 复用已验证的下口 DRD/UDC 与匹配当前 Image 的 FunctionFS 模块。
- DWC3 role 切换异步完成后，等待实际 UDC 注册，再提供 `sys.usb.controller`。
- 停止不匹配本机主线 configfs 约定的 QTI USB HAL，避免两个 gadget owner 争用。
- 使用 Android 自带 `sys.usb.configfs=1` 的 init 规则与 adbd/bridge，
  仅把 `ffs.adb` 接入配置；不自造 ADB 协议或第二个 adbd。
- Android boot complete 且 gadget function 已创建后恢复保存模式，无选择则启用下口 ADB。
- `ro.adb.secure=1`、设备授权、普通 shell 身份保持，不开启额外网络 ADB。
- 原主机模式可恢复，原有本地 ADB bridge 不因此移除。

## 构建与输入

`full_image_typec.py` 在标准 `build.py` 路径默认执行，没有逐台设备输入。

```sh
python3 scripts/full_image_typec.py --services ORIGINAL_SERVICES_JAR --output PRIVATE_ROOT/typec
```

源码构建只需上述共用 JAR；显式 prebuilt 构建额外指定 `--modules MATCHED_MODULE_DIR --image EXACT_IMAGE`。
现有目录不要覆盖；使用新目录或显式迁移共用输入。

构建流程：

1. 将共用 Android system 的原 `system/framework/services.jar` 准备到
   `PRIVATE_ROOT/typec/services.jar`。仅处理 InputManager 两处 UNKNOWN SW_LID 首次通知；
   不把未知当合盖，真实开合回调不变。baksmali/smali 重组后复读所有类，zipalign 对齐。
2. 源码/复用模式从本次内核构建审计取得 source/KOUT/cross，核对实际 Image，
   `build-spx-typec.py` 在副本应用 QMP 生命周期补丁并重编五个模块。
   不用同 release 的旧模块替代。显式 prebuilt 模式读取 typec/prebuilt.json 的准确 Image 配对。
3. 原生 Settings 页面不重写；从 `probe/typec/settings-tile` 编译安装入口。
   需要 Android SDK（`SQ2_ANDROID_SDK`）、Java、baksmali、smali、zipalign。
   APK 由本地构建签名密钥签名（可用 `SQ2_TYPEC_KEYSTORE` 指定）；不携带设备授权。
4. QTI composer 仍为现有 Android/QTI 共用输入。`patch-spx-composer.py` 对已知 ELF 布局
   精确适配模式组和 DRM 热插拔过滤；等价源码补丁见 `patches/spx-hwc-multidisplay.patch`。
   这不是整套 HWC 源码重编；不同 ELF 布局明确拒绝，不能将任意库混装。
5. DT 从构建自己的板级 DT 派生：两路 DP/PHY 图与下口 OTG；保留内屏、保留内存、固件和其它子系统。

私有输入仅包含共用 JAR/平台依赖及显式 prebuilt 模块；不包含每台设备的 lower-mode、ADB keys、
用户账户、地址或凭据。代码与补丁入库，含身份镜像/原始日志保持 ignored。
