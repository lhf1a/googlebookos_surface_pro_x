> 当前唯一编译入口已切换至 6.18 LTS，见 [BUILD](BUILD.zh-CN.md)；默认项已接入 LTE/Radio 增量，Mesa 回退保持非 LTE，详见 [LTE 当前路径](LTE-PRODUCT.zh-CN.md)。下方旧 6.3.8 构建命令仅作历史参考。

# SQ1 / SQ2 统一构建

## 平台输入与决策

| 输入 | 观察 | 决策 |
| --- | --- | --- |
| ESP | Windows FAT UUID `2010-259C`，256GB GPT | SQ2 的 `B027-0512` 无法定位；不将 Windows ESP 写入 Android 安装器 |
| Android 布局 | SQ1 Windows 磁盘无 Android 分区 | SQ1 构建面向离线 20GiB Android 镜像模板 `B026-1027` |
| 平台 | SC8180X 系列；IORT `acfe9033…` 与 SQ2 一致 | 共用平台实现 |
| TCB | `195997cf…`，与固定 TCB 一致 | 复用固定输入 |
| MPSS / CDSP | `9ec7b6c6…` / `2a276b79…` 与 Linux 固定版本一致 | 复用固定二进制 |
| ADSP | Windows `09c06fe8…` 跨度 28MiB，DT 预留区 26MiB | 保留适配的 Linux `2703ceb6…` 二进制 |
| 视频 | Windows `891997ec…` 不同于 Linux `4e93d345…` | 保留 Linux42 固件与 no-TZ 运行时 |
| 固件版本 | SQ1 UEFI `4.234.140` | 仅作版本记录 |

原 SQ2 DT 保持不变。SQ1 衍生候选仅修改机型标签与频率策略：移除 GPU700 和两个 >3GHz CPU OPP，改用 GPU661/RPMh416 与大核最高 2995.2MHz；保留内存、固件名称、SMMU/中断/拓扑及温控 trip 均保持一致。

## 构建入口与共享基线

```sh
python3 scripts/build-source-boot.py --machine both \
  --sealed-input private/unified-input --output out/unified-boot
```

`--machine sq1` 和 `--machine sq2` 选择单个配置，默认为 SQ2；`--rebuild-components` 转发至主线组件重建后端，`--legacy-el1` 保持不变。

两个配置基于同一套 initramfs `957b5ee6…` 与 Image `c1ef559f…`，包含全部 USB 模块、USB 输入、Wi-Fi/蓝牙策略、FUSE、screen-only、音频 `92852a7b…` + WSA881x `b165ca2c…`、HAL `774cd7e7…`、no-TZ 视频、KSU/KVM/AVF 准备、手动 Terminal、温控/电池/触控修复以及 Qualcomm Vulkan / Mesa 双选。SQ1 沿用同一内核/模块 ABI，不集成 LTE 实验驱动或实验 DT。

SQ1 donor 经过哈希固定，仅用于其 Android 镜像存储/扩容实现与实验 SSH 身份；有效身份字节保留在最后一个仅凭据成员中。SQ2 保留现有 `user10` 自动登录，SQ1 不假定该账号存在。每个配置生成默认 Qualcomm Vulkan 与 Mesa 回退两个入口，启用 `sq2.el2=1`、原生 60Hz 与 screen-only/nohibernate；SQ1 启动器副本仅做等长目录 token 替换。

## 输出

每次输出包含 `SQ1/` 和/或 `SQ2/`、一对 EFI 载荷、固定固件、`grub.cfg`、`SHA256SUMS` 与构建审计。产物包含私有固件和实验身份，必须留在被忽略的 `private/` / `out/` 中；构建不会修改既有 20GiB 整盘镜像或替换共享 GRUB 固件。
