# KGSL + DRM/SDE resume round 1 — 2026-11-11

用户明确要求"使用多agent并行可以并行开发的部分，推进恢复工作"后，暂停解除。本轮仅做源码与主机
验证：不连接设备、不部署/重启、不改默认发布 pins、不推送远端。整体目标保持 active，`full_*` 未提升。

## 本轮并行结构

五个子代理各自基于 prepared `integrated-17` 独立工作，写集互斥；Lead 负责交叉审查、语义合并、
串行配置/文档、以及全部完整构建（KOUT 唯一持有者）。证据在 ignored 的 `out/kgsl-resume-2/`，
工作树在 `/mnt/spx-workspace/.../sde-resume-2/<agent>/`。

| 任务 | 交付 | 关键结果 |
|---|---|---|
| irq | `out/kgsl-resume-2/irq/PRODUCT.patch`（28850 B，9 路径） | 真实 Linux IRQ 线（preinstall→request_irq→postinstall，局部回滚，tables 前 free_irq）；6.18 `drm_driver` irq/vblank/lastclose 迁移；per-CRTC prepared-vblank gate；56 host 检查+5 变异；ARM64 TU msm_drv 诊断 51→9，IRQ family 0 |
| mode-kernel | `out/kgsl-resume-2/mode-kernel/PRODUCT.patch`（14 文件，3 新增） | driver-owned requested/adjusted/selected + `SDE_MODE_REQUEST_V1`/`SDE_MODE_CATALOG_V1` uapi；`msm_kms.h` 改收 CRTC state；DSI bridge/catalog publisher；sde_crtc/connector/encoder/atomic 消费者；真实 `<drm/drm_fourcc.h>`；112 host 检查+5 变异；私有 `private_flags`/`SEAMLESS` 清零（364+120→0） |
| mode-userland | `USERLAND.patch` + `PRODUCT.patch` | 对齐最终 kernel uapi 的 requested/derived/selected 序列化、双重/部分 apply 拒绝；42 TU/5 source 库严格 Android aarch64 链接 exit0；103 host 检查+5 变异；与真实 kernel `sde_mode_state_ingest/derive/commit` 的 90 项 payload round-trip 通过；仅库级，不是 HWC/设备验收 |
| mst | `CORE.patch`（8755 B）+ `DRIVER-PRODUCT.patch`（54946 B） | 6.18 core 显式 per-connector opt-in persistent 生命周期（NULL 默认行为不变）+ 新 `drm_dp_atomic_release_port_payload`；固定 primary connector 身份保持；core apply/rollback 与窄 TU 0/0/0；driver apply/rollback 字节精确；59 host 场景+4 变异；`dp_mst_drm.c` 自有错误 113→0（合并后整编中剩 13 条均为新 core 符号，须新配对内核） |
| owner | `out/kgsl-resume-2/owner/PRODUCT.patch`（19715 B） | drmm-managed `msm_drm_private`、GEM/file owner+device 引用、uninit 后拒绝新准入并等待在途、VRAM 延迟到最终 DRM put、可逆 PRIME import；28 host 场景+5 变异、3 个真实 ARM64 probe；整编后不新增任何诊断 |

所有子代理均声明 `hardware_validated=false`、`deployment_ready=false`，并保留失败首试与被建模的内核
叶子（IRQ/slab/MMIO/PM/DPCD/locks 等）。

## Lead 语义合并

- `msm_drv.c`：`msm_postclose` 取 owner 版本（owner 引用、lastctx 清除、enable_refcnt、`drm_dev_put`），
  并统一为 tab 缩进；`msm_lastclose` 取 IRQ 的 6.18 表述（删除 4.19 `dev->vblank[]` disable-timer 遍历）
  + owner live-owner guard，删除仅为旧遍历存在的 `int i`。
- `msm_atomic.c`：4 处 bridge 链冲突统一取 mode-kernel 形态。依据匹配 6.18 内核
  `include/drm/drm_bridge.h`：`drm_bridge_chain_get_first_bridge()` **会递增引用，必须 `drm_bridge_put()`**；
  MST 版本漏 put，每次 commit 会泄漏 bridge 引用。MST payload/atomic-check 改动保留。
- 新增 `techpack/display/msm/sde/sde_mode_state.h` 走 patch；两个 uapi 头
  `include/uapi/drm/sde_mode_identity.h`、`sde_mode_request.h` 走 config `additional_sources`，
  prepared tree 共 318 源。

## integrated-18 实际整编结果

- 新增配置补丁 `patches/sde-xiaomi-618-resume-round1.patch`（27 文件，sha256
  `c0ed125fa935493e9ad78a301c8842d8922c99e4c1642ad8271766bb324b999f`），配置 18 补丁/318 源；
  `SOURCE.json` 的 `config_sha256` 与当前 config 一致。
- 三态完整构建（既有 prepared 6.18 KOUT，只读）：

  | 状态 | exit | 对象 | 模块 | 错误行 |
  |---|---|---|---|---|
  | BASELINE | 2 | 25 | 0 | 978 |
  | MODIFIED | 2 | **54** | 0 | **254** |
  | ROLLBACK | 2 | 25 | 0 | 978（与 baseline 相同） |

- 目标诊断：`private_flags` 364→0、`DRM_MODE_FLAG_SEAMLESS` 120→0、`drm_irq_install`→0、
  `DRM_UNLOCKED`→0、`dp_mst_drm.c` 113→13、`msm_drv.c` 42→1（仅 `iommu_present`）。
- 320 个事务文件精确回滚/重应用；KOUT 五输出 hash 未变；16/16 host checker 组 exit0。
  其中 active-lock / prime 仅补充 owner 真实 `*_active()` 拆分所需模型（`list_empty`、`list_del_init`、
  `drm_dev_get/put`、`msm_drm_owner_*`、`msm_gem_publish`），断言与场景列表未改。
- 四角色：`MODIFIED_FILE.tar.gz` `5cbb54e4…`、`DIFF_FILE.patch` `f6e200c3…`、
  `VERIFICATION.txt` `db063a5d…`、`ROLLBACK.sh` `c1b7f563…`，位于
  `out/kgsl-resume-20261111/sde-integration-18/`。

## 明确未完成（不得含糊）

1. `dp_mst_drm.c` 剩余 13 条诊断正是新 core 符号（`cbs.connector_lifetime`/`disconnect_connector`、
   `DRM_DP_MST_CONNECTOR_*`、`drm_dp_atomic_release_port_payload`）。现有 dev2 KOUT 没有它们，
   必须**单独构建匹配的 6.18 内核**（Lead 独占）后重跑，不能与 dev2 模块混装。
2. 剩余整模块阻塞是 round-1 范围外的既有 6.18 API 漂移：`dsi_panel.c`（`MIPI_DSI_MSG_*`、
   `mipi_dsi_msg.ctrl/wait_ms`、`drm_dsc_config.dsc_panel_id`、`drm_panel_init`、`devm_of_pwm_get`、
   `PTR_RET`）、`sde_crtc.c`（evtlog 格式、`of_fdt_get_ddrtype`、`vzalloc/vfree`）、`sde_dsc_helper.c`
   `dsc_panel_id`、`sde_connector.c`（prototype、`drm_framebuffer_get/lookup`、`drm_connector.panel`）、
   `sde_plane`/encoder `drm_encoder::bridge`、`msm_drv` `iommu_present` 等。
3. mode-userland 仅是库级链接与主机 round-trip；没有 HWC 服务、联合内核+用户态运行或设备验收。
4. 未做设备访问/部署/重启/性能/PM/真实提交/fence 退休/SQ1-SQ2 验收；`full_*` 不提升。

## Round 2 (2026-11-11)：匹配 6.18 内核、SDE 整组 API 迁移与首个完整模块

用户目标为"实现KGSL-SDE，并在真机调试运行"；本轮先打通"完整模块可编译链接"这一设备运行前置门禁。

- 三个并行代理：dsi+dsc、sde graphics、sde hw/encoder/DP；Lead 独占匹配内核构建、语义合并与链接修复。
- 匹配内核（Lead）：paired 6.18 source + `out/kgsl-resume-2/mst/CORE.patch` → `kernel-mst1` / `kernel-out-mst1`，
  release `6.18.60-sq2-kgsl-sde-mst-dev1`，Image `e0afdadd…`，Module.symvers `e6fc06cf…`，486 模块，
  0 error / 37 既有 warning；`Module.symvers` 导出 `drm_dp_atomic_release_port_payload`。旧 dev2 KOUT/模块未动。
  结果：`dp_mst_drm.c` 诊断 113 → 0。
- 三候选均独立交付窄 ARM64 -Werror 矩阵、负例/三态、`--fuzz=0` 字节精确 patch；Lead 逐一独立复验后合并：
  - dsi+dsc：25 个 TU 81→0；真实 6.18 DSI message flags/ctrl sidecar、panel/pwm/bridge/class/tear-off、
    driver-owned `dsc_panel_id`、PLL divider flags。
  - sde graphics：6 个 TU 47→0（含 sde_kms 修复 fatal include 后暴露的 94 条真实漂移）；const mode_valid、
    connector-owned panel、framebuffer helpers、DT 版 `sde_get_ddrtype()`、6.18 tracepoint 宏；显式关闭
    secure-VM/panel-notifier/minidump（6.18 无 provider）。
  - sde hw/encoder/DP：32 个 TU 101→1（剩余 DDR type 由 Lead 闭合）；4 参 `drm_bridge_attach`/
    `devm_drm_bridge_alloc`、`drm_mode_vrefresh`、colorspace、platform remove、`drm_gem_object_put`、11 个兼容头。
- Lead 语义/链接修复：vendor format modifier 统一为 donor 值（DX=0x2/TIGHT=0x4/TILE=0x8，与 TILED2/3
  数值冲突已注明）；5nm PLL 头合并 DSI enum + DP IDs；catalog 用 `sde_get_ddrtype()`；HDCP-qseecom 代码
  编译关闭（DP 报告 HDCP 不可用但保持 alive）；fbdev 不构建（4.19 fb_helper API 不存在）；
  altmode/fsa4480/dwc3/spmi/ext-display 提供显式 `-EOPNOTSUPP` shim；`MODULE_IMPORT_NS("DMA_BUF")`；
  `sched_setattr_nocheck()` FIFO helper；splash buffer 释放改为"保留至重启"并记录（memblock/free_reserved_area
  非模块安全/未导出）。

两次真实整编事务：

| 事务 | 补丁/源 | BASELINE | MODIFIED | ROLLBACK | checker |
|---|---|---|---|---|---|
| integrated-19（stage A） | 19 / 321 | exit2, 55 对象, 0 模块 | exit2, 78 对象, 0 模块 | exit2, 55 对象 | 16/16 exit0 |
| **integrated-20（stage B）** | **20 / 332** | exit2, 78 对象 | **exit 0, 114 对象, 1 模块** | exit2, 78 对象 | 16/16 exit0 |

- `integrated-20` MODIFIED：0 error、0 warning（KCFLAGS=-Werror），MODPOST clean；
  `msm_drm.ko` sha256 `cb34d88e78e2d1240a031936327a250199f3cb6c9e9ba38d4375ee966b94dcf9`（4161544 B），
  vermagic `6.18.60-sq2-kgsl-sde-mst-dev1 SMP mod_unload aarch64`，depends `llcc-qcom`。
- 四角色：MODIFIED_FILE `8fbff6be…`、DIFF `c52a3229…`、VERIFICATION `aa4b1b07…`、ROLLBACK `c1b7f563…`；
  新增配置补丁 `patches/sde-xiaomi-618-resume-round2b.patch` sha256 `079e0139…`。
- 明确边界：模块**未部署、未加载、未启动**；真机显示/真实提交/fence 退休/PM/性能/SQ1-SQ2 全部未验证。
  禁用/未移植特性（HDCP、fbdev、secure-VM、panel notifier、minidump、USB-C altmode/FSA4480/DWC3/SPMI/
  ext-display shim、splash 保留内存）必须随任何声明列出。模块必须与匹配内核（kernel-mst1）配套，
  禁止与 dev2 内核/模块混装。
- 下一阶段：设备 bring-up（部署匹配内核与模块、模块加载、内部 eDP/DSI 显示点亮），再做真实提交/fence/
  PM/性能验收；在得到真机证据前不提升 `full_*`。

## 续接入口

- 本地提交：`codex/kgsl-sde-port-v83` 上的 SDE18 集成提交（未推送；暂停保存提交 `d51ff3e` 保留）
- 机器可读状态：`out/kgsl-resume-20261111/CURRENT.json`
- 集成证据：`out/kgsl-resume-20261111/sde-integration-18/`（四角色 + STATUS/COMMANDS/VERIFICATION）
- 五路候选证据：`out/kgsl-resume-2/{irq,mode-kernel,mode-userland,mst,owner}/STATUS.json`
- 角色路径/SHA：`out/kgsl-resume-20261111/FINAL-ROLE-PATHS.txt`
- 本轮 Lead 计划与合并日志：`out/kgsl-resume-2/integration-18/PLAN.md`
- 下一步（按序）：匹配 6.18 内核构建（MST core）→ 其余 6.18 API 迁移 → 完整模块/MODPOST →
  设备侧提交/显示/fence/PM/性能/SQ1-SQ2 验收。
