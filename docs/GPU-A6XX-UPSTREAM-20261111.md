# GPU A6xx 上游修复回移（Linux 6.18.60）— 2026-11-11

**状态：host-only 源码准备 + 主机侧核心编译（2026-11-12 更新）。未加载模块、未接设备、未做任何硬件验收。**
`hardware_validated=false`，`compiled=core-only`，`deployed=false`。本文“已验证”指主机侧文本/哈希
检查与 arm64 核心编译；运行期与硬件行为均未验证，不据此提升 `full_*`。

## 1. 目标与结论

分支 `codex/6.18-mesa-a6xx-upstream-20261111`（从 `origin/main` `2da9e02` 克隆）把一批 pending/
已合并的 Adreno A6xx 上游修复适配到项目 pinned 的 Linux 6.18.60 源码。最终系列是 **11 个补丁**，
不是单一上游 series：

| 组成 | 补丁 | 来源状态 |
| --- | --- | --- |
| 内核前提子集 | `0001` `Do CX GBIF config before GMU start` | 上游 commit `60a4e18e0e8a`（6.19）的 **6.18 子集**，本地适配 |
| a680 修复系列 | `0002`–`0005` | 上游 `[PATCH 0/4] drm/msm/a6xx: Fixes for a680 (SC8180X)`（Val Packett，2026-11-03），**仍 pending** |
| 已合并主线修复回移 | `0006`–`0011` | 已 merged-mainline 的 6 个 commit（见 §3），按 6.18.60 适配 |

机器可读 pins 在 `configs/drm-618-a6xx-upstream-20261111.json`。

## 2. 基线

| 项目 | 值 |
| --- | --- |
| 内核 | Linux 6.18.60（`kernel/Makefile` 原值 6.18.60） |
| 源码归档 | `private/build-inputs/kernel-6.18/linux-6.18.60-current-sources-20261107.tar.gz` |
| 归档 sha256 | `26f3045793cdf6e44379b75cb0adda0b7b32f2914537fcfb02f83c8d752a3b3b` |
| 归档大小 | 303774784 B，91832 个成员 |
| 解包 (`kernel/`) | 91244 个文件，1539286812 B（≈1.47 GiB） |
| 编译参考配置 | `configs/kernel-6.18-baseline.config`，sha256 `bcd5f646…20fea`，`CONFIG_LOCALVERSION="-sq2-android1"` |
| 本系列改动 | 7 个文件：`a6xx_catalog.c`、`a6xx_gmu.c`、`a6xx_gpu.c`、`a6xx_gpu.h`、`adreno_gpu.c`、`a6xx_gmu.xml`、`msm_gpu.c` |

**重要**：归档是项目 pinned 源码树，不是 pristine LTS tarball。它已含项目本地改动（例如
`a6xx_catalog.c` 的 `SQ2_A680_FW` 固件名、已存在的 `a6xx_gmu.xml.h` include）。
所有 before/after 哈希都以该归档为准，不能拿 upstream master 的 blob 哈希比对。

## 3. 11 个补丁与前提分析

| # | 主题 | role / origin | 上游 | 6.18.60 适配点 |
| --- | --- | --- | --- | --- |
| 0001 | Do CX GBIF config before GMU start | prerequisite / backport-subset | `60a4e18e0e8a` | 新增 `struct a6xx_info::gbif_cx`、`a640_gbif[]`、GMU 启动前 GBIF 写入；保留旧 hw_init 写入 |
| 0002 | Restore GBIF CX config for A640 family | fix / pending-upstream | `…65398-2` | 依赖 0001；只加 A640/A680 两项 `.gbif_cx` |
| 0003 | Fix GMEM range on a680 | fix / pending-upstream | `…65398-3` | 无 A722：16M 分支保持 a740-only；`MSM_PARAM_GMEM_BASE` 追加 `adreno_is_a680()` |
| 0004 | Add GMU_SP_RF_CONTROL registers | fix / pending-upstream | `…65398-4` | 上下文一致，原样应用；必须先于 0005 |
| 0005 | Import a680 hwcg sequence from kgsl | fix / pending-upstream | `…65398-5` | 已含 `a6xx_gmu.xml.h` 故去掉 include hunk；依赖 0002/0004 |
| 0006 | fix use-after-free of OPP in a6xx_pm_resume | fix / merged-mainline | `8128ae7af6b4` | 适配后等价：`dev_pm_opp_put(opp)` 移到 `dev_pm_opp_set_opp()` 之后 |
| 0007 | check pm_runtime_resume_and_get during resume | fix / merged-mainline | `e2f57c576f14` | **改动最大**：6.18.60 的 `a6xx_pm_resume()` 只有 grp_clks 段，错误阶梯按 6.18.60 重写 |
| 0008 | Increase GMU FW init timeout | fix / merged-mainline | `ba970587a0e1` | 同一 poll，10 ms → 100 ms |
| 0009 | Fix secondary rail vote in ARC votes | fix / merged-mainline | `128a0edde507` | 仅比较对象与注释变化（`level` → `pri[pindex]`） |
| 0010 | Correct OOB usage | fix / merged-mainline | `d34b6919798c` | 仅 a6xx/A7xx：sysprof OOB 从 `a6xx_gmu_resume()` 移到 `hw_init()` 末尾 |
| 0011 | Fix IRQ storm during msm_recovery test | fix / merged-mainline | `bc024d325e98` | 仅 `a6xx_irq()` + 公共 `msm_gpu.c recover_worker()`；A5xx/A8xx hunk 省略 |

前提分析（`0001` 必须最先应用）：6.18.60 **不含** `60a4e18e0e8a`——没有 `gbif_cx` 成员、
没有 `a640_gbif[]`，且 `a6xx_gpu.c hw_init()` 仍直接写 `REG_A6XX_GBIF_QSB_SIDE0..3`。
`0002` 依赖 0001 的字段/表。若 6.18 LTS 之后回移 `60a4e18e0e8a`，应删除 0001 并把 0002 重写为
该树形态，**不要叠加两份**。上游版本会删除旧 hw_init 写入，本地保留它们，因此 A640/A680 会
“hw_init 旧写入 + GMU 前 catalog 写入”两次配置——这是刻意选择，**必须由编译/运行阶段确认**。

`0007` 是本批语义改动最大的补丁：它不是逐行 cherry-pick，而是按 6.18.60 的
`a6xx_pm_resume()` 函数体重写了错误回退阶梯（GPU 时钟块只有 grp_clks）。编译/审阅时应把
这段与上游 `e2f57c576f14` 逐项对照。

## 4. 集成产物与命令

```text
patches/drm-618-a6xx-upstream/0001..0011-*.patch        适配后的 11 个补丁（保留上游 From/Subject/Date 与 posting/commit 说明）
configs/drm-618-a6xx-upstream-20261111.json             唯一 pins：归档 sha256、有序补丁、role/origin、每文件 before/after、baseline/preconditions/markers/final_absent、limits、rollback
scripts/prepare-drm-618-a6xx-upstream.py                校验/解包/严格顺序应用/前置+后置条件检查
tests/test_drm_618_a6xx_upstream.py                     主机 unittest（27 项）
docs/GPU-A6XX-UPSTREAM-20261111.md                      本文
```

```bash
# 1) 只校验 + 临时树预演（不写仓库；读归档一次）
python3 scripts/prepare-drm-618-a6xx-upstream.py --check
# 期望: check OK: 11 patches | 18 markers | kernel 6.18.60 | tool patch

# 2) 解包 pinned 源码并应用（fresh work dir；≈1.47 GiB）
python3 scripts/prepare-drm-618-a6xx-upstream.py \
    --apply --work-dir work/drm-618-a6xx-upstream-20261111
# 成功时 <work-dir>/SOURCE.json 记录归档/补丁 sha256、7 个文件 before/after、检查项与边界

# 3) 主机测试
python3 -m unittest tests.test_drm_618_a6xx_upstream -v
```

`--apply` 失败会删除刚建的 work dir（fresh-only，且只在全部校验通过后才写 `SOURCE.json`），
不会留下半应用源码。归档、`private/build-inputs` 与已跟踪文件从不被修改。
脚本默认 `patch --batch --forward --fuzz=0 -p1`；`git apply` 仅作后备，且当解包树位于
git worktree 内时明确拒绝（那里 `git apply` 按仓库根解析路径并静默忽略，exit 0 假成功）。

## 5. 已验证（仅主机侧）

- 11 个补丁 sha256 与 config、`out/a6xx-work/patches/SHA256SUMS`、`patch-meta.json` 一致；
  每文件 before/after 与 `patch-hashes.json` 一致；归档 sha256/字节数一致。
- 严格顺序应用通过（`patch -p1 --fuzz=0`）：`0001`→`0011`，7 个文件的哈希链完整。
- **baseline（8 项，pristine 必须存在）**：`a680_sqe.fw`/`a680_gmu.bin` 固件名、10 ms
  GMU init 超时、`sec[j] >= level`、仅 masked 的 `disable_err_irq` status、OPP put 先于 set、
  未检查的 `pm_runtime_resume_and_get`、`a6xx_gmu_resume()` 里的 sysprof OOB。
- **preconditions（14 项，pristine 必须不存在）**：`gbif_cx`、`a640_gbif`、`a680_hwcg`、
  `GMU_GX_GMU_SP_RF_CONTROL`、`gmem_range_min = 0;`、`adreno_is_a680` GMEM_BASE、
  OPP 重排、checked pm resume、100 ms 超时、`pri[pindex]`、hw_init OOB、IRQ storm mask、
  recover 里 `disable_err_irq = false;` 等。
- **markers（18 项，应用后必须出现）**：a680 hwcg 表与选择、a680 GMEM 0 与 GMEM_BASE、
  GMU SP_RF CONTROL 0/1、`gbif_cx` 字段与 GMU 前循环、`a640_gbif` + 两处 `.gbif_cx`、
  OPP put after set、checked pm resume、100 ms 超时、`pri[pindex]`、hw_init OOB、
  IRQ storm mask、recover 重新启用 err IRQ。
- **final_absent（5 项，应用后必须消失）**：resume 里的旧 sysprof OOB、1M 默认 GMEM 行、
  requested-level 次轨比较、10 ms 超时、未检查的 pm resume 对。
- 27 项 host unittest 通过（真实产物 provenance/一致性 + 合成 fixture 的 check/apply/
  原子失败/乱序拒绝/baseline/final_absent）。依赖私有归档或 Lead 输入的用例在缺失时 skip。

### 5.1 主机侧真实 arm64 核心编译（build/env workstream，2026-11-12）

- 工具链：pinned `toolchain.tar.gz`（aarch64-linux-gnu-gcc 13.3.0）；配置
  `configs/kernel-6.18-baseline.config`（sha256 `bcd5f646…`，localversion `-sq2-android1`）；`-j64`。
- **BASELINE**（未打补丁，同一 pinned 归档）：`olddefconfig` + `make Image modules dtbs` exit0，98 s；
  Image `071bb1c78e40fc69b12823200cf1efff42a52d7b5383ad2638332c2cb4b4e69e`（35,588,608 B）；
  486 modules / 333 dtbs；37 warnings / 0 errors。System.map 与 Module.symvers 与项目已验收
  参考构建逐字节相同（复现了可接受构建的核心输出）。
- **PATCHED**（本系列 11 补丁按序 `git apply` 到 pristine 副本）：exit0，99 s；Image
  `82cd2d983741365bd38f81109738dfc48b688c88b6f5858a80ba30325df3b3e5`（同尺寸）；release 仍为
  `6.18.60-sq2-android1`（`msm.ko` vermagic 匹配）；486 modules / 333 dtbs；**37 warnings / 0 errors，
  无新增 warning**；System.map/Module.symvers 不变（无导出符号/ABI 变化）。
- 变更对象（`CONFIG_DRM_MSM=m`）：`a6xx_catalog.o`、`a6xx_gpu.o`、`a6xx_gmu.o`、
  `a6xx_hfi.o`（仅因 `struct a6xx_info` 布局变化而重编）、`adreno_gpu.o`、`msm_gpu.o`、`msm.o`；
  `msm.ko` `0d0266de…`→`6579d372…`；新增 rodata 符号 `a640_gbif`/`a680_hwcg`；补丁 `0004` 的 XML
  确实改变生成头 `generated/a6xx_gmu.xml.h`。
- 等价性：`prepare-… --apply` 生成的树中全部 7 个被改文件与编译所用 patched 源逐字节相同，即
  “脚本应用结果 = 被编译的源码”。
- 环境注意：pinned 归档遗漏 12 个 `scripts/dtc/include-prefixes/*` 符号链接目录，`make dtbs`
  会失败；已在**工作副本**按 stock 布局恢复相对链接（未改归档，未改任何普通文件内容），
  记录于 `out/a6xx-build/source-archive-remediation.txt`。这不是内核源码补丁。
- **独立审计**（另一 workstream，`out/a6xx-upstream/INVENTORY.json`）：以真实 6.18.60 源码逐项
  复核 11/11 补丁与本文件所有排除项，未发现其他可回移的 A6xx 修复；并逐文件确认
  `v6.18.60..v6.18.63` 仅新增 A704 GPU-id（feature，非修复），且该 a680 series 在
  torvalds `v7.4-rc2`、`msm-next`、`msm-fixes` 均未合并。

**未验证**：模块加载、官方完整链的 12 组 external modules 与 `board.dts`、GMU HFI 超时/
纹理损坏/PM/IRQ 行为是否在运行期被修复、性能/功耗/长时、SQ1/SQ2 或任何设备行为。

## 6. 明确未做（不得提升的边界）

- 已做主机侧核心编译（`Image`/modules/dtbs，见 §5.1），但**未**构建官方完整链的
  12 组 external modules 与 `board.dts`，未做模块加载或运行期验收。
- 未连接、未部署、未重启任何设备；没有 `/dev/dri`、sysfs 或运行日志证据。
- `0002`–`0005` 上游仍 pending；`0001` 是本地子集；`0006`–`0011` 是对已合并主线修复的
  6.18.60 适配回移——本批**不是**单一上游 series 的原样 backport。
- 未证明 GMU HFI `GX_BW_PERF_VOTE` 超时、桌面锁死、Vulkan 纹理损坏、OPP UAF、
  IRQ storm、OOB 顺序问题在运行期被修复。
- 未做性能、DDR/总线投票、PM、长时、SQ1 或双机验证；`full_*` 全部保持 false。

## 7. 回滚

删除 fresh `work/drm-618-a6xx-upstream-*` 即可（脚本拒绝复用目录，不会就地改源）。
仓库侧 `git checkout -- <file>`/`git reset --hard` 可恢复本系列新增文件；pinned 归档与私有输入
从未被写入。没有设备、EFI、GRUB 或发布 pins 改动需要回滚。

## 8. 证据

- `out/a6xx-integration/`（本草案：RECOMMENDATION、patches、config、script、test、docs）。
- `out/a6xx-work/`（Lead 输入：11 patch + `SHA256SUMS`、`patch-meta.json`、`patch-hashes.json`）。
- `--apply` 生成的 `work/drm-618-a6xx-upstream-*/SOURCE.json`（安装后运行产生）。
- 测试输出：`python3 -m unittest tests.test_drm_618_a6xx_upstream -v`（27 项通过）。
- 主机核心编译证据（ignored，本地产物）：`out/a6xx-build/baseline/build-audit.json`、
  `out/a6xx-build/patched/build-audit.json`、`out/a6xx-build/comparison/COMPARISON.json`、
  `out/a6xx-build/comparison/changed-objects.tsv`、`out/a6xx-build/source-archive-remediation.txt`。
- 独立上游审计：`out/a6xx-upstream/INVENTORY.json`、`out/a6xx-upstream/REPORT.md`
  （含 11 patch 复核、127 commit 分类、stable v6.18.60..63 逐文件比对）。

## 9. 后续

1. build/env：主机核心编译已完成（见 §5.1）；剩余官方完整 external modules/board.dts、
   模块加载与运行期/设备验收，需另行明确授权。
2. 已安装到仓库、复跑 `--check`（`check OK: 11 patches | 18 markers`）与 27 项测试；由 Lead
   提交到 `codex/6.18-mesa-a6xx-upstream-20261111`（未推送远端）。
3. 上游跟踪：a680 series 若合并，记录 commit hash 并重新比对；`0001` 在 6.18 上仍需保留，
   除非 6.18 LTS 自己回移了 `60a4e18e0e8a`；`0006`–`0011` 若日后进入 6.18 stable，
   需与新 stable 版本逐项比对后删除重复回移。
4. 设备侧验收必须另行显式授权，且必须与本 host-only 结论分开陈述。
