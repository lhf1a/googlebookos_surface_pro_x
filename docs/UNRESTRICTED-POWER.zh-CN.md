> 旧诊断工具 `lte-fs12-seed`、`lte-fsg-seed` 与 `el2-preboot-init` 已退役，当前实现见 [SUSPEND](SUSPEND.zh-CN.md)。

# screen_only 依赖整理与无限制环境

## 机制与范围

- `scripts/build.py` 与 `build-surface-pro-x.py` 默认策略为 `unrestricted`，显式传递 `screen_only=0`、`screen_only_autosuspend=0`。
- 内核原有 owner/一次/期限准入不再参与拦截，`init` 不运行 owner 注册与保活，手工 `PM_DEBUG` 阶段测试不接入正常启动。
- 普通模块不再以 `screen_only=Y` 为前提，涵盖 LTE/Radio、视频 Codec2 及采样工具、USB、KSU、swap、EL2/AVF/Terminal、图形探针、CPU/GPU 测试入口与显示/GPU 打包检查。
- 保留真实硬件/ABI/固件哈希/磁盘边界/热保护校验、凭据、用户设置、标准 Android 合法 wakelock、`KEEP_SCREEN_ON`、`offscreen_only`、`nohibernate` 以及原 Mesa 回退。

## Codec2 与环境构建

- 配对源码 `probe/video/sq2-c2-runtime-launch.c` 使用 Android NDK API 35 编译，显式传入 `-DBASE='"/data/vendor/sq2-codec2/v2u/"'`，产物作为 `--codec-launcher PATH` 输入；仍保留原 private mount namespace、mapper 与 `MAP_CACHE` 契约，并保留/校验旧 `v1`。
- 若旧包存在固定 `VIDEOCC` b5 门，需显式提供 `--video-reference-module PATH`，使用 `build-sq2-hfi-overlay.py` 的动态引用模块；组包保留 compatible/cell 宽度/ABI/哈希/固件防重复启动校验，缺失匹配输入时拒绝构建。
- `scripts/prepare-unrestricted-environment.py` 离线构建增量环境，需使用操作者自己的完整基线、私有身份与匹配 kernel/module bundle。新 EFI 通过原 initrd + 覆盖层加载，最终有效 cpio 逐项一致且原身份成员保留在最后。

## 原引用文件分类

| 文件 | 类别 | 处理 |
|---|---|---|
| `configs/deployment-status.json`、`configs/sq2-video-hfi-status.json` | 历史状态 | 保留旧观测 |
| `configs/grub-internal-ssd.cfg`、`configs/grub-source.cfg`、`configs/suspend-known-issues.json` | 功能/构建入口 | 已解除必须 Y |
| `patches/sq1-suspend-wip/*`、`patches/surface-pm/*`、`patches/suspend-el2-trial/*` | 历史/兼容内核补丁 | 保留供回退复现；N 绕过全部门 |
| `probe/graphics/vulkan-readback.c` | `offscreen_only` | 保留离屏渲染模式 |
| `probe/graphics/quest2-*`、`probe/lte/*`、`probe/test2/*`、`probe/video/*`、`probe/virtualization/*` | 功能/构建入口 | 已解除必须 Y |
| `scripts/check-sq1-autosuspend-policy.py`、`scripts/check-sq1-suspend-wip.py`、`scripts/check-surface-native-once.py`、`scripts/check-suspend-trial-policy.py`、`scripts/patch-sq2-screen-only.py`、`scripts/prepare-surface-pm-debug.py`、`scripts/smoke-sq2-ksunext.py`、`scripts/smoke-sq2-screen-only.py` | 旧诊断/状态工具 | 不接入新入口；原语义保留供回退验证 |
| 其余 `scripts/*` 功能与打包脚本 | 功能/构建入口 | 已解除必须 Y |
| `tests/*` | 测试 | 保留兼容与完整性验证；已更新 N 成功预期并保留拒绝场景 |
