# 内核整树迁移到 Linux 6.18 LTS（SQ2）：编译里程碑记录（2026-11-05）

状态：**核心编译通过（Image + 323 模块 + DTB）；首次默认启动在 EFI preboot 失败，握手 DTB 已修正部署，尚待复测。整树功能迁移未完成。**

本文只记录"源码/构建"层面已经完成和尚未完成的事，不构成任何功能已通过的结论。
用户指令为"整树迁移到 msm 6.18 的最新 LTS，先全部完成过编译，再真机启动 debug"，
最初编译阶段没有部署；随后已部署并发生首次启动失败。以下 §0 为最新确认状态，
§1–7 的阶段记录不能代替本节，也不能当作 Linux 已成功启动。

## 0. 当前检查点：EFI preboot Not Found（2026-11-05）

用户报告默认入口停在 `EL2 BOOT ERROR [preboot prepare]: Not Found (0x800000000000000E)`，
随后手动从回退入口恢复。本轮 SSH 只读确认运行 `6.3.8-sq2-android1`、cmdline 指向
`SQ2Fallback`；磁盘默认 `SQ2MesaDev/Image` 已是 `ec54afb3…f838b49` 的 6.18。
GRUB 也已改为两项、6.18 默认，故 §7 的“三项、默认未改”只是此前中间态，不再代表设备。
该错误发生在 qebspil 的 `StartImage`，尚未执行 Linux `LoadImage`，不能归因于 msm 或模块 ABI。

### 已完成的定点更正

- 实际默认 DTB `e91bd896…87387d53e` 仅带 6.18 板级资源，遗漏了 EFI 握手属性。
  `probe/virtualization/sq2-preboot-rmtfs.c:sq2_rmtfs_prepare()` 检查
  `/reserved-memory/memory@85500000` 的 `microsoft,el2-preboot-rmtfs`；缺失即返回
  `EFI_NOT_FOUND`。它在本次原 DTB 中确实缺失，三 DSP 的 opt-in/result 属性也全部缺失。
- 新增 `scripts/prepare-sq2-lts-el2-dtb.py INPUT_DTB FRESH_OUTPUT_DTB`，给匹配的
  Surface Pro X / SC8180X 新 DTB 添加 18 项配对 EFI ABI 属性：RMTFS/DSP opt-in、
  `qcom,broken-reset`、四节点的零值 complete/status、根节点 kernel-fdt 空槽、
  PAS trace 未执行哨兵、Venus metadata contract。**不预填认证成功，不复制旧 phandle/资源。**
  拒绝非匹配硬件、错误 RMTFS 边界/ownership、非 pristine 状态及覆盖已有输出。
- 属性级逐项比较：18 新增，0 个原属性改动或删除，0 个节点变化；6.18 的 reg、phandle、
  memory-region、firmware-name、OPP 全部保留。源 DTB 原字节不变。
- 使用真实 `sq2_rmtfs_prepare()` C 源码和真实 libfdt 做主机测试；仅 EFI 分配/缓存 API
  是主机桩，不执行 SCM。相同程序输入各 DTB：BASELINE `0x800000000000000e / exit14`，
  MODIFIED `0x0000000000000000 / exit0`，ROLLBACK `0x800000000000000e / exit14`。
  可执行 rollback 在独立副本恢复原 SHA；二进制 patch 重建和重新应用候选均字节一致。
  7 项单元测试通过。这不是 EFI 实机执行、DSP 认证或 Linux 启动通过。
- 动态核对远端挂载的 ESP/block sysfs、当前回退 boot 与原 SHA 后，只原子替换
  `/media/esp/EFI/SQ2MesaDev/android-el2.dtb`，新文件 88771 bytes，SHA256
  `6c6afe671f01324f895dc8da859b73fc54d6b5fde193abc00dbfa8f253b78e4f`。
  原件保留为同目录 `android-el2.dtb.before618-handshake-20261105-e91bd896`。
  完整远端回读与候选逐字节一致；默认 Image/initramfs/EFI、GRUB 和全部回退文件 SHA 未变。
  本轮 boot ID 未变，**没有重启、没有切换运行中图形后端、没有清理用户数据。**

### 验证角色与下一动作

固定四角色（后续扩展而不替换）：本仓库绝对路径
`C:\Users\Administrator\Documents\ChatGPT\chromebookOS\out\kernel-6.18-migration-20261105\`
下的 `MODIFIED_FILE.dtb`、`DIFF_FILE.patch`、`VERIFICATION.txt`、`ROLLBACK.sh`。
补充文件保存原 DTB、原/新 DTS、主机 C 程序与桩、完整部署前后输出及远端回读 DTB。

下一步是修正后首次启动复测（**尚未执行**）。假设：缺失的配对 EFI DT 握手是当前
`preboot prepare/Not Found` 的直接触发条件；变量仅为上述 DTB 属性。区分性观察：
是否越过 `preboot prepare` 到达 `Kernel LoadImage succeeded`，之后分别记录 PAS/TCB/Linux
的下一条原始状态。若再报错则保留完整阶段/状态码并回到原回退，不连续盲重启、不热重置 DSP。
在 TSENS/热区未接回前不做 GPU/CPU 压力验收；§5 其余迁移缺项全部仍未完成。

## 1. 基线与产物

| 项 | 值 |
| --- | --- |
| 内核基线 | `linux-6.18.60`（kernel.org v6.x 最新 6.18.y，2026-11-05 拉取） |
| 源码树 | `work/lts-6.18-20261105/src/linux-6.18-sq2`（ext4 工作区，未入 Git） |
| 对比基线 | `work/lts-6.18-20261105/base/linux-6.3.8`（纯净 6.3.8，用于逐文件枚举项目差异） |
| 构建输出 | `work/lts-6.18-20261105/build/6.18`（`O=` 分离构建） |
| 产物目录 | `work/lts-6.18-20261105/out/linux-6.18.60-sq2/`（含 SHA256SUMS 与打补丁后的源码 tar.xz） |
| 运行版本串 | `6.18.60-sq2-android1`（沿用 `CONFIG_LOCALVERSION="-sq2-android1"`） |
| Image | 34112000 bytes，sha256 `ec54afb324c00bc7a6e92117a33b05f8bc4256d97a016b8ab81abfb46f838b49` |
| board DTB | `sc8180x-surface-pro-x.dtb` 87709 bytes，sha256 `05043631e59306c046861f18972ec93e3d9f1b4f5c5737d24a22cd2de96f7264` |
| 模块 | 323 个 `.ko`；`sq2_battery_compat.ko` sha256 `b34553b6f6a7b5fef5345fbdf852de4fc74608f16f3f6a8ba4dd63ca47884f5d` |
| 编译告警 | `error: 0`，`warning: 0`（`make Image modules`，GCC 13.3 aarch64） |

工具链：Ubuntu 24.04 WSL，`aarch64-linux-gnu-gcc 13.3.0`，64 核并行。

## 2. 移植方式（可复现）

```
# 1) 6.18.60 + 项目 DTS
python3 out/_wsl/prep618.py         # 建立 6.18 工作树并对每个差异文件生成 6.3.8 侧补丁
python3 out/_wsl/setup_dts2.sh      # 把项目在 sc8180x.dtsi 上的音频增量补进 6.18 的 dtsi
# 2) 配置
configs/sq2-internal-kernel.config -> olddefconfig（6.18 Kconfig）
scripts/config -m SPI_HID -m RTC_DRV_SURFACE -m QCOM_LMH
# 3) 项目补丁（分两批，全部带 marker，可重复执行）
python3 out/_wsl/port618.py         # GPU/显示/音频/时钟/蓝牙/ashmem/spi-hid
python3 out/_wsl/port618b.py        # EL2 preboot 交接 + SMMU context pool
# 4) 构建
make -C TREE O=BUILD ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j64 Image modules
```

> `out/_wsl/` 在 `out/`（已忽略、不入 Git）下，是本次一次性使用的脚本；真正的改动已经
> 落在 6.18 工作树里，并通过 `out/linux-6.18.60-sq2/linux-6.18-sq2-patched-src.tar.xz` 归档。

## 3. 逐项移植结果

### 3.1 设备树

- 以 **6.18 自带的 `sc8180x.dtsi`** 为基线（6.3.8 时代那份是更早的上游回移，已过时），
  只把项目自己的 4 个 hunk 补上去：`slimbam`/`slim-ngd`、`apr`+`q6afe`+`q6asm`+`q6adm`、
  `sound` 节点与两个 dt-bindings include。
- `sc8180x-surface-pro-x.dts`（1455 行）与 `sc8180x-wcd9340.dtsi` 原样带入，只改 4 处 **USB PHY 标签
  改名**（6.18 上游改了 multiport PHY 的 label，地址也修正为 0x88e8000/0x88eb000）：
  `usb_mp0_hsphy→usb_mp_hsphy0`、`usb_mp1_hsphy→usb_mp_hsphy1`、
  `usb_mp0_qmpphy→usb_mp_qmpphy0`、`usb_mp1_qmpphy→usb_mp_qmpphy1`。
- `make dtbs` 全量通过，本板 DTB 无 DTC 告警。
- 6.18 的 `sc8180x.dtsi` 已自带 `mdss_edp`（`qcom,sc8180x-edp`）、`edp_phy`、`aux-bus` 结构，
  与板级 DTS 的用法兼容，eDP 面板节点未做结构性改动。

### 3.2 内核配置

- 6896 行项目配置经 `olddefconfig` 落到 7698 行。291 个符号被 6.18 移除（绝大多数是自动探测/
  内部符号，如 `AS_HAS_*`、`ARCH_HAS_*`、`CRC32_*` 备选），11 个符号取值变化
  （`ARCH_FORCE_MAX_ORDER 11→10`、`BASE_SMALL 0→n`、`CEC_CORE m→y` 等），均无构建影响。
- 新增/恢复：`CONFIG_SPI_HID=m`、`CONFIG_RTC_DRV_SURFACE=m`、`CONFIG_QCOM_LMH=m`。
- `CONFIG_QCOM_TSENS` 在 6.18 仍依赖 `NVMEM_QCOM_QFPROM`，而项目配置有意关闭 QFPROM，
  所以照旧走树外模块路线（见 §5，**尚未重建**）。

### 3.3 已移植的项目补丁（均在源码里留了 marker）

| 位置 | 内容 | marker |
| --- | --- | --- |
| `kernel/power/suspend.c` | screen-only 系统挂起隔断（只读模块参数） | `SQ2_SCREEN_ONLY_V1` |
| `adreno/a6xx_catalog.c` | A680 使用 `a680_sqe.fw`/`a680_gmu.bin` | `SQ2_A680_FW` |
| `adreno/a6xx_gpu.c` | ucode 版本检查接受 `a680_sqe.fw` | — |
| `dpu1/dpu_plane.c` | 去掉 `DRM_FORMAT_MOD_QCOM_COMPRESSED` 修饰符 | `SQ2_NO_UBWC_MODIFIER` |
| `drm/panel/panel-edp.c` | EDID 读失败重试 10×100ms；新增 `LP129WT112684` 面板条目 | `SQ2_EDID_RETRY` |
| `sound/soc/qcom/sdm845.c` | 停播前先静音 WSA PA（`--machine-only` 语义），两处 `sdw_disable_stream` 前 | `sq2_mute_sdw_speakers` |
| `clk/qcom/gcc-sc8180x.c` + dt-bindings | 补 `GCC_USB3_MP0/MP1_CLKREF_CLK`（0x8c000/0x8c004） | `gcc_usb3_mp0_clkref_clk` |
| `bluetooth/hci_qca.c` | WCN3998 重开时的异步 baudrate ack 丢弃（只在 HCI_INIT 期间） | `SQ2: consumed WCN3998` |
| `mm/{shmem_fs.h,memfd.c,shmem.c,Makefile}` + `mm/sq2-memfd-ashmem.c` | Android ashmem 兼容 ioctl（仅普通 memfd inode 生效） | `SQ2_ASHMEM_SHIM` |
| `drivers/hid/spi-hid/` | 项目自带的 spi-hid 驱动（含 `SQ2_SPI_PM_V1` 系统睡眠恢复补丁）；`strlcpy→strscpy` | `SQ2_SPI_PM_V1` |
| `drivers/rtc/rtc-surface.c` + Kconfig/Makefile | Surface SSAM RTC | `RTC_DRV_SURFACE` |
| `drivers/hid/surface-hid/Kconfig` | 依赖改为 `SURFACE_AGGREGATOR_BUS` | — |
| `efi/libstub/fdt.c` + `sq2-el2-progress.h` | EL2 preboot：把最终 DT 拷贝地址交给 qebspil；`sq2.el2_trace=1` 时的屏幕进度标记 | `SQ2_EL2_PREBOOT_DESTINATION` |
| `remoteproc/qcom_q6v5_pas.c` | EL2 下改为 **attach** 已由 EFI 启动的 ADSP/CDSP/MPSS（不重载固件、不 assign 内存、不回写 handover） | `sq2_preboot_requested` |
| `remoteproc/remoteproc_sysfs.c` | 已配对 EFI 固件时 `firmware` 不再显示 `unknown` | `sq2_preboot_firmware_known` |
| `soc/qcom/rmtfs_mem.c` | 保留 EL1 已完成的 RMTFS 归属，不在 EL2 里做权限回收 | `microsoft,el2-preboot-rmtfs` |
| `iommu/arm/arm-smmu/arm-smmu.c` + `sq2-el2-context-limit.h` | EL2 实验档的 IRQ-backed CB 池（仅 DT opt-in + 精确硬件断言） | `SQ2_EL2_IRQ_BACKED_CONTEXT_POOL` |

### 3.4 判定"上游 6.18 已有"而**不再**移植的（有源码证据）

方法：把每个 6.3.8→项目 差异里**新增行**（≥25 字符）到 6.18 里统计命中率；命中率高说明是
上游回移，直接丢弃，避免把 6.18 的新代码"改回旧版"。

| 项目补丁 | 6.18 缺失率 | 结论 |
| --- | --- | --- |
| `adreno_gpu.c` zap `of_device_is_available` | — | 6.18 已是该写法 |
| `interconnect/sc8180x.c` CO0 keepalive | — | 6.18 已有 |
| `arm-smmu-qcom.c` SMR groups（上游 5583a55e074b） | 14% | 6.18 已含上游版本 |
| `usb/dwc3/*`、`usb/gadget/*`、`usb/mtu3` | 70–100% 命中 | 上游回移，丢弃 |
| `platform/surface/aggregator/*`、`tabletsw`、`surface-hid` | 41–93% 命中 | 上游回移，只保留 Kconfig 依赖调整 |
| `efivarfs`、`efi/vars`、`ucs2_strlcpy` | 50–100% 命中 | 上游回移，丢弃 |
| `sc8280xp.dtsi`、`sa8295p-adp.dts`、`sa8540p-ride.dts` | 54–100% 命中 | 与 SQ2 无关，丢弃 |
| `qcom_scm.c` QSEECOM（420 行） | 路径变为 `drivers/firmware/qcom/` | 6.18 已有 QSEECOM/UEFISECAPP；本项目配置两项均为 `not set`，未移植 |
| `gcc-sc8180x.c` 的 `GCC_USB3_UNIPHY_*_BCR` | — | 6.18 已有；仅补 clkref 时钟 |
| `gcc-sc8180x.c` PCIe GDSC `ALWAYS_ON` | — | 6.18 已改为 `PWRSTS_RET_ON` + genpd 同步下电，暂不恢复该历史 hack |
| `drivers/base/dd.c` `fw_devlink.timeout` | — | 6.18 已重做 deferred probe/fw_devlink 时序；需要时再按 6.18 语义实现 |
| `drivers/kernelsu` | — | 树外 KSU；本轮未接（见 §5） |

## 4. 与 6.3.8 的行为差异（必须实机确认）

1. **GPU**：6.18 用 `drm_gpuvm`/`drm_exec`、`drm_sched_init_args`、`a6xx_catalog`，且有 CX GDSC
   强制崩溃 + PDC 状态位 + 1s HFI 超时。也就是 `docs/KERNEL-6.18-FEASIBILITY.md` 里 A 路线
   想回移的修复，现在都是原生的。661/416 的 OPP 来自板级 DTB，未改。
2. **显示**：DP/DPU 是 6.18 的重写版（`dp_display.c`/`dp_link.c`/`dp_panel.c`），`dpu1` 只保留
   "禁用 UBWC 修饰符"这一处项目改动。**eDP 链路速率表（`msm-edp-rates`）尚未移植**。
3. **音频**：只保留"停播前静音"的机器级补丁；WSA881x 驱动保持 6.18 原版（与 `--machine-only` 语义一致）。
   6.18 的 ADSP 仍由 EFI preboot 启动，Linux 只 attach。
4. **USB/存储**：dwc3/gadget 全部换成 6.18 上游版本（旧回移已丢弃），NVMe/USB 网卡/USB 存储的行为
   需要在实机上重验。
5. **Surface 子系统**：SSAM/tabletsw/battery/AC 用 6.18 上游版本 + 项目的 Kconfig 依赖调整。

## 5. 尚未完成（明确列出，不得当作已修复）

1. **eDP 链路速率表 / 65Hz 入口**：`scripts/patch-sq2-edp-rates.py` 是按 6.3.8 的 `dp_panel.c`/`dp_ctrl.c`
   SHA 钉死的，6.18 的 DP 驱动结构已变（而且项目里 `msm-edp-rates.ko` 是"复制整份 msm 再打补丁"的做法）。
   需要按 6.18 的 `dp_panel.c`（`DP_SUPPORTED_LINK_RATES` 目前在 6.18 msm 里 0 引用）重新实现，
   或改为直接给 in-tree `msm.ko` 打补丁。**本轮未做。**
2. **TSENS**：6.18 主线的 `qcom_tsens` 仍要求 `NVMEM_QCOM_QFPROM`，而项目配置故意关闭 QFPROM；
   历史上用 linux-surface `b86f786` 源码单独编 `qcom_tsens.ko`。该源码归档本机 `downloads/` 里不存在，
   **本轮没有重建该模块**；6.18 的 TSENS 驱动把 `qcom,tsens-v2` 作为通用匹配（`data_tsens_v2`，
   `hw_id = i`），理论上可工作，但未实机验证。
3. **KernelSU Next**：`drivers/kernelsu` 是树外源码，本轮没有接入；`CONFIG_KSU*` 因此为未设置。
4. **IPA 4.1 / LTE**：`patches/sq2-ipa-v4.1-lab.patch` 针对 6.3.8 的 `drivers/net/ipa`，6.18 的 IPA
   结构又变了，**没有移植**。EFI FSG 采集 -> RAM RMTFS -> Radio 的路径本身在用户态/EFI 层未改，
   但内核 IPA 侧仍缺。
5. **Venus / QCA / USB 网卡等树外模块**：`CONFIG_VIDEO_QCOM_VENUS=m`、`SM_VIDEOCC_8150=m` 等按 6.18
   配置已编译通过；但项目里对 Venus system PM、QCA 失败清理、GENI TX 完成的**机器级补丁**未移植。
6. **休眠策略**：`suspend.screen_only` 隔断已移植；`nohibernate`、SPI/SSAM 唤醒、
   `patches/surface-pm/*`、`patches/sq1-suspend-wip/*` 未在 6.18 上重验。
7. **真机**：**完全没有启动过**。EL2 preboot 交接只在源码层面移植，qebspil 的 DT 属性路径
   （`/soc@0/remoteproc@17300000`、`@8300000`、`@4080000`、`/reserved-memory/memory@85500000`）
   已用 `dtc` 解出确认存在于新 DTB 中，但没有实机证据。

## 6. 下一步（真机 debug 顺序建议）

1. 组一个**独立 EFI 入口**（保留现有 6.3.8 默认/回退入口不动），payload = 新 Image + 新 DTB
   + 新 initramfs（模块目录按 `6.18.60-sq2-android1` 重排）+ 原有固件。
   现有 `scripts/build-source-boot.py`/`standard` 组包链是按 6.3.8 路径写的，需要先确认它如何取
   `Image`/DTB/模块，再决定是复用还是写一个 6.18 专用打包器。
2. 首启只看三件事：串口/屏幕是否进内核、`dmesg` 里 `msm` 是否 probe 成功（GMU 不再 watchog）、
   是否挂到 root。EL2 preboot 的 `microsoft,el2-preboot-complete` 是否被 qebspil 写入要在
   `/proc/device-tree` 里核对。
3. 起来后再按 §5 顺序补：eDP 速率 -> TSENS -> IPA/LTE -> KSU -> 树外模块。

## 7. 已部署的首启候选（2026-11-05 中间态，后续状态以 §0 为准）

### 7.1 ESP 整理（按用户指令"只保留 mesa dev 和 vulkan rollback，其他全删"）

删除 `/EFI/` 下 10 个实验目录：`SQ2Default`、`SQ2Free`、`SQ2GMUFix`、`SQ2LTE`、`SQ2Power`、
`SQ2U4`、`SQ2Vb3`、`SQ2Vp2`、`SQ2Vpm`、`SurfacePmAuto`（共释放约 1.48 GB）。
保留：`BOOT`（GRUB 主入口）、`SPX`、`SQ2Buttons`（EFI 引导器副本，2 MB，**没有当实验目录删**，
避免动到固件可能指向的引导路径）、`SQ2MesaDev`（默认入口）、`SQ2Fallback`（回退入口）。
整理后 ESP：2.0 G 用 531 MB（26%）。

### 7.2 新入口 `/EFI/SQ2Lts618`（手动选择，默认未改）

| 文件 | 大小 | sha256 |
| --- | --- | --- |
| `Image` | 34112000 | `ec54afb3…f838b49` |
| `android-el2.dtb` | 87709 | `e91bd896…87387d53e` |
| `initramfs.cpio.gz` | 199546708 | `73d93040…b198a7126` |
| `start.efi` / `el2launch.efi` / `qebspilaa64.efi` / `tcblaunch.exe` | 与原入口相同 | 从 `SQ2MesaDev` 原样复制 |
| `kernel-options.txt` | 449 | initrd 指向 `\EFI\SQ2Lts618\initramfs.cpio.gz`，其余选项与 MesaDev 一致 |

GRUB 追加菜单项（`/EFI/BOOT/grub.cfg`，原文件备份为 `grub.cfg.before-lts618-20261105`）：

```
menuentry 'SQ2 Linux 6.18.60 LTS (experimental)' --id sq2-lts618 {
 search --no-floppy --fs-uuid --set=root B027-0512
 chainloader /EFI/SQ2Lts618/start.efi
}
```

`set default=sq2-default` 未改动，超时后仍进原 6.3.8/Mesa 入口；要试 6.18 需在菜单里选第三项。

### 7.3 initramfs 的做法（可复核）

没有重建 initramfs，而是**只替换模块树**：解压部署版（422.7 MB，5 个 cpio TRAILER），
逐个删除 `lib/modules/6.3.8-sq2-android1/**`（532 条）条目，在原位置插入
`lib/modules/6.18.60-sq2-android1/**`（478 条：143 目录 + 335 文件，含 `depmod` 生成的
`modules.dep/alias/builtin/order`），其余字节原样保留。

校验结果：**非模块条目 515 条，名称 + sha256 + 大小 + 权限逐条与原文件一致**；
TRAILER 数量 5→5；旧 release 残留 0；新树内 `msm.ko`、`spi-hid.ko`、`hci_uart.ko` 均存在。

注意：该 cpio 的 newc 头把 header+name 对齐到 4 字节（不是常见的 header+name+4-byte-pad），
直接按标准公式解析会整体偏移 1 字节。`out/_wsl/repack618c.py` 用的是修正后的偏移。

### 7.4 与工作配置对齐的一处 DT 修正

`zap-shader` 在 6.18 的 `sc8180x.dtsi` 里**没有节点**，而板级 DTS 只补 `memory-region`/
`firmware-name`，于是默认变成 `okay`；但本机没有签名的 zap 固件（rootfs 的 `lib/firmware/qcom`
只有 `a680_gmu.bin`/`a680_sqe.fw`），部署版 6.3.8 DTB 里该节点是 `status = "disabled"`。
已在板级 DTS 显式加 `status = "disabled"`，DTB 重新生成并重新上传。

### 7.5 首启已知差异（预期内，不是回归）

- **GPU 上限 514 MHz**：661/416 来自项目自己的 GPU profile DTB 合并步骤
  （`scripts/sq2-gpu-boot-payload.py`，其输入 DTB 是 6.3.8 时代的），本次用的是 6.18 直出的
  板级 DTB，`gpu_opp_table` 最高 `opp-514000000`。首启不追求频率，先看能否起来。
- **没有 `qcom_tsens.ko`**：`init` 里 `insmod /lib/modules/6.3.8-sq2-android1/.../qcom_tsens.ko`
  会失败，但随后的保护条件是"DT 存在 `opp-661000000` 才算失败"，新 DTB 没有该节点，所以不会
  触发 `fail gpu-tsens-module`。热区会缺失，AOSP 可能报 thermal HAL 症状。
- **不含** LTE/IPA、Venus 视频覆盖、KSU、closed Vulkan 的额外内核模块（这些模块在 initramfs
  的 `etc/sq2-*` 里仍是 6.3.8 ABI，`modprobe`/`insmod` 会被内核以 vermagic/ABI 拒绝，
  属预期失败，不影响 6.18 自带路径）。

### 7.6 回滚

默认入口与回退入口完全未改，直接选菜单第二项（或等 10 秒默认）即可回到 6.3.8。
彻底移除：`rm -rf /media/esp/EFI/SQ2Lts618`，并把
`/media/esp/EFI/BOOT/grub.cfg.before-lts618-20261105` 还原为 `grub.cfg`。

## 8. 限制声明

- 本文的"完成"只指 **`make Image modules dtbs` 通过**（0 error/0 warning），不是功能验收。
- 已部署且默认入口首次启动在 EFI 失败；用户从 6.3.8 回退恢复。本轮仅修正 DTB、未再重启，
  仍无 6.18 Linux 真机启动成功结果。
- 单机编译通过不能推出"任意 SQ1/SQ2 通用"；board DTS 仍是 SQ2 专用输入。
- 6.18 的 msm 是**代码更替**而非"补丁叠加"，GPU/显示/音频/LTE 的回归风险由实机验证承担。
