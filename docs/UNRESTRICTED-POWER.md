> 2026-11-04更新：下文lte-fs12-seed、lte-fsg-seed及el2-preboot-init相关旧诊断工具已退役；引用仅为历史记录，当前实现见docs/HANDOFF.md顶部。

# screen_only 依赖整理与无限制环境

## 2026-11-04 最终部署补充

已实机启动并成为新默认，原Mesa回退/原正常包/共享固件保留。实际N/autoN、owner0、
test/trial=N、pm_test none和22条有效hash通过，用户确认视频正常。完整经过和失败/回滚
见HANDOFF最新节。旧段落的15条目/未部署属于早期构建，不是当前状态。

必须审计嵌套tar及清单依赖。旧runtime.tar/start.sh仍拒绝N，现移到独立v2u，
保留/校验旧v1。新launcher仍采用原private mount namespace、mapper和MAP_CACHE契约，
只以编译时BASE选择新版本；codec service/mapper没有换成软件解码器。
配对源码probe/video/sq2-c2-runtime-launch.c使用原Android NDK API35编译，
显式参数 -DBASE='"/data/vendor/sq2-codec2/v2u/"'；结果作为--codec-launcher PATH输入。

若旧包仍有固定VIDEOCC b5门，必须显式提供--video-reference-module PATH，
使用build-sq2-hfi-overlay.py的动态引用模块；本机复用历史已验证86b6acbc…输入。
组包保留compatible/cell宽度/ABI/hash/固件防重复启动校验，不能换成另一数值白名单。
没有匹配输入则拒绝，不生成又一个缺硬解的“无限制”包。

合盖不在已验收范围：当前lid_behavior=0/NONE，无SW_LID输入能力，只有tablet-mode。
当前挂起success仍0。关闭实验限制不等于禁用合法锁、改变用户合盖设置、消除真实
挂起黑屏或证明多轮稳定。本次未自动发起真睡。

## 范围与事实

用户已确认关闭的是 `suspend.screen_only`。字面 `screen_on` 精确符号检索为0；
`KEEP_SCREEN_ON`有2处，是真实显示/应用逻辑，不删除。原筛选211行/102文件包括测试、
历史补丁和1个`offscreen_only`假阳性，不等于102个运行模块都有限制。

新环境明确传`screen_only=0`、`screen_only_autosuspend=0`。内核原有owner/一次/期限
代码不再参与准入，init不运行owner注册与lab保活；PM_DEBUG的手工阶段测试并未接入
正常启动。没有使用伪造sysfs挂载冒充N，也没有仅改日志标记。

功能范围：LTE/Radio、视频Codec2及安装/采样工具、USB、KSU、swap、EL2/AVF/Terminal、
图形探针、CPU/GPU测试入口、显示/GPU打包检查。普通模块不再以screen_only=Y为前提。
保留真实硬件/ABI/固件hash/磁盘边界/热保护、凭据、用户设置、标准Android合法wakelock、
显式功能opt-out和原Mesa回退。保留nohibernate；这不是磁盘休眠支持声明。

## 交付与验证

- `scripts/build.py`和`build-surface-pro-x.py`默认策略为`unrestricted`，旧策略需显式选择。
- `scripts/prepare-unrestricted-environment.py`离线构建增量环境；必须使用操作者自己的
  完整正常基线/私有身份与匹配kernel/module bundle，不能把本机私有包分发给其他设备。
- 新EFI通过原正常initrd + 覆盖层加载；实际EFI stub会顺序拼接多个initrd参数。最终
  有效cpio逐项相等，原身份成员逐字保留且最后；当前视频修正版仅显式更换已验证HFI overlay，其他模块和DT/固件不变。
- 实际内核QEMU同一输入root、owner0、freeze/freezer阶段3次请求：Y拒绝/N通过/回滚Y拒绝。
  这是准入测试，不是深睡驻留或物理多轮稳定性。
- 功能脚本N/缺失标记的路径与其他完整性拒绝场景由隔离fixture执行。图形探针源码
  已去掉Y检查；应从新工作区重建运行，不篡改用户数据目录里已有的历史探针二进制。
- 黑屏、早醒、长时间/多轮恢复、能耗和SQ1仍须单独验收，不把此环境清理当根因修复。

## 全部原引用文件

| 文件 | 原引用数 | 类别 | 处理 |
|---|---:|---|---|
| `configs/deployment-status.json` | 1 | 历史状态 | 保留旧观测，不能篡改成实测N |
| `configs/grub-internal-ssd.cfg` | 2 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `configs/grub-source.cfg` | 3 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `configs/sq2-video-hfi-status.json` | 1 | 历史状态 | 保留旧观测，不能篡改成实测N |
| `configs/suspend-known-issues.json` | 2 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `patches/sq1-suspend-wip/01-debug-whitelist.patch` | 11 | 历史/兼容内核补丁 | 保留供旧入口/回退复现；N绕过全部门 |
| `patches/sq1-suspend-wip/03-prefix-policy.patch` | 1 | 历史/兼容内核补丁 | 保留供旧入口/回退复现；N绕过全部门 |
| `patches/sq1-suspend-wip/04-single-owner-trial.patch` | 7 | 历史/兼容内核补丁 | 保留供旧入口/回退复现；N绕过全部门 |
| `patches/surface-pm/01-framework-owner.patch` | 8 | 历史/兼容内核补丁 | 保留供旧入口/回退复现；N绕过全部门 |
| `patches/surface-pm/02-native-single-request.patch` | 4 | 历史/兼容内核补丁 | 保留供旧入口/回退复现；N绕过全部门 |
| `patches/suspend-el2-trial/01-debug-single-owner.patch` | 16 | 历史/兼容内核补丁 | 保留供旧入口/回退复现；N绕过全部门 |
| `probe/graphics/quest2-composition.cpp` | 2 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/graphics/quest2-hwui/Sq2Quest2Hwui.java` | 2 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/graphics/quest2-readpixels.c` | 2 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/graphics/quest2-visible/Sq2Quest2Visible.java` | 2 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/graphics/vulkan-readback.c` | 7 | 无关：offscreen_only | 保留离屏渲染模式，不是休眠门 |
| `probe/lte/lte-framework-boot` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/lte/lte-unit-guard` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/lte/lte-unit-seed` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/lte/radio-stage` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/framework-suspend-stage` | 3 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/framework-suspend-start` | 3 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/ksu-framework-boot` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/lab-keep-awake` | 2 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/lte-fs12-seed` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/lte-fsg-seed` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/surface-pm-debug` | 4 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/surface-swap-boot` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/usb-functions-stage` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/video-codec-manifest-stage` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/video-decoder-lab-start` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/video-runtime-framework-boot` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/test2/video-runtime-stage` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/restore-sq2-allocator.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/sample-hfi-events.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/sample-mpeg2-first-error.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/sample-mpeg2-geometry.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/sample-vp9-eos.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/sq2-allocator-watchdog.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/sq2-c2-runtime-install.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/test-c2-bilibili.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/test-c2-media-refresh.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/test-c2-player.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/video/test-c2-vintf-live.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/virtualization/el2-android-recovery` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/virtualization/el2-kvm-init` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/virtualization/el2-launcher-main.c` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/virtualization/el2-pcie-diagnostic-init` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/virtualization/el2-preboot-init` | 2 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/virtualization/el2-terminal-autostart` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `probe/virtualization/el2-terminal-stage` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/build-sq2-default.py` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/check-sq1-autosuspend-policy.py` | 3 | 显式旧诊断/状态工具 | 不接入新入口；原语义保留供回退验证 |
| `scripts/check-sq1-suspend-wip.py` | 6 | 显式旧诊断/状态工具 | 不接入新入口；原语义保留供回退验证 |
| `scripts/check-surface-native-once.py` | 3 | 显式旧诊断/状态工具 | 不接入新入口；原语义保留供回退验证 |
| `scripts/check-suspend-trial-policy.py` | 11 | 显式旧诊断/状态工具 | 不接入新入口；原语义保留供回退验证 |
| `scripts/install-sq2-edp-test.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/pack-sq2-c2-runtime.py` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/patch-sq2-screen-only.py` | 4 | 显式旧诊断/状态工具 | 不接入新入口；原语义保留供回退验证 |
| `scripts/prepare-sq2-edp-stage.py` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/prepare-sq2-el2-android-preboot.py` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/prepare-surface-pm-debug.py` | 4 | 显式旧诊断/状态工具 | 不接入新入口；原语义保留供回退验证 |
| `scripts/run-quest2-gles-system.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/run-quest2-gles.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/run-quest2-vulkan-only.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/run-quest2-wildlife.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/smoke-sq2-ksunext.py` | 2 | 显式旧诊断/状态工具 | 不接入新入口；原语义保留供回退验证 |
| `scripts/smoke-sq2-screen-only.py` | 2 | 显式旧诊断/状态工具 | 不接入新入口；原语义保留供回退验证 |
| `scripts/sq2-display-boot-payload.py` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/sq2-gpu-boot-payload.py` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/surface-power-payload.py` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/test-quest2-offscreen.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/test-quest2-surfaceflinger.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/test-quest2-vulkan-only.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/test-quest2-wsi.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/test-sq2-cpu-frequency.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `scripts/test-sq2-gpu-opp.sh` | 1 | 功能/构建入口 | 已解除必须Y；显式旧兼容分支可保留 |
| `tests/test_closed_vulkan_boot.py` | 2 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_development_portability_policy.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_allocator_labels.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_allocator_restore.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_allocator_watchdog.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_c2_manifest_stage.py` | 1 | 测试 | 更新N成功预期并保留其他拒绝场景 |
| `tests/test_sq2_default_boot.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_display_boot_payload.py` | 2 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_el2_no_tz_pack.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_el2_runtime_pack.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_el2_terminal_autostart.py` | 1 | 测试 | 更新N成功预期并保留其他拒绝场景 |
| `tests/test_sq2_el2_terminal_stage.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_ksu_payload.py` | 2 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_lte_fs12_seed.py` | 3 | 测试 | 更新N成功预期并保留其他拒绝场景 |
| `tests/test_sq2_lte_fsg_seed.py` | 3 | 测试 | 更新N成功预期并保留其他拒绝场景 |
| `tests/test_sq2_overlay_phandles.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_runtime_boot.py` | 1 | 测试 | 更新N成功预期并保留其他拒绝场景 |
| `tests/test_sq2_usb_boot.py` | 1 | 测试 | 更新N成功预期并保留其他拒绝场景 |
| `tests/test_sq2_usb_input_watch.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_usb_network_watch.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_sq2_video_default_menu.py` | 1 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_surface_lte_baseline.py` | 2 | 测试 | 更新N成功预期并保留其他拒绝场景 |
| `tests/test_surface_pm_debug.py` | 10 | 测试 | 保留兼容/完整性验证；不是运行时限制 |
| `tests/test_surface_power_policy.py` | 2 | 测试 | 更新N成功预期并保留其他拒绝场景 |
| `tests/test_surface_swap.py` | 1 | 测试 | 更新N成功预期并保留其他拒绝场景 |
