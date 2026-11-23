# SQ1/SQ2 LTE：产品路径

本文档取代旧 LTE 文档中的 Radio04 donor、单机序列准入、手工移植 FSG、Java NetworkAgent 与实验观测器说明。

## 运行链路

1. `scripts/build.py` 默认启用 LTE，共用源码与运行包；非 LTE Mesa 回退保持不变。
2. 默认 EFI router 在进入 EL2 前读取本机 SMBIOS UUID。若 `EFI/SPX/LTE/<uuid>.bin` 缓存有效则直接复用；缺失或损坏时通过原生 SCM `GET_INFO` 与经 CRC 检查的主 GPT 定位并读取本机 FSG，仅在启动介质保存私有缓存，不写物理基带 NV。格式容量上限为 2 MiB。
3. `lte-unit-seed` 校验本机缓存来源、长度与 CRC 后，原子更新 RAM 中的 `/run/rmtfs/modem_fsg`，保持原 MPSS、FS1/FS2 与启动顺序。
4. IPA4.1/DPM/WDA/WDS/RMNET 提供 IPv4 bearer；Radio AIDL 作为唯一控制者，由 Android 管理 SIM、网络、APN 与移动数据。AOSP APN 表及固定来源位于 `probe/lte/overlay/`，保留用户已有 APN 编辑。

## 构建输入与限制

- UUID 仅用于标识动态缓存来源，不能随镜像分发；CRC 仅用于检测损坏。
- 私有固件、驱动模块与 Binder 链接库需由操作者提供，缺少必要输入时构建失败。
- `ipa_sq2_lab.ko` 为当前实际驱动名。
- 当前支持 LTE IPv4 数据连接；不支持语音、SMS、IMS、eSIM、5G 与 IPv6。
