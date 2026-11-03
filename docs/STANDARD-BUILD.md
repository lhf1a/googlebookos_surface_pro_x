# 项目标准构建入口

项目标准入口是 `python3 scripts/build.py`，输出**单个 SQ1/SQ2 通用原始磁盘镜像**，不是两份独立镜像。

默认基线（不是必须满足的能力校验项）：
- SQ1：GPU 661MHz；SQ2：GPU 700MHz。
- 两机：原生 60Hz、2.7Gbps × 4，KSU、EL2、首次触摸校准。
- Android 默认 **320 DPI**，`--dpi N` 保留自定义；只改默认属性，不覆盖 userdata 中的显示设置。
- KernelSU Next 管理器/ksud 默认 `--ksu-manager on`，输入目录 `out/ksunext` 可用 `--ksu-bundle DIR` 替换，`--ksu-manager off` 不注入首启安装器（不移除内核 KSU）。使用历史验收的官方 3.2.0 / 33129 固定文件，不追随 latest，不降级现有安装，不预装 Zygisk/伪装模块或自动授予应用 root。
- 以 main fee67b2 的匹配内核/驱动和完整957b组件为底；默认项加入最新 `0100429` 的已验证 LTE/Radio 专属增量，Mesa 保持原非 LTE 回退。
- LTE 默认 `--lte on`，需精确私有 Radio04 输入；`--lte off` 保留关闭选择。仅原绑定 SQ2 可激活，SQ1/其他设备不移植 FSG，详见 [LTE 默认基线](LTE-BASELINE.md)。
- 通用触摸修复默认 `--touch-repair on`：状态一致性、按原始信号核心的连续性修复与统一校准向导。算法不区分 SQ1/SQ2，测量结果按设备保存；`--touch-repair off` 保留旧包选择。
- 默认触摸包目录 `out/touch-baseline`；可用 `--touch-bundle DIR` 指定当前源码匹配包，缺包或过期直接拒绝。第二层需用户在向导中采集、复测确认才保存参数；本轮尚未实机验收。

`--profiles CUSTOM.json` 保留自定义频率配置；`--sealed-input` 选择本地固定输入。
更换内核功能、显示/固件等组件时，保留既有组件构建和 `--rebuild-components`、`--legacy-el1` 路径；需要配套输入及清单，不会假称修改配置文本就改变已编译内核。
文件哈希、模块ABI、存储身份和拒绝误写物理盘属于安全/一致性保护，不是强制采用默认能力的门槛。

```sh
python3 scripts/build.py --template TEMPLATE.img --gnu-efi BUILT_GNU_EFI --lte-bundle PRIVATE_LTE_DIRECTORY --output FRESH_OUTPUT
```

同一镜像包含共用内核/固件、两机DT，以及默认/回退各自跨机型共用的initramfs。两个菜单项为高通默认和Mesa回退；EFI根据SMBIOS的SQ1/SQ2身份路由，未知身份拒绝，不猜测机型。保持SQ1/661和SQ2/700的差别。
这不是已完成双机实测的声明：本轮没有硬件、虚拟机或功能测试，没有部署。镜像带私有实验室凭据，不公开分发。
`build-source-boot.py` 是底层启动包/组件兼容入口；**对外标准整镜像入口只有 scripts/build.py**。

## 编译默认通用触摸包

使用已固定的 IPTSd v3.1.1 源码归档、Meson 依赖和 ARM64 cross-file，离线编译。
输出目录必须不存在；更新时使用新目录并通过 `--touch-bundle` 指定，勿覆盖旧验收包。

```sh
python3 scripts/build-unified-touch.py --archive IPTSD_V3_1_1_TAR_GZ --dependencies MESON_SUBPROJECTS --cross-file ARM64_CROSS_FILE --output out/touch-baseline
python3 scripts/build.py --template TEMPLATE.img --gnu-efi BUILT_GNU_EFI --lte-bundle PRIVATE_LTE_DIRECTORY --output FRESH_OUTPUT
```

同一包进入两机的默认/回退载荷，不按机型选择触摸算法。包不含任何设备的
已测坐标、静置行/列或点触参数。默认能力设置仍可显式关闭/替换；文件完整性
校验不是强制机型参数。`--touch-repair off` 只回到原基础触摸版本，不宣称禁用
所有历史硬件 quirk。第二层的证据与实机待验收项见 TOUCH-NOISE-AND-CALIBRATION。

## 准备固定 KernelSU Next 用户态

只下载官方历史验收版本，不自动获取 latest。二进制留在忽略目录，不提交 Git。

```sh
mkdir -p out/ksunext
curl -fL https://github.com/KernelSU-Next/KernelSU-Next/releases/download/v3.2.0/KernelSU_Next_v3.2.0_33129-release.apk -o out/ksunext/manager-verified.apk
curl -fL https://github.com/KernelSU-Next/KernelSU-Next/releases/download/v3.2.0/ksud-aarch64-linux-android -o out/ksunext/ksud
printf '%s  %s\n' 96c2bbbf1b973461fe82dd1ed17f89deb86a6a5a9d7c4cf079bd32091131ef57 out/ksunext/manager-verified.apk bb231b0f77d6ee78a4362ff751b3cc54c934293a4b0acde177afa59c4a64da00 out/ksunext/ksud | sha256sum -c -
```

首启安装器保留 `sq2.ksu=0` / rescue 跳过，验证只关屏 guard；等待 Android
boot completed 后，先检查内核接口版本再安装缺失的 ksud。已有 ksud 不覆盖。
管理器优先复用已安装的版本，向现有用户执行 install-existing；不创建用户。
模块生命周期在后续启动可用，不把包内置等同于本轮实机 root 验收。
官方来源：[KernelSU Next v3.2.0](https://github.com/KernelSU-Next/KernelSU-Next/releases/tag/v3.2.0)。

本次离线检查包含默认/自定义密度、保留用户设置、KSU 首启/重复启动/多用户/关闭、
四个有效载荷中的共同文件及源码副本回滚。四载荷组包显式关闭 LTE，仅因当前
缺私有 Radio04，不修改默认 on。未重建整盘镜像，也未部署设备。
