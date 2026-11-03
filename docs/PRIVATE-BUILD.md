# 默认通用镜像：本地输入与源码构建

标准入口：`python3 scripts/build.py --output /home/USER/spx-release`。
输出目录必须不存在。默认交付 `surface-pro-x-sq1-sq2.img.gz`，不是 Radio04 或裸 IMG。
默认构建会编译 Radio AIDL HAL、QMI data/mode、RMNET 用户态助手、Java 辅助包和 Radio 资源覆盖；
不再要求预先提供 LTE03/Radio04 完整启动包。内核、固件与匹配驱动仍复用本地输入，不宣称全源码 AOSP/内核重编译。

## 私有输入目录

以下路径全部相对于项目根目录，统一放入已被 Git 忽略的 `private/build-inputs/`。
构建不按历史 SHA256 白名单拒绝私有输入；仍检查必需文件、格式、尺寸、启动钩子与封装一致性。
输出 SHA256 用于下载/解压核对；运行时模块/FSG 校验基于本次输入生成，并非历史输入门槛。

| 位置 | 必需内容与来源 |
|---|---|
| `template.img` | 本项目 20GiB、512 字节扇区的新数据分区模板；保留既有 GPT/分区 UUID、ESP 偏移14875099136、大小2147483648。不可放物理设备路径或任意其他磁盘镜像。 |
| `runtime/common/` | 同一基线的 `Image`、`android-el2.dtb`、`initramfs.cpio.gz`、`qebspilaa64.efi`、`tcblaunch.exe`。包含匹配模块/音频/视频/USB/KVM/KSU；不是 Radio04。 |
| `runtime/firmware/` | `qcom/msft/surface/pro-x-sq2/{qcadsp8180,qccdsp8180,qcmpss8180}.mbn` 及 `qcom/venus-5.2/venus.mbn`。来自操作者拥有的采集/已有工作运行包。 |
| `runtime/launchers/` | `closed.efi`、`mesa.efi`，与同一基线的 DT/EL2 handoff 对应。 |
| `runtime/sq1/profile-donor.cpio.gz` | 原 SQ1 模板的存储自扩展/SSH 身份输入；不取其中旧内核。含凭据，不公开。 |
| `ksunext/` | 操作者选择的 `ksud` 与 `manager-verified.apk`，与内核 KSU 接口兼容。 |
| `touch/` | 本项目触摸构建器的 `touch-bundle.json` 与 `payload/`；不放其他设备的坐标/噪声测量参数。 |
| `lte/` | 本机 `modem_fsg.bin`（2MiB）、`ipa_fws.elf`、`android-el2.dtb`（IPA DT）、与公共内核匹配的 `ipa_sq2_lab.ko`、`rmnet.ko`、从目标 Android 系统采集的 `libbinder_ndk.so`（只链接，不替换系统库）。 |
| `lte/unit.json` | 可选原单位配置：`{"unit_serial":"原NVMe序列号","operator_numeric":"真实MCCMNC","apn":"实际APN"}`。不得复制其他设备的身份。缺省不猜 APN；序列号字段仅兼容旧配置，不再用于启动准入。 |
| `lte/local.keystore` | 首次构建生成的本地 Radio 覆盖签名密钥；后续保留复用，不提交 Git。 |

**2026-11-03 用户要求 SQ1/SQ2 共用 LTE，已解除旧绑定。** 两机默认项均加入 IPA DT，并保留各自 GPU/CPU 档位。LTE 不再核对 NVMe 序列号或固定 ESP 标记；仍拒绝未知机型，保留 EL2、screen_only、nohibernate、rescue/sq2.lte=0。
本地 FSG 是原 SQ2 采集件，本轮按用户要求作为共享实验输入，仅复制到 `/run` 的 RAM shadow，不写物理 NV 分区。硬件相同不是跨设备配置已验证的证明。若拥有目标机自己的 FSG，应放入同一输入位置重新构建。未配置 APN 时需在系统中设置；不保证插卡即上网，SQ1 实机验证仍未执行。Mesa 回退不加入 LTE。

## 构建环境

WSL Ubuntu 24.04：Python3/python3-libfdt、gcc/g++、aarch64-linux-gnu 工具链、make、mtools、dosfstools、JDK、gzip。
公开工具依赖沿用现有缓存：`/root/.cache/sq2-graphics/android-ndk-r28c`、
`/root/.cache/sq2-graphics/sdk/{android-15,android-35}`、`/root/.cache/sq2-c2-hardware-interfaces`
（Android frozen Radio V3/common V2）、`/root/.cache/sq2-c2-native-reference`（binder 平台头文件）。
这些是公开源码/工具，不是需要用户采集的私有资料。GNU-EFI 用 `--gnu-efi /path/to/built/gnu-efi` 指定 ARM64 构建目录。

```sh
python3 scripts/build.py --gnu-efi /path/to/built/gnu-efi --output /home/USER/spx-release
```

同一镜像通过 SMBIOS 选择 SQ1/661MHz 或 SQ2/700MHz，原生60Hz、300DPI、EL2/KVM、KSU、触摸修复/首启校准；两项菜单为 Qualcomm 默认与 Mesa 回退。
20GiB 模板保留 data 首启扩容实现；这不是新实机验证。
解压后可用 Rufus 的 DD 方式写入已确认的目标盘；构建器不会写盘或重启。

## 清理与边界

压缩完成且解压读回一致后，默认清除本次输出目录中的裸 IMG、ESP、临时 EFI 对象和 profile 副本；保留压缩包、构建审计及日志。`--keep-work` 保留这些中间件。
编译缓存可供下次复用；需要回收时只删除本轮明确记录的生成目录，不删除 `private/build-inputs`、原始采集、其他任务输出或旧可用镜像。
本地镜像含固件、FSG 和 SSH 身份，不应公开分发。源码编译/封装通过不等于 LTE、触摸或双机实测通过。
