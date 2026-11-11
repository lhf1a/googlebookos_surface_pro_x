# KGSL + DRM/SDE paused checkpoint — 2026-11-11

> **已恢复（2026-11-11）**：用户明确要求并行推进恢复工作后暂停解除，round1 已完成。
> 最新状态与证据见 `docs/KGSL-SDE-RESUME-20261111.md`、`out/kgsl-resume-20261111/CURRENT.json`
> 与 `out/kgsl-resume-20261111/sde-integration-18/`。本文保留为暂停时点的历史记录；其中
> “明确恢复后”步骤已被 round1 部分执行，MST 匹配内核与其余 6.18 API 迁移仍待续。

## 状态与本次保存边界

- 分支：`codex/kgsl-sde-port-v83`；目标 **paused**，三个子代理均中断。
- 本次只保存和提交，不恢复实验、不连接设备、不部署、不重启、不改默认发布 pins。
- 已集成 KGSL58（既有完整模块构建完成，未据此宣称实机通过）与 SDE17/315源。
- SDE BASELINE/MODIFIED/ROLLBACK 整体构建全部 exit2、25对象、0模块。
- 16组专项检查通过，额外真实 ARM64 framebuffer API probe 通过；317文件回滚/重应用精确。
- 全模块/MODPOST、物理显示、真实提交/fence/DDR、PM/性能、SQ1/SQ2 验收仍未完成。

机器可读版本、源配置及四角色哈希：`configs/kgsl-sde-checkpoint-20261111.json`。
最新证据仍在本地 `out/kgsl-resume-20261111/sde-integration-17`；历史30套四角色未替换。

## 未完成工作也已保存，但不应用

`patches/wip/kgsl-sde-paused-20261111/MANIFEST.json` 记录7组独立源码快照、52个修改文件、
精确父/候选SHA及patch SHA。每组patch均在单独父文件副本上以 `--fuzz=0` 重建并核对hash。
这只证明保存完整性，不证明未完成代码可编译或正确。不同父版/重叠文件不可盲目串联应用。

| 快照 | 状态 |
|---|---|
| irq | provider已写，Linux IRQ owner及prepared-vblank路径未完成 |
| mode-kernel | 基于integrated16的v2增量；catalog/bridge/全消费者未闭合 |
| mode-userland / mode-product | 对应源码/构建改动；库构建成功不等于联合运行验收 |
| mst-driver / mst-core | 保留fixed-primary身份的显式persistent生命周期扩展，未验收；需要新配对内核 |
| gem-owner | 先前中断的owner寿命候选，仅部分测试，不可视为安全unbind |

本地精确源快照及保存校验：`out/kgsl-resume-20261111/paused-save-20261111/LOCAL-SNAPSHOTS.json`。
未完成核验脚本亦保留本地原目录，公开主机checkers按已有路径提交；不得混称所有checker已绿。

## 明确恢复后

1. 先读 `out/kgsl-resume-20261111/CURRENT.json` 和此checkpoint，核对各候选源hash与当前分支。
2. 继续完整IRQ/vblank、mode与MST工作，按语义合并重叠的msm_drv/msm_kms/msm_atomic/CRTC字段。
3. 固定MST primary不能改成新connector ID假称兼容；新增core API必须构建匹配新内核，旧dev2不可混用。
4. 完整模块、GEM/provider退休及真机阶段仍是原目标，不以窄测试或保存成功替代。

私有库、固件、凭据、原始日志、编译载荷不入Git。此前六个rescue-console未跟踪文件不属于本次提交。
本次没有请求推送远端，因此只创建本地分支提交。
