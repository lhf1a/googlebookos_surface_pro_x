# 内核升级到 Linux 6.18 LTS / msm 的可行性分析（2026-11-05）

结论先行：

- **单独把 msm（GPU/DRM 驱动）换成 6.18 不可行**。6.18 的 msm 依赖 6.8~6.17 才进入主线的核心设施
  （drm_gpuvm/drm_exec、drm_sched ops 结构、adreno catalog、DPU/DP 重写），在 6.3.8 上不是"改改就能编"，
  而是缺接口。下面第 4 节列了三条已在源码里核对过的硬依赖。
- **整树升级到 6.18 LTS 技术可行，但属"重建移植"级工程**：现有 9 类已实机验证的功能都要在 6.18 上重验，
  板级 DT、树外模块、LTE/IPA 补丁、显示与音频的机器特定修复全部要重做。
- **对当前黑屏，性价比最高的是"定点回移 a6xx 恢复路径"**（第 5.1 节），其中最关键的一项上游 v6.4 就有，
  6.3.8 恰好缺。

## 1. 当前内核是什么

| 项 | 现状 |
| --- | --- |
| 版本 | Linux **6.3.8**（mainline stable 基线，Makefile VERSION/PATCHLEVEL/SUBLEVEL=6/3/8） |
| 运行版本串 | `6.3.8-sq2-android1`（CONFIG_LOCALVERSION 注入） |
| 源码树 | 项目仓库 vendor 在 `work/sq1-autosuspend-owner-20261103/kernel`（无独立 .git）；Android 版另从 `/root/.cache/sq2-ksunext/kernel` 复制后打补丁 |
| 内核补丁集（相对 6.3.8） | screen-only、SPI-HID PM、adreno_gpu.c 的 zap/of_device_is_available 小改、EL2 context limit、SMMU SMR groups、audio sequencing（machine-only）、EL2 preboot；另加 KSU、KVM/vhost-vsock、netfilter、Venus media 配置 |
| 板级 DT | in-tree `sc8180x.dtsi`（相对 6.3.8 原始档约 70 行差异，保留 `.orig`）+ `sc8180x-surface-pro-x.dts` **1455 行** |
| 内核配置 | `configs/sq2-internal-kernel.config` 6896 行，约 2106 个 `=y/=m` |
| 树外模块 | `msm-edp-rates.ko`、`sq2_battery_compat.ko`、`qcom_tsens.ko`、USB 网卡模块等 |
| IPA/LTE | `patches/sq2-ipa-v4.1-lab.patch`（针对 6.3.8 的 drivers/net/ipa 子树） |

关键点：这个内核**不是**高通下游厂商内核，一直是 mainline + 项目自己的一小撮补丁。

## 2. 历史上"提取的高通驱动"是什么内核版本

按提取到的对象分类，避免混淆：

| 来源 | 版本/形态 | 性质 |
| --- | --- | --- |
| Quest 2 公开厂商内核树 | **Linux 4.19.325** | 唯一提取到的**高通内核驱动源码**；走 KGSL 路径，GPU 表含 A680。见 `docs/QUEST2-GPU-RESEARCH.md`、`docs/QUEST2-VENDOR-REFERENCE.md`。该树尚未证明与 OTA boot 逐字对应 |
| Quest 2 v77 vendor 分区 | 用户态（906 个普通文件、A650 固件） | 用户态 GPU 库/HAL/固件，**不含内核版本概念** |
| AdrenoTools v690 / v819.1 | 第三方重打包 Vulkan 包 | 用户态驱动，与内核无关 |
| Surface SQ2（Windows）驱动 | 二进制 + 表（如 `ipa_4_1_mem_part` 86 字段） | LTE/IPA 寄存器参考，无 Linux 内核版本 |
| mainline 参考 | 6.12（SC8280XP 频率档）、6.18（`qcom_tzmem.c`） | 仅作代码/绑定参考 |

也就是说：**"历史提取的高通内核驱动"= 4.19.325（Quest 2 KGSL）**；本机 GPU 从来没有跑过 KGSL，
一直是 mainline MSM/a6xx。这一点对下面的升级判断很重要——升级目标只能选 mainline msm，不能选厂商 KGSL。

## 3. 6.3.8 → 6.18 的 msm 差距（逐项核对过源码）

| 机制 | 6.3.8（现在） | 6.18（目标） | 引入版本 |
| --- | --- | --- | --- |
| GEM/VM | `msm_gem_vma`/`msm_vm` 自管 iova | `drm_gpuvm`/`drm_gpuvm_bo` + drm_exec | msm 于 **v6.17** 切（v6.16 的 msm_gem.c 仍是 0 引用） |
| 调度 | `drm_sched_init(&s, &msm_sched_ops, num_hw, 0, timeout, NULL, NULL, name, dev)` | `drm_sched_init(&s, &args)`（`drm_sched_init_args`） | **v6.15**（msm 侧；v6.14 仍是旧形式） |
| GPU 信息 | `adreno_info` 硬编码在 adreno_gpu.c | `a6xx_catalog.o` 等 catalog 拆分 | **v6.11** |
| 显示 | `disp/`、`dp/`、`dsi/`、`hdmi/` | 新增 `disp/dpu1/`，DPU/DP 大改（encoder 1:1:1、宽平面、msm_dp 重写） | 6.8~6.16 逐步 |
| a6xx 恢复：CX GDSC | 只有 `reset_control_reset(gx_collapse)` | genpd notifier + `dev_pm_genpd_synced_poweroff(cxpd)` + 1s 超时检测 "cx gdsc didn't collapse" | **v6.4 起就有**（v6.4/6.5/6.6/6.18 实测都在） |
| a6xx 恢复：PDC 序列 | 无状态位 | `GMU_STATUS_FW_START` / `GMU_STATUS_PDC_SLEEP` 跟踪 + force_off 末尾 `a6xx_rpmh_stop` | **仅 v6.18**（对应上游 2025-10 `f248d5d`） |
| HFI 应答超时 | 5 ms 轮询（`gmu_poll_timeout(...,100,5000)`） | 1 s，且感知 fault coredump 完成后重试 | **仅 v6.18** |
| A680 支持 | 有 | 有（`a6xx_catalog` 里 chip id `0x06080001`、revn 680） | — |
| GMU 固件协商 | supported_version 1.10 | 同 1.10 | 未变 |

补充：6.18 的 `a6xx_gmu.c` **没有** `secure_init`/`qcom_tzmem` 调用（那是 master/6.19+ 才有的），
所以"6.18 需要 tzmem"这个担心不成立；但走 6.19+ 时要重新评估。

## 4. 为什么"只换 msm"不成立

已在源码里核对的三条硬依赖（都在 6.3.8 之后才进入 mainline）：

1. `drm_gpuvm`/`drm_exec`：6.18 的 `msm_gem.c` 直接用 `drm_gpuvm_bo_*`、`drm_gem_for_each_gpuvm_bo`；
   6.3.8 的 drm 没有这套 API（msm 在 **v6.17** 才切换）。
2. `drm_sched_init()` 签名：6.18 传 `struct drm_sched_init_args`，6.3.8 传 9 个位置参数
   （msm 在 **v6.15** 改用它，v6.14 仍是旧形式）。
3. adreno catalog / `a6xx_catalog.o`：6.18 的 a6xx 代码按 catalog 里的 `adreno_info` 取值，
   6.3.8 的 a6xx 代码内联常量，结构体字段也不一样。

除此之外，msm 的 mmu/adreno-smmu、DP/DPU、devfreq/OPP、genpd、clk 绑定全部在这三个版本区间里动过。
把 6.18 的 msm 目录整体放进 6.3.8，等于同时把 drm core、iommu、clk、opp、display 子系统也搬过来——
那就已经是整树升级了。

## 5. 三条路线

### 5.1 A. 定点回移 a6xx 恢复路径（推荐，先做）

按收益排序，全部只改 `drivers/gpu/drm/msm/adreno/*`：

1. **CX GDSC 强制崩溃 + 检测**（上游 v6.4 就已存在，6.3.8 缺失）：在 `a6xx_recover` 里用
   genpd notifier + `dev_pm_genpd_synced_poweroff(gmu->cxpd)`，并检测 "cx gdsc didn't collapse"。
   这正好对应我们已经观测到的二次失败：候选补丁后 RSC 交换过了，但 2 秒后
   `GMU firmware initialization timed out`——CM3 没有被真正复位。
   依赖检查（实测）：6.3.8 已有 `dev_pm_genpd_synced_poweroff()` 与 `dev_pm_genpd_add_notifier()` 两个 API；
   但 `a6xx_gmu` 目前只挂接了 `gxpd`（`dev_pm_domain_attach_by_name(gmu->dev, "gx")`），**没有 `cxpd`、
   `pd_gate` completion 和 notifier 字段**，回移要一并补上（probe attach "cx"、recover 里 add/remove notifier、
   put_sync 后等 completion，超时打印 "cx gdsc didn't collapse"）。工作量不大，但比"只改 a6xx_recover"多几步。
2. **HFI 应答超时 5 ms → 1 s**（6.18）：避免把"慢但正常"的 GMU 应答误判为致命超时。
   单独不解决初始停摆（两次实测都是真的没应答），但能减少误判与后续连锁。
3. **PDC 序列状态位**（6.18）：即上一轮候选做过的那套；它修好了 RSC 交换，但没能救回 GMU。
   若要再做，必须与第 1 项配合，并保留 `FW_START` 的语义（候选当时把冷启动的 RSC 上电一起跳过是错的）。
4. **force_off 的 AHB fence/WRITEDROPPED 清理 + wmb**（6.18）：小改动，属"干净复位"的一部分。

风险：这一组只提升"卡死后能否自愈"，不保证消除初始的 GMU 停摆；需要实机复现验证。
成本：1~3 天/项，含构建与一次实机实验。

### 5.2 B. 全树升级到 6.18 LTS

需要重做的清单（每项都要实机验收，不只是编译通过）：

1. 板级 DT：1455 行 board DTS 迁到 6.18 的 `sc8180x.dtsi`（绑定变化：opp/icc/clk/phy/DPU）；
2. 显示：60Hz/65Hz 两个入口、eDP 链路速率（`msm-edp-rates.ko` 依赖 msm DP 内部结构）、EDID 覆盖；
3. 音频：ADSP "attached" 状态、WSA881x/SoundWire 停播爆音修复（`patch-sq2-audio-sequencing.py` 是机器特定的）；
4. 触摸：SPI-HID PM 补丁、iptsd 链路、edge ghost 修复；
5. LTE：IPA 4.1 lab 补丁要在 6.18 的 IPA 上重写（6.3.8 都没有 4.1 支持，6.18 结构又变了）；
6. 视频：Venus H.264 C2 + `sm_videocc` 时钟模块；
7. 电源/休眠：screen_only 策略、SPI/SSAM 唤醒、nohibernate 的整组行为；
8. KSU / AVF(KVM) / FUSE / USB 网卡等树外与配置项；
9. 工具链与配置：GCC 13.3 足够；6896 行自定义 config 要在 6.18 Kconfig 上重走 depend。

工作量（基于上面清单的估计，不是实测）：核心 bring-up 2~4 周；每类功能 3~7 天验证与修；
显示、音频、LTE 三项是高风险不确定项。合计按人月计，且期间设备只能在一个分支上跑，回退靠保留原 ESP 入口。

### 5.3 C. 中间 LTS（如 6.12）

msm 已是"catalog 有、gpuvm 无"的中间态，但 2025 的两项关键修复（PDC 状态位、1s HFI 超时）都**不在** 6.12；
CX GDSC 有。收益接近 A（CX GDSC 一项），成本却接近 B，**不推荐**。

## 6. 对当前黑屏问题的建议

1. 已在做：GPU 默认档回退 661/416（去掉未验证的 700/432），设备默认入口 DTB 已替换。
2. 下一步优先：A-1（CX GDSC），因为它对应我们唯一实测到的"恢复失败第二阶段"，且是上游已稳定一年多的代码。
3. 若 661 下仍复现、或 CX GDSC 回移后仍不能自愈：再加 A-3（PDC 序列，按 6.18 语义正确移植），
   同时补失败瞬间寄存器级取证（RSC status/ACK、RSCC SEQ busy、HFI 队列头、CM3 状态）。
4. 只有在"要做长期平台维护"的前提下才评估 B；不要为了这一个 bug 做整树升级。

## 7. 证据出处

- 版本/补丁集：`scripts/build-sq2-el2-android-kernel.sh`、`configs/sq2-internal-kernel.config`、本机内核树 Makefile。
- 提取来源：`docs/QUEST2-GPU-RESEARCH.md`、`docs/QUEST2-VENDOR-REFERENCE.md`、`docs/LTE-IPA41-REFERENCES.md`。
- 6.18 差距：对 torvalds/linux 的 v6.6/v6.9/v6.12/v6.13/v6.16/v6.17/v6.18 逐一比对
  `drivers/gpu/drm/msm/{Makefile,msm_gem.c,msm_ringbuffer.c}`、`adreno/{a6xx_catalog.c,a6xx_gmu.c,a6xx_gmu.h,a6xx_hfi.c,a6xx_gpu.c}`。
- 黑屏故障链：`out/sq2-black-screen-20261105/{FINDINGS.txt,VERIFICATION.txt,gpu661-revert/NOTES.md}`。
