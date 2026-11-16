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
