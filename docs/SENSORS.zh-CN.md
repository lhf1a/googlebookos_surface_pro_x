# Surface 自动亮度与自动旋转

共同组件指纹记录在 `configs/sensors-baseline.json`。SLPI 闭源固件、型号配置和默认光学校准可在 SQ1/SQ2 设备间共用，无需逐机提取。

## 默认构建

`build.py full-image` 必须提供传感器输入（默认 `PRIVATE_ROOT/sensors`，或通过 `--sensor-inputs` 指定）。总流程见 [STANDARD-BUILD](STANDARD-BUILD.zh-CN.md)。

```sh
python3 scripts/build.py full-image --graphics hybrid \
  --private-root /path/to/private-inputs --sensor-inputs /path/to/sensors \
  --camera-inputs /path/to/camera --output out/new-full-image --esp-only
```

`source` 内核模式下会针对当前内核重编 TCS3430、SLPI PAS 与 FastRPC 模块；`prebuilt` 模式核验 ARM64 架构与内核 ABI。SLPI 固件安装至 ESP firmware 树，带 PAS12 的 `qebspil` 与对应 DT 成对安装。

打包传感器输入前先构建源码产物并准备共用 OEM 资料：

```sh
bash scripts/build-spx-sensors.sh KERNEL_SOURCE MATCHING_BUILD NEW_ALS_OUTPUT
python3 scripts/build-spx-slpi.py KERNEL_SOURCE MATCHING_BUILD NEW_SLPI_OUTPUT
python3 scripts/build-spx-sensor-hal.py AOSP_INTERFACES TOOLCHAINS MATCHING_LIBRARIES NEW_HAL_OUTPUT
python3 scripts/build-spx-sensor-overlay.py TOOLCHAINS NEW_OVERLAY_OUTPUT
python3 scripts/extract-spx-light-calibration.py SurfaceLightSensor.sys GOLDEN_OUTPUT
python3 scripts/prepare-spx-slpi-efi.py MATCHING_EFI_SOURCE NEW_EFI_SOURCE
```

HAL 使用 AOSP sensors 2.1 SubHal 接口及匹配平台的 HIDL/libc++ ABI；RPC 文件服务使用 `linux-msm/hexagonrpc`（`da7a3742ab8d5273a281b70ee11f16fb2cb6a690`）的 ARM64 静态构建。生成私有输入清单：

```sh
python3 scripts/build-spx-sensor-bundle.py \
  --modules MODULE_DIR --hal HAL_DIR/sensors.spx.so \
  --overlay OVERLAY_DIR/spx-sensors-overlay.apk --rpc HEXAGONRPCD \
  --config OEM_MODEL_CONFIG_DIR --calibration GOLDEN_OUTPUT \
  --firmware qcslpi8180.mbn --efi qebspilaa64.efi \
  --output private/build-inputs/sensors
```

仅导入型号 JSON/`config_list`、型号固件和默认光学校准，禁止导入其他设备的 registry、persist 或身份数据。

## 运行机制与修复

- **ALS 与 SLPI**：TCS3430 位于 I2C `0x888000`/`0x39`，输出同一积分帧的 XYZ/IR 并按模型默认校准换算 lux。SLPI 通过 EFI PAS12 启动，保留 `0x99000000`/20MiB SCSS 内存边界，不侵占 ADSP。
- **HAL 与 QRTR**：加速度 HAL 动态发现 SSC 服务及 accel SUID，不执行全量 `qsh` 枚举；QRTR 先执行 `getsockname` 再 `bind`。
- **旋转覆盖**：开启 `config_supportAutoRotation` 并设置 `config.override_forced_orient=true`，同时保留用户旋转锁。
- **SLPI SSC 停流握手（`probe/sensors/spx-ssc.{h,cpp}`、`spx-sensors-subhal.cpp`）**：
  - `SscSensor::disable(250)` 发送 `kSscMsgClientDisableReq (10)`，等待匹配的 QMI 应答（`ctl_flag=2, msg_id=0x20, txn`）并执行 `drain(15)` 后再关闭。
  - `disable()` 成功后复用已打开的 QRTR socket 与已解析 SUID，仅在显式关闭、`DISABLE_REQ` 失败或收到 `QRTR_TYPE_BYE` / `QRTR_TYPE_DEL_SERVER` 时释放句柄。
  - `SscSensor::rate()` 对连续流下发 `kSscDeliveryNoWakeup (1)`；`SurfaceSensors::work()` 在阻塞调用 `disable()`/`close()` 前解锁 `mutex`，并通过 `slpiAlive()` 检查 `/sys/class/remoteproc/<slpi>/state`（`attached` 或 `running`）再执行 `sendto()`。

## 验证

```sh
python3 -m unittest discover -s tests -p 'test_full_image*.py'
python3 -m unittest discover -s tests -p 'test_spx_als.py'
python3 -m unittest discover -s tests -p 'test_spx_slpi.py'
g++ -std=c++17 -fsanitize=address,undefined tests/spx_ssc_test.cpp -o /tmp/spx-ssc-test
/tmp/spx-ssc-test
```
