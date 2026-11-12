# 全 Mesa 通用镜像（Linux 6.18.60 + Mesa 用户态）— 2026-11-12

**状态：主机侧构建完成。未写入设备、未启动、未验证显示/渲染/性能，`hardware_validated=false`。**
本文“已验证”仅指主机侧构建产物、结构、哈希与静态链路检查；不据此提升任何实机或 `full_*` 结论。

## 1. 目标与参考

按用户要求：参考当前构建基线（`configs/kernel-baseline.json` 的 v21-release-4k 私有基线、
Linux `6.18.60-sq2-android1`）与 6.3.8-sq2 时期的统一构建历史，更新统一构建脚本
`scripts/build.py`（实际入口 `scripts/build-lts.py`），新增“全 Mesa”可选项，并用它构建一个
通用（SQ1/SQ2 共享）整盘镜像。

历史结论（来自本地 git 历史重建，证据在 ignored `out/build-history-recon/`）：
6.3.8-sq2 时代的图形选择**不是构建参数**，而是两个 EFI/GRUB 入口共享同一 Image/DT/initramfs，
由 EL2 启动器内嵌的等长 UTF-16 token `sq2.vulkan=closed|mesa` 在启动时选择；当前
`scripts/build.py full-image` 是 6.18 时代新增的整盘构建流程（源码内核 + 私有 runtime +
原 20 GiB 模板 + 新 userdata/GPT）。本选项把这套既有 seam 提升为构建期选择，并补齐
“全 Mesa”所需的用户态层替换。

## 2. 新增选项

```text
python3 scripts/build.py full-image --graphics baseline|mesa \
    [--kernel-patches DIR] --output FRESH_OUT --jobs N
```

- `--graphics baseline`（默认）：与已验收基线逐字节相同的混合图形栈（QTI display/composer +
  Mesa GLES + 可选闭源 Vulkan entry），构建行为不变。
- `--graphics mesa`：构建全 Mesa 链路（见 §3）。
- `--kernel-patches DIR`：在编译前把该目录内所有 `*.patch` 按文件名顺序应用到从 pinned 归档
  解出的内核源码；`mesa` 模式下未显式给出时默认使用
  `patches/drm-618-a6xx-upstream`（上一阶段验证过的 11 个 A6xx/Mesa 链路修复）。
- `--kernel-mode prebuilt` 与 `--kernel-patches` 同时出现会明确报错，不会静默忽略。
- 输出目录必须全新；不会写设备、不会部署、不产生内容哈希启动门槛。

## 3. 全 Mesa 模式改了什么

用户态（在 `full_image.assemble_runtime` 内完成，主机侧、可静态审计）：

1. 保留 `etc/googlebook-security/**` 的 72 个 Mesa 成员（pinned 私有 payload 原字节）：
   Mesa freedreno `libEGL/libGLESv1_CM/libGLESv2`、`libgallium_dri.so`、Turnip
   `vulkan.freedreno.so`、libdrm、minigbm mapper/gralloc、drm_hwcomposer AIDL3、
   minigbm allocator AIDL3，以及 `ro.hardware.egl=freedreno`、
   `ro.hardware.vulkan=freedreno`、`ro.vendor.hwcomposer.mode=client` 等属性。
2. 删除闭源 Qualcomm 图形层共 **124** 个成员（约 151,307,943 B）：
   `etc/sq2-qti-system`（85，QTI composer/allocator/SDM/mapper5/闭源 EGL）、
   `etc/sq2-graphics-full`（22，闭源 v77 GLES/Vulkan）、`etc/sq2-vulkan-closed`（14），
   以及 `bin/qti-system-display-stage`、`bin/qualcomm-full-start`、
   `bin/closed-vulkan-start` 三个闭源图形前置 helper。
3. `init` 去掉两条闭源图形 stage 调用（`qualcomm-full-start`、`qti-system-display-stage`），
   保留视频用的 `quest2-hidl-stage`；前置写入 `etc/sq2-graphics-mode = full-mesa` 并在
   `/logs/status.txt` 打印 `SQ2_GRAPHICS_MODE: full-mesa`。
4. 启动命令行：`kernel-options.txt` 与 `el2launch.efi` 内嵌 UTF-16 命令行的
   `sq2.vulkan=qcom` → `sq2.vulkan=mesa`，二者**等长**替换，PE 结构/大小不变；`SQ2Res`
   救援入口与 SQ1 profile 通过同一个 effective token 派生，不会回退成 qcom。
5. 内核：11 个补丁应用到 pinned 6.18.60 源码树后编译（`KERNEL-PATCHES.json` 记录每个
   补丁 sha256 与全部被改文件的新 sha256），编译模块替换 initramfs 内同版本全部 `.ko`。

不改动：`template.img` 的布局/GPT/分区边界、`start.efi`、固件、私有 SSH 身份与
single-user 策略、EFI 文件集、内容哈希门槛策略。

## 4. 本轮实际构建（host-only）

```sh
cd /home/kanoyami/workspaces/chromebookOS-mesa-a6xx
python3 scripts/build.py full-image --graphics mesa \
  --kernel-patches patches/drm-618-a6xx-upstream \
  --output /home/kanoyami/workspaces/chromebookOS-mesa-a6xx/out/full-mesa-image-20261112 \
  --jobs 32
```

| 产物 | 值 |
| --- | --- |
| 内核 release | `6.18.60-sq2-android1`（pinned 工具链 aarch64-linux-gnu-gcc 13.3.0，32 jobs） |
| 内核补丁 | 11/11 应用成功（`KERNEL-PATCHES.json`）；`a6xx_catalog.o` = `a9bca00d93da9ffa…`，与上一阶段独立验证的 patched 构建对象**逐字节相同**，含 `a640_gbif`/`a680_hwcg` 符号 |
| Image | `4183492142f27f8d98fd849d52fa38be792e91056896f4706259bc36867bdcf7`（35,588,608 B） |
| android-el2.dtb | `91a2c5eb4d5cbec020469f51158852f6e1c789ba47da4832e4fa6dc14d8933fc`（90,742 B） |
| el2launch.efi | `7c85f159fdfa39f709a42f740d77fd118c7ab54286c08da4c3ffe08b13b61513` |
| kernel-options.txt | `d5ca17ddf9de270e1b351eeaa5e72721c39d8858f313bb71247b172d70683a0d`（含 `sq2.vulkan=mesa`） |
| initramfs.cpio.gz | `2f62df8efab98bca54feeb52c01818e6a81a9f44b8a196fe412fb6d7429fde80`（203,605,739 B，1154 成员，`init` sha256 `cce7b47e…`） |
| 整盘 raw | 21,474,836,480 B（20 GiB，39 分区，GPT 主/备一致）：`690ad7032ad6af62f6b425fd3b2cbc70e691399ab457827a1449eda715d2487b` |
| 整盘 gzip | 8,150,671,223 B：`7509f063d48a4c9e7970cb0c71cf54932eaf458b8ff5255dd435d4d21dbb9933` |

主机侧独立校验（全部通过）：

- `out/mesa-payload/verify_mesa_image.py bundle --graphics mesa --strict` → `ok=true`：
  21 项 Mesa 成员 pin、闭源成员 0、`sq2.vulkan=mesa`、`etc/sq2-graphics-mode`、
  init 不含闭源 stage、`init` sha 与预期一致。
- initramfs 记录审计：闭源图形成员 0，`etc/googlebook-security/**` 72 成员保留，
  1154 成员；`lib/modules/6.18.60-sq2-android1/.../msm.ko` 与编译输出 sha 一致。
- `sgdisk -v`：主/备 GPT 一致，`No problems found`；分区边界与模板一致。
- `pigz -dc *.img.gz | sha256sum` = raw 的 sha256（8.15 GB 全流独立解压校验）。

构建审计：`FULL-IMAGE.json`（graphics=mesa、boot token、removed/retained、kernel_patches=11、
`hardware_validated=false`）、`KERNEL-PATCHES.json`、`MESA-VERIFY-BUNDLE.json`、
`disk/{LAYOUT.json,IMAGE-MANIFEST.json}`、`BUILD-COMMANDS.json`。

## 5. ESP 部署：必须使用 disk image 的 initramfs 变体（2026-11-12 事故）

设备 ESP 上的 `EFI/SQ2Nat/initramfs.cpio.gz` 不是 paired/EFI 目录里的那个文件。
`disk_image.build()` 会调用 `storage_payload()`，在 paired initramfs 后追加 storage
delta：`etc/sq2-ssd-partitions`、`etc/sq2-ssd-layout`、`bin/prepare-native-ssd`，并把
`init` 的 `sq2-loop-slice "${ssd_part}3"` 改为 `sq2-loop-slice /dev/block/by-name/super`。
缺少 delta 时 init 无法发现内置 NVMe，表现为 SSD-not-found 且 SSH 不可达。

2026-11-12 曾把 paired 变体（`39a3993c…`，203,609,782 B）直接写入设备 ESP，重启后
SSD-not-found。正确文件应取自 `out/<build>/disk/…img` 的 SQ2PROBE/FAT32 分区
（本次 r2 为 `5fb5622c…`，203,615,765 B；r1 为 `34132fa9…`），或者完整刷新该
`disk/…img`。paired 目录只用于外部配对/交付，不是内置 SSD 的 ESP 载荷。

恢复用最小载荷：以 r1 ESP initramfs 为基础，仅替换 `bin/grow-image-userdata` 为长选项
修复版，再用 `storage_payload()` 保留完整 delta；结果 `c1bece1e…`（203,612,637 B），
除 grow helper 外与原 r1 ESP initramfs 逐成员一致，模块与设备未改动的 r1 Image 配对。
主机侧准备 `out/esp-recovery-20261112/`，Windows 侧用 `run-recovery.cmd` 提权写入。

部署时只替换两个 `initramfs.cpio.gz`，保留 `SPX-IMAGE.txt`、
`SPX-USERDATA-GROWN.txt`、`GPT-BACKUP-*.bin`、`Image`、dtb、launchers 与 firmware。
仅主机准备、尚未写回设备；恢复后仍需实机验证启动、扩容 marker 与 swap。

## 6. `external/edp-618` 覆盖带补丁 `msm.ko` 根因修复与 r3/r4 ESP 载荷（2026-11-12）

实机验证 r1/r2 后发现 3DMark 分数不变（5123）且仍有花屏；根因复核确认：

1. `scripts/full_image.py` 原本仅对 `extracted/kernel` 应用了 11 个 `drm-618-a6xx-upstream` 补丁，生成含 `a640_gbif`/`a680_hwcg` 的 `kernel-build/.../msm.ko`（`6579d372…`）。
2. 随后 `scripts/build-lts.py` 编译外部覆盖树 `external/edp-618/drivers/gpu/drm/msm`（同时含 SQ2 eDP/DPU 适配与一份未打补丁的 Adreno GPU 源码），并用产物 `04f37c99…` 覆盖了 `modules-stage/.../msm.ko`，导致最终进盘的 `msm.ko` 缺失全部 11 个 A6xx/A680 修复（§4 此前仅核对了 `kernel-build` 的 `a6xx_catalog.o`，未对最终进盘 `msm.ko` 复核符号）。
3. 进一步在 `5.110` 上实机启动 r3 时发现并定位出第 5 号补丁 `0005-drm-msm-a6xx-Import-a680-hwcg-sequence-from-kgsl.patch` 的主线 MMIO 空间越界缺陷：该补丁把 `REG_A6XX_GMU_GX_GMU_SP_RF_CONTROL_0/1`（`0x1a883`/`0x1a884`，字节偏移 `0x6a20c`/`0x6a210`）放入 `a680_hwcg[]`，但主线 `drm/msm` 中 `gpu@2c00000` 仅映射 `0x40000` B，GMU 单独映射于 `gmu@2c6a000`（`gmu->mmio_offset = 0x6a000`）；`a6xx_set_hwcg()` 无条件调用 `gpu_write()` 会越界写入 `gpu->mmio + 0x6a20c`（在 `5.110` 上命中未映射 `vmalloc` 页引发 Oops 并死锁 `msm_open` 导致黑屏，在未触发故障的机器上也未真正写到 GMU `0x02c6a20c`）。

已完成的修复与 r4 ESP 载荷构建：

- `scripts/full_image.py`：`apply_kernel_patches(..., external_source=extracted/'external', extra_patch_dirs=...)` 将 `drivers/gpu/drm/msm/**` 补丁同步应用到 `kernel` 与 `external/edp-618`，并在使用默认 `patches/drm-618-a6xx-upstream` 时自动追加 `patches/drm-618-a6xx-mmio-fix/0001-drm-msm-a6xx-Route-GMU-offset-hwcg-entries-via-gmu_write.patch`（使 `>= gmu->mmio_offset` 的 `hwcg` 项改走 `gmu_write()`）；校验 GPU 文件在两棵树产出的 SHA256 完全一致，同时拒绝任何触及未同步外部覆盖树的补丁。
- `scripts/build-lts.py`：新增 `validate_msm_gpu_sync()`，在任何 `compile` 路径下校验 `external/edp-618` 非显示 GPU/核心源码与 `kernel` 逐字节一致，失配即拒绝编译；`full-image` 新增 `--esp-only`（配对 `disk_image.py`），跳过 20 GiB 整盘与 gzip 打包，直接导出含完整 `storage_payload` delta 的内置 SSD ESP 树 `disk/esp/`。
- `out/full-mesa-esp-20261112-r4` 与替换包 `out/esp-update-a6xx-20261112/`：
  - `EFI/SQ2Nat/Image`（及 `SQ2Res`）：`418ebc422317e78e86b62e0ff7ad54bf184714e160509d54b85283a16e335313`（35,588,608 B）
  - `EFI/SQ2Nat/initramfs.cpio.gz`（及 `SQ2Res`，含 `storage_payload` delta）：`dd8d797994479f43a47d47899ec44cb52f16c38292555eaeedadb26593257730`（203,616,044 B）
  - 解包与反汇编核实其唯一 `msm.ko`（`8d822403a98f86acd1c9f3844ff9527a910c003381992c7a711135935c69e33e`）同时包含 `a640_gbif`、`a680_hwcg`、`a6xx_set_hwcg` 的 `gmu_write` 偏移路由分支以及 SQ2 DPU/eDP 适配（`dpu_caps_blob_release` 等）。

## 7. 全 Mesa 硬件视频解码与 `/storage/emulated`（`/data` 存储）恢复（2026-11-12）

在 `192.168.5.110` 实机启动全 Mesa 载荷（`EFI/SQ2Msa`）后，定位并修复两项独立回归：

1. **OMX 服务 QTI gralloc 依赖与原生缓冲契约失配**：
   - `--graphics mesa` 移除 `etc/sq2-qti-system` 后，启用了 `QUEST2_NATIVE_SURFACE_TEST` 的 `quest2-omx-service`（`7a6aa750…`）因动态依赖 `_ZN7gralloc15GetBufferLayoutEP16private_handle_tPjS2_S2_` 回落到原厂 vendor 的同名 `libgrallocutils.so`，触发 `CANNOT LINK EXECUTABLE`；
   - VINTF 仍声明 `IOmx/default` 与 `IOmxStore/default`，使 `mediaserver` 与 `mediacodeclist_generator` 持续每秒阻塞重试 `getService`，进而阻塞 `MediaProvider` 挂载 FUSE `/storage/emulated`（表现为应用侧 `/data` 存储无法加载）；
   - 同时该构建对 `OMX_IndexParamVideoAndroidRequiresSwRenderer` 返回 `0`，会令 `ACodec` 在全 Mesa 下分配 `minigbm`（`cros_gralloc_handle`）原生缓冲并交由 QTI `private_handle_t` 校验而失败。
2. **源码构建 `venus-core.ko` 头文件挂钩被覆盖**：
   - `scripts/prepare-full-image-sources.py` 在应用 `patch-sq2-venus-system-pm.py` 与 `patch-sq2-venus-avc-caps.py` 之后复制 `probe/video/*.h` 覆盖了 `external/venus/`，丢失 `venus-sq2-core.h` 的 `venus_sq2_pm_publish(core);` 与 `venus-sq2-system-pm.h` 的 `#include "venus-sq2-avc-export.h"`，导致源码编译出的 `venus-core.ko` 缺少 `sq2_{avc,hevc,vp8,vp9,mpeg2,mpeg1}_caps` 且 `sq2_pm_status` 恒为 `unpublished`。

修复与实机验证：

- 新增 `scripts/full_image_video.py`（配套更新 `scripts/build-lts.py`、`scripts/prepare-full-image-sources.py`、`scripts/full_image.py` 与增量修复脚本 `scripts/patch-mesa-video-initramfs.py`）：
  - 在 `build-lts.py` 编译外部 `venus` 模块前幂等恢复 `venus_sq2_pm_publish(core)` 与 `venus-sq2-avc-export.h` 包含挂钩，并在 `full_image.assemble_runtime` 校验源码编译产出的 `venus-core.ko` 实际导出五类解码能力参数；
  - 在 `mesa_runtime` 移除闭源图形目录前，将 QTI `libgrallocutils.so`（`0fb5d79e…`）仅拷入 `sq2-omx` 私有目录 `etc/sq2-hidl/lib64/`（不写入 `/vendor/lib64/`），并将 `quest2-omx-service` 的 `RequiresSwRenderer` 常量补丁为 `1`（`abbd1b66…`），采用 **Venus 硬解 → ByteBuffer + `SoftwareRenderer`（含 `sq2-p010-renderer`）→ `minigbm` / Mesa `SurfaceFlinger`** 路线。
- `192.168.5.110` 实机部署 `EFI/SQ2Msa/initramfs.cpio.gz`（`9329ff7a1de0d363e5391d96e5aadebc34686212aeda2e44602d2bcb903cee6d`，配套从 `r4` 构建树重编的 `venus-core.ko` `3ec4cc33…` / `venus-dec.ko` `52382a57…`）并正常重启（`boot_id` `d44cff09…` / `6d4e7b28…`）：
  - `init.svc.sq2-omx=running`，`sq2_pm_status` 为 `sys_error=0 core_state=1 insts=0 vb2_bytes=0`，`media_codecs_quest2_omx.xml` 自动生成 `avc/hevc/mpeg2/vp8/vp9` 五项硬解能力，`mediacodeclist_generator` 正常完成退出，FUSE `/storage/emulated` 正常挂载，用户确认恢复可用；
  - 按 `probe/graphics/chrome-graphics-cache.sh` 规范对动态发现的已解锁 `user 0` 下 `com.android.chrome` 执行先停进程、直接删除 6 处确认图形/着色器缓存（`GPUPersistentCache`、`Default/{DawnGraphiteCache,DawnWebGPUCache,GPUCache}`、`user_de` 下 `opengl.shaders_cache.multifile` 与 `skia.shaders_cache`，`cache-backup=none`）并重启 Chrome，账号、普通缓存与其他应用未改动。

## 8. 明确未做（不得提升的边界）

- 仅在 `192.168.5.110` 单机验证了 `EFI/SQ2Msa` 启动、OMX 服务注册、存储挂载与按需 Chrome 图形缓存清理；未据此宣称跨 SQ1/SQ2 整盘重刷、4K60 吞吐/功耗基准、PM 休眠唤醒、长时稳定性或 `full_*` 整栈目标完成。
- 只读的 stock mica vendor/super 分区中仍可能有非本链路使用的闭源文件；本镜像只是不再向系统图形路径加载闭源 EGL/GLES/Vulkan/HWC 层（`sq2-omx` 仅在自身私有库目录携带闭源 OMX 组件所需依赖）。
- Mesa 成员与 20 GiB 模板来自私有输入（`private/build-inputs/`，ignored）；全新 clone 无这些输入时不可复现；本镜像仍含私有 SSH 身份，属私有实验室产物，不得当匿名公共镜像分发。
- 为保持通用镜像策略，`disk_image` 以 `--build-only` 运行，其 `IMAGE-MANIFEST.json` 中
  postbuild 读回/全流校验字段为 False/None；§4 的 gzip 全流与 GPT 校验是本轮另外独立执行的。
- 历史 6.3.8 图形入口、已验收发布包与设备现有回退均未改动。

## 9. 回滚

输出全部在 ignored `out/`，删除对应目录即可；`192.168.5.110` 更新前原 `SQ2Msa/initramfs.cpio.gz`（`90473e9b…`）保存在本地 `out/omx-mesa-fix-20261112/device/` 可随时原子恢复；仓库侧恢复对应脚本修改即可，私有输入与发布 pins 未被写入。
