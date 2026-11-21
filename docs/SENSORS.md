# Surface 自动亮度与自动旋转

2026-11-16：SQ2 已完成独立 HAL、Android SensorService、用户照光/遮挡与横竖屏测试，
完整重启后再次确认正常。已验收 initramfs 为 `e4f6cb1491a35c2c632a7747cd5c9c171e2ee099d5ce316459d56c036ff2525d`。
共同组件指纹见 `configs/sensors-baseline.json`；原始资料、固件和含身份镜像不进 Git。
SQ1 同包实测、长期休眠/功耗尚未验收。

## 从零构建与共用依赖

无需先启动 Surface，也无需从每台设备提取驱动或启动包。用户已确认当前 SLPI
闭源固件、型号配置和默认光学校准可在 SQ1/SQ2 设备间通用；准备一次共用输入后，
后续构建直接复用，不要求逐机提取或重新校准。

Linux TCS3430 驱动、SSC 客户端、Android SubHal、启动集成及旋转覆盖源码在
`probe/sensors/`，构建脚本在 `scripts/`，具体命令如下。内核源码、Android 平台
接口/匹配库、工具链和 EFI 工程均可在构建主机离线准备，无需连接运行中的 Surface。
SLPI 固件仍作为共用二进制依赖提供；现有资料已足够准备传感器构建输入。
`extract-spx-light-calibration.py` 仅在需要从已有 `SurfaceLightSensor.sys` 重新生成
默认校准文件时使用；已有共用 golden 文件可直接复用，无需再次提取 Windows 驱动。

## 默认构建

本工作区 `build.py full-image` 的唯一Hybrid构建必须安装传感器。输入目录默认为
`PRIVATE_ROOT/sensors`，可用 `--sensor-inputs` 指定；标准入口不提供跳过传感器选项。
缺少必需输入会报错。先按下文准备共用包，再运行整包；总流程见 [STANDARD-BUILD](STANDARD-BUILD.md)。

```sh
python3 scripts/build.py full-image --graphics hybrid \
  --private-root /path/to/private-inputs --sensor-inputs /path/to/sensors \
  --camera-inputs /path/to/camera --output out/new-full-image --esp-only
```

source 内核模式重新构建 TCS3430、SLPI PAS 与 FastRPC 模块，使用同一次内核构建的
配置、symvers 和交叉编译器。prebuilt 模式核验输入完整性、ARM64 和内核 ABI。
当前源码始终提供 boot/stage helper；不打包独立测试程序、抓包器或开机采集脚本。
SLPI 固件进入 ESP firmware 树，带 PAS12 的 qebspil 与对应 DT 成对安装。
不把样机整份运行时 DT 覆盖到新构建，不改变无关设备或保留内存。

构建输入由源码产物与 SQ1/SQ2 共用 OEM 资料组成：

```sh
bash scripts/build-spx-sensors.sh KERNEL_SOURCE MATCHING_BUILD NEW_ALS_OUTPUT
python3 scripts/build-spx-slpi.py KERNEL_SOURCE MATCHING_BUILD NEW_SLPI_OUTPUT
python3 scripts/build-spx-sensor-hal.py AOSP_INTERFACES TOOLCHAINS MATCHING_LIBRARIES NEW_HAL_OUTPUT
python3 scripts/build-spx-sensor-overlay.py TOOLCHAINS NEW_OVERLAY_OUTPUT
python3 scripts/extract-spx-light-calibration.py SurfaceLightSensor.sys GOLDEN_OUTPUT
python3 scripts/prepare-spx-slpi-efi.py MATCHING_EFI_SOURCE NEW_EFI_SOURCE
```

HAL 使用 AOSP sensors 2.1 SubHal 接口和匹配平台的 HIDL/libc++ ABI。
按现有 EFI 工程构建修改后的源码；RPC 文件服务使用 linux-msm/hexagonrpc
`da7a3742ab8d5273a281b70ee11f16fb2cb6a690` 的 ARM64 静态构建。
将三个模块放在同一目录后生成私有输入清单：

```sh
python3 scripts/build-spx-sensor-bundle.py \
  --modules MODULE_DIR --hal HAL_DIR/sensors.spx.so \
  --overlay OVERLAY_DIR/spx-sensors-overlay.apk --rpc HEXAGONRPCD \
  --config OEM_MODEL_CONFIG_DIR --calibration GOLDEN_OUTPUT \
  --firmware qcslpi8180.mbn --efi qebspilaa64.efi \
  --output private/build-inputs/sensors
```

只导入型号 JSON/config_list、型号固件和模型默认光学校准，绝不导入另一设备的
registry、persist、身份或用户数据。修改 HAL/EFI 源码后须重编并更新清单，
不能仅修改源码便宣称旧二进制已经包含修复。

## 运行机制与修复

TCS3430 位于板载 I2C 0x888000/0x39，驱动输出同一积分帧的 XYZ/IR。
HAL 使用 OEM 模型默认校准换算 lux；实际 Android 亮度曲线与用户模式由框架管理。
SLPI 通过 EFI PAS12 启动，Linux 仅接管已成功启动的 DSP，禁止活跃 DSP 热复位。
保留现有 0x99000000/20MiB SCSS 内存边界，不侵占 ADSP。

加速度 HAL 动态发现 SSC 服务及 accel SUID，只请求必要的数据，不做导致固件
崩溃的原 qsh 全量枚举。QRTR 先 getsockname 再 bind，不能把本地节点写死为 0。
静态资源覆盖打开 config_supportAutoRotation；config.override_forced_orient=true
取消桌面类型的默认方向固定，但保留用户旋转锁。启动 helper 不重设用户的
自动亮度/旋转选择。测试时用户已启用两项功能。

实机记录：lux 约 52–160、背光 643–1053/4095、显示方向 0/3 随操作变化，用户两次
确认正常；四个 DSP attached，原物理声卡保留。早期候选黑屏的具体原因仍未证明，
不能把修正 QRTR bind 等同于解释所有历史黑屏。

### 2026-11-21：SLPI SSC 停流握手与 QRTR 复用修复

SQ2 息屏停用加速度计（进入 `s2idle` 前）曾触发 `qcom_smd_qrtr: invalid ipcrouter packet`
并导致 `remoteproc3`（SLPI）永久崩溃（`EX:kernel:0x0:SNS_SEE_P_1:0xc1:PC=0xb0040408`）。
直接原因为 HAL 在连续流（accel/gyro/mag）活跃期间仅执行原始 `close(fd)`，未先向 SSC
客户端发送停流请求（`SNS_CLIENT_MSGID_SNS_CLIENT_DISABLE_REQ = 10`）并等待应答。

`probe/sensors/spx-ssc.{h,cpp}` 与 `probe/sensors/spx-sensors-subhal.cpp` 已修复如下：

- **显式停流握手**：新增 `SscSensor::disable(250)`，发送 `kSscMsgClientDisableReq (10)`，
  在剩余超时期内吸收在途数据并等待匹配的 QMI response（`ctl_flag=2, msg_id=0x20, txn`），
  随后执行 `drain(15)` 清空残留包；`SscSensor::close(bool quiesce = true)` 仅在 `quiesce && streaming`
  时先调用 `disable(250)` 再 `closeRaw()`。
- **空闲 QRTR socket 与 SUID 复用**：`disable()` 成功后保留已打开的 QRTR socket 与已解析 SUID
  （`isOpen() == true`），避免反复开关传感器时触发 QRTR 客户端连接/断开抖动；仅在显式关闭、
  `DISABLE_REQ` 失败或收到 `QRTR_TYPE_BYE` / `QRTR_TYPE_DEL_SERVER` 时释放句柄。
- **非唤醒流与死锁防护**：`SscSensor::rate()` 对连续流下发 `kSscDeliveryNoWakeup (1)`；
  `SurfaceSensors::work()` 在调用阻塞的 `disable(250)` / `close(...)` 前先解锁 `mutex`，
  并以 `slpiAlive()` 检查 `/sys/class/remoteproc/<slpi>/state`（`attached` 或 `running`），
  防止向已崩溃的 SLPI 执行 `sendto()`。`receive()` 与 `drain()` 在 `EINTR` 时按剩余截止时间重试。

本次产出的匹配构件指纹：

- `sensors.spx.so` SHA-256：`1463cb3cef5b9844f6dc0f1b6c98c364b2148e3115b1b4b431269d28dd13c0f5`
- 实机部署 `initramfs.cpio.gz` SHA-256：`5644bf735c8c71352c6c446181140df6ffaf3b10812d9c82290425855c57718b`

## 验证

```sh
python3 -m unittest discover -s tests -p 'test_full_image*.py'
python3 -m unittest discover -s tests -p 'test_spx_als.py'
python3 -m unittest discover -s tests -p 'test_spx_slpi.py'
g++ -std=c++17 -fsanitize=address,undefined tests/spx_ssc_test.cpp -o /tmp/spx-ssc-test
/tmp/spx-ssc-test
```

`spx_lux_test.cpp` 需传入私有 golden 数据，以原算法参考向量检查换算。
`subhal-test LIBRARY [1|2]` 为手动实机加速度/光线事件、flush 和停用测试。
主线实际安装函数的有效载荷及 DT 已与已验收包比较一致，HAL/三个模块重编逐字一致。
本次未重新制作整块磁盘镜像，也不改写历史 kernel-baseline 的整机验收声明。

2026-11-21 实机与主机验证：

- **主机单测**：`spx_ssc_test.cpp`（ASan/UBSan，`512`/`513`/`10` 报文编码、`isQmiClientResponse`、
  `isPeerDownCtrl`、`disable()` 未打开防护）与 `tests/test_spx_slpi.py`、`tests/test_full_image_sensors.py`
  全数通过。
- **SQ2 实机冷启动基线**：内核 `6.18.60-sq2-android1`、`sensors.spx.so` 实机 SHA-256 与构建产物一致，
  `/sys/class/remoteproc/remoteproc3/state == attached`，启动阶段（含 gyro/mag 开关切换）`dmesg`
  零 `invalid ipcrouter packet`、零 SLPI fatal error。
- **SQ2 `subhal-test` 压测（15 轮）**：推入 `/data/local/tmp/subhal-test` 后依次对 handle `1`（accel，5 轮）、
  `3`（gyro，5 轮）、`4`（mag，5 轮）执行 `activate(true) -> flush -> activate(false)/DISABLE_REQ -> initialize()`，
  结果 `5/5 SUBHAL_ACCEL_PASS`、`5/5 SUBHAL_GYRO_PASS`、`5/5 SUBHAL_MAG_PASS`，清理临时二进制后
  `remoteproc3 == attached`、`dmesg` 无错误。
- **SQ2 息屏停流与亮屏恢复（5 轮）**：通过 Android `cmd power set-wakelock acquire PARTIAL_WAKE_LOCK`
  临时保活后连续执行 5 轮 `input keyevent 223`（SLEEP）/ `224`（WAKEUP）：每轮息屏后
  `Surface LSM6DSO Accelerometer (0x01000001)` 活跃连接数清零，亮屏后立即恢复为 `connections=2`；
  加速度计最新事件时间戳由压测前 `1147.027s` 持续推进至 `1188.934s`，`WindowOrientationListener`
  `mLastFilteredTimestampNanos=1188933632779` 同步刷新，`remoteproc3 == attached` 且 `dmesg` 无
  `invalid ipcrouter packet` / SLPI crash。
- **边界说明**：上述 5 轮息屏/亮屏自动化循环持有临时 Android `PARTIAL_WAKE_LOCK` 以避免设备在无 RTC
  `wakealarm` 的情况下进入深睡，因此实测时 `suspend_stats/success=0`；真机物理合盖/电源键完整进出
  `s2idle` 及折叠键盘竖屏旋转体验仍需人工手感复核。
