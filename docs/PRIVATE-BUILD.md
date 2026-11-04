# 默认通用镜像：本地输入与源码构建

## 当前交付规则：以下私有包流程不得冒充通用发行

本页记录现有实验室输入流程。用户最新要求同一镜像可运行于任意 SQ1/SQ2，规则见
[DEVELOPMENT](DEVELOPMENT.md) 与根 AGENTS.md；目标唯一身份、凭据、FSG/NV/校准须
改为本机来源或首次配置，不得靠按设备替换本页私有输入重打包才能启动。
当前 runtime/donor/私有 addon 中的 SSH 身份仍需区分实验室输入与公共交付；LTE 已改为本机 EFI 自动 FSG 缓存，不再接受共享实验 FSG 输入。旧实验包仍保持私有，不得直接作为通用交付。没有本机调制解调器
数据时应安全降级该可选能力，不阻止基础系统启动。现有输入兼容/完整性与防误写检查仍保留。

标准入口：`python3 scripts/build.py --output /home/USER/spx-release`。
输出目录必须不存在。默认交付 `surface-pro-x-sq1-sq2.img.gz`，不是 Radio04 或裸 IMG。
默认构建会编译 Radio AIDL HAL、QMI data/mode、RMNET/本机缓存用户态助手、默认 EFI 采集 router 和 Radio 资源覆盖；
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
| `lte/` | `ipa_fws.elf`、`android-el2.dtb`（IPA DT）、与公共内核匹配的 `ipa_sq2_lab.ko`、`rmnet.ko`、从目标 Android 系统采集的 `libbinder_ndk.so`（只链接，不替换系统库）。 |
| `lte/local.keystore` | 首次构建生成的本地 Radio 覆盖签名密钥；后续保留复用，不提交 Git。 |

**2026-11-03 用户要求 SQ1/SQ2 共用 LTE，已解除旧绑定。** 两机默认项均加入 IPA DT，并保留各自 GPU/CPU 档位。LTE 不再核对 NVMe 序列号或固定 ESP 标记；仍拒绝未知机型，保留 EL2、screen_only、nohibernate、rescue/sq2.lte=0。
当前 LTE 首次启动自动读取本机 FSG，按本机 UUID 保存私有缓存，后续核验来源/CRC后复用；不要求逐机换文件重打包，也不写物理 NV。标准 APN来自随源码的 AOSP 表，Android保留用户编辑。SQ1 已验证自动采集/正常缓存启动及广电 IPv4 数据；SQ2 沿用既有 LTE 路径，新自动采集入口与完整新镜像双机全新启动未验收。Mesa 回退不加入 LTE，详见 [LTE-PRODUCT](LTE-PRODUCT.md)。

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
标准 LTE 包不内嵌 FSG。已有本地实验室镜像仍可能含专有固件和 SSH 身份，不应公开分发。源码编译/封装通过不等于 LTE、触摸或双机实测通过。
