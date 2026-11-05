# GPU 栈方向：KGSL + 闭源 GLES/Vulkan  vs  6.18 LTS + Mesa（2026-11-05）

**推荐：6.18 LTS + Mesa（分阶段）。不推荐移植 KGSL。**

## 1. 两条路各自要做什么

| | A. KGSL + 高通闭源 GLES/Vulkan | B. mainline 6.18 LTS + Mesa |
| --- | --- | --- |
| 内核 | 把 4.19.325（Quest 2 厂商树）的 KGSL 移植到 6.3.8/6.18：QFPROM、subsystem restart、QCOM devfreq governor、IOMMU/SCM/GMU/时钟/供电/总线投票；MSM 与 KGSL 不能同时接管 GPU，还要设计 KGSL + MSM DRM（仅显示）共存 | 整树升到 6.18 LTS，msm/a6xx、DPU/DP、音频、IPA、Venus 全部按 6.18 的绑定与 API 重验 |
| 用户态 | vendor gralloc.kona、ION/QTI、GSL、闭源 GLES/Vulkan，加上 vendor 服务；需要新写 gralloc/QTI 适配，因为本机是 minigbm + dmabuf heap | 现成 Mesa/freedreno/Turnip（26.2.4），上游持续修 |
| 可维护性 | 无上游；4.19 代码 + 闭源库，出问题只能自己啃（本仓库已经啃过：GSL 16 vs 24 字节 GEM_INFO、16384 字节 surface metadata、5528 字节 layout blob） | 上游 + 可改源码；本次 GMU 死锁之所以能分析到 HFI 队列级别，就是因为整条栈有源码 |
| 交付形态 | 必须打包 Quest 2 私有库 → 不能作为通用/可分发镜像（与项目"任意 SQ1/SQ2 同一镜像"目标冲突） | 可分发；私有输入仍按现有流程排除 |
| 工作量 | 按人月计，且第一版就要"显示 + GPU 双驱动共存"设计 | 按人月计，但每步可验证、可回退，且已有验证清单（见 KERNEL-6.18-FEASIBILITY） |

## 2. 关键事实：闭源用户态**不需要 KGSL** 也能跑

本仓库已有实测（`docs/QUEST2-GPU-RESEARCH.md`）：

- **闭源 Vulkan（v819.1）在原生 MSM DRM 上**跑通：计算、图像、三角形、AHardwareBuffer 读回、
  独立 APK 的 2880×1920 窗口呈现。
- **闭源 GLES（v77）作为 producer**，在系统 SurfaceFlinger（Mesa 合成）上出可见 BLAST 窗口，
  640×360 逐像素核对错误 0。
- 两者都在私有 namespace/探针内完成，正常桌面仍是 Mesa；没有替换 vendor、没有改启动镜像。

也就是说，"用上高通闭源驱动"这个目标，主要卡在 **gralloc/QTI/ION 的句柄与元数据契约**（用户态），
而不是"内核必须是 KGSL"。移植 KGSL 只能解决闭源库的"原生后端"，不能顺带解决 gralloc 契约，
却要付出双驱动共存 + 无上游维护的代价。

## 3. 与当前故障的关系

- 本次黑屏的链条（HFI `GX_BW_PERF_VOTE` 无应答 → GMU watchdog → RSC/CX 复位失败）在 **MSM/a6xx** 里，
  升级到 6.18 能拿到 2025 年的 PDC 序列、HFI 1s 超时、CX GDSC 强制复位（6.4 起）等修复；
  KGSL 是完全另一套 GMU/DVFS 实现，换过去等于把已知可分析的故障换成未知故障，且没有源码级取证。
- 频率方面：661/416 已回退并写入设备默认入口，与两条路线都兼容。

## 4. 建议的分阶段路线（B）

1. **现在**：保持 6.3.8，先做定点回移（CX GDSC 优先，其次 HFI 超时/PDC 序列），解决"卡死后能否自愈"；
   661/416 已生效（重启后验证）。
2. **中期**：按 `KERNEL-6.18-FEASIBILITY.md` 的清单把整树迁到 6.18 LTS，逐项重验 9 类功能；
   回滚靠保留现 ESP 入口。
3. **可选**：闭源 Vulkan/GLES 继续作为私有实验入口在 MSM DRM 上推进（gralloc/QTI 契约方向），
   但**不做 KGSL 内核移植**，也不把它作为默认或通用镜像的一部分。

## 5. 什么情况下应改选 A

只有在需求变成"单机私有、最高程度兼容高通闭源栈、接受不可分发与无上游维护"时才考虑；
即使那时也应先评估"KGSL ABI shim over MSM"（msm 仍是唯一 GPU owner，只补 `/dev/kgsl` 语义），
而不是直接搬 4.19 KGSL，因为后者还要重新做 GMU/时钟/供电/IOMMU 的板级适配。
