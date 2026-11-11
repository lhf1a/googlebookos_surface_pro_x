# KGSL + DRM/SDE 真机 bring-up 进度 — 2026-11-11

目标："实现KGSL-SDE，并在真机调试运行"。Round3 完成**真机侧可行性与启动载荷准备**：
设备已重新确认可达、内核/配置与候选一致性核对、候选内核+模块+initramfs 载荷构建并在
**QEMU 启动路径**上验证，且载荷已上传到真机 ESP 的独立目录（**未改启动默认、未重启**）。

## 2026-11-11 更新：匹配内核已在真机启动并进入 Android 桌面

**结果（已实测）**：SQ2 设备已用 `EFI/SQ2Sde` 候选载荷启动 `6.18.60-sq2-kgsl-sde-mst-dev1`
（`#1 SMP Wed Nov 11 15:54:37 CST 2026`），`boot_id=65a7d969-…`，cmdline
`initrd=\EFI\SQ2Sde\initramfs.cpio.gz`；GRUB one-shot 已消费清空；Android
`sys.boot_completed=1`、SurfaceFlinger 与 `vendor.hwcomposer` 均 running、launcher 运行、
`card0-eDP-1 status=connected`，截图显示完整 Android 桌面（时间/WiFi/电量/任务栏）。
运行中的显示驱动是已验收的上游 MSM（`msm_dpu`）；**KGSL native 与 SDE `msm_drm.ko` 尚未加载**。

### 失败与修正（全过程保留）

1. **one-shot 未生效**：grub.cfg 缺少 `load_env`，`next_entry` 从未被读取；补上
   `if [ -s ${prefix}/grubenv ]; then load_env; fi` 与 `save_env` 后生效。
2. **仍进 native**：`start.efi`/`el2launch.efi` 内嵌 UTF-16 常量硬编码 `\EFI\SQ2Nat\...`
   （含完整 kernel cmdline），仅换目录无效。按 `scripts/prepare-sq2-618-debug-entry.py` 的
   只改 UTF-16 常量方式，把 router/launcher 的 `SQ2Nat→SQ2Sde` 同长度替换（PE 布局不变），并保持
   kernel-options 与内嵌 cmdline 一致。
3. **内核起来但 Android HWC abort**：失败 boot 的 KSU 持久 logcat 显示
   `SDM: DRMCrtcManager::Init: CRTC capabilities failed: -95`、`Failed to get hwcomposer instance`，
   composer-service 反复退出（status 234）。根因：重建 external `msm.ko` 用的
   `full-image-source-20261110-v1/external/edp-618` 缺少项目显示补丁；设备验收版本来自
   `gpu-submit-efficiency-20261110-v1/combined-v5/edp-618`（hash `7ee05cae…`，含 dpu caps/
   release fence/scheduler/exec-module）。用 combined-v5 源对新内核重建 `msm.ko`
   （sha `59104470…`，vermagic `6.18.60-sq2-kgsl-sde-mst-dev1`）后 HWC 正常、Android 完整启动。
4. **可调试性/回退**：候选 initramfs 的 init 在 ssd 路径写 `sde-*` 日志到 ESP（boot marker/
   dmesg 快照/module-load）、启动早期 SSH、420s 内无 SurfaceFlinger 自动重启回 native；
   one-shot 启动前清除，断电重启即回 android1 默认。重启统一用 Android 方式
   （`nsenter -t 1 -m -- /system/bin/reboot`），不用 Linux `reboot` 系统调用。

### 证据

- `out/kgsl-resume-2/round3/DEVICE-BOOT-20261111.json`
- `out/kgsl-resume-2/round3/device/candidate-boot-verify.txt`、`candidate-boot-evidence/`
  （dmesg、`sq2-sde-boot.png` Android 桌面截图）、`esp-logs/sde-*`
- logcat 对照：`device/ksu-logs/log/logcat.old.log`（失败 boot）与 `logcat.log`（成功 boot）
- 设备侧运行中 `msm.ko` sha `59104470…`（combined-v5 源对新内核重建产物）

### 下一步

1. **DT 是 SDE/KGSL 的硬前提**：当前
   `/proc/device-tree/soc@0/display-subsystem@ae00000/compatible` 只有 `qcom,sc8180x-mdss`
   （上游风格），没有下游 `qcom,mdss`/`qcom,sde*` 与 `qcom,kgsl-3d0` 节点；需先给出
   DT overlays/DTB 方案，native KGSL 与 techpack SDE 才能 probe。
2. 显示栈切换：initramfs `prepare-native-gpu` 由上游 `msm` 切换为 `msm_drm.ko`（SDE），
   配合 DT 方案先 QEMU 后真机；KGSL 与上游 drm/msm GPU 二选一。
3. 之后才是 SDE 物理显示、真实提交/fence、PM/性能与 SQ1/SQ2 验收。`hardware_validated`
   目前只对"匹配内核启动 Android（上游显示栈）"成立，SDE/KGSL 显示仍未验证。

## 设备现状（只读核查，2026-11-11）

- 地址/身份：`root@192.168.5.110`（SSH key `codex_sq2_port_20261026`，root）
- 运行内核：`6.18.60-sq2-android1`（`#2 SMP Fri Nov 6 17:53:18 CST 2026`）
- 显示栈：**上游 mainline MSM DRM 模块**（`/lib/modules/<release>/kernel/drivers/gpu/drm/msm/msm.ko`，
  `DRIVER=msm_dpu`，vendor composer-service + SurfaceFlinger 正常）
- 启动：GRUB（`/media/esp/EFI/BOOT/grub.cfg`）→ `EFI/SQ2Nat/start.efi`（默认）；
  回退 `EFI/SQ2QDbg`；另 `EFI/SQ2Dbg`。ESP = `/dev/nvme0n1p6` (2.0G，部署前 946.9M 已用/1.1G 空闲)。
- initramfs 内含该 release 的 486 个内核模块，`init` 的 `prepare-native-gpu` 加载
  `phy-qcom-edp/dispcc-sm8250/pwm_bl/msm`。
- 已下载当前载荷（`Image db374853…`、`initramfs 180b1900…`、dtb/EFI loader/grub 等）到本地
  ignored work 目录，作为候选构造与回滚基线。

## 内核一致性（关键门禁）

用设备 `/proc/config.gz` 与候选 matched kernel `.config` 逐项比较：**2564 项中仅 1 项不同**——
`CONFIG_LOCALVERSION`（`-sq2-android1` → `-sq2-kgsl-sde-mst-dev1`）。即候选内核与
已验收 android1 基线同源同配置，仅增加本项目源码补丁（提供者 + MST persistent core）与 release 名。
证据：`out/kgsl-resume-2/round3/device/CONFIG-COMPARISON.json`。

## 候选载荷（已构建并上传，未激活）

- matched kernel：`kernel-mst1`/`kernel-out-mst1`，release `6.18.60-sq2-kgsl-sde-mst-dev1`，
  Image `e0afdadd…`，Module.symvers `e6fc06cf…`，导出 `drm_dp_atomic_release_port_payload`。
- 模块 stage（497 个，vermagic 全匹配，depmod 0 错）：
  - 全量 in-tree 模块（modules_install）
  - 12 组项目 external override 以新 KOUT 重编（msm/venus/spi-hid/ssam/tsens/battery/ipa/identify/
    hfi/fw/videocc/rmnet）
  - **native KGSL58**：`msm_kgsl.ko` + `governor_gpubw_mon.ko` + `governor_msm_adreno_tz.ko`
  - **SDE 端口**：`msm_drm.ko`（integrated-20 编译，sha `cb34d88e…`）
- initramfs：取设备当前 initramfs，仅替换 `/lib/modules/<release>`（497 模块），其它
  init/脚本/固件不变；sha `bb3247cd…`
- 候选载荷 8 文件 + SHA256SUMS 位于 `EFI/SQ2Sde`；**已上传到真机 ESP 并逐项 sha256 校验通过**；
  启动配置完全未改。

## QEMU 启动路径验证（候选 vs 当前基线，同参数）

两个 case 结果一致：均打印 `SQ2_GOOGLEBOOK_NATIVE_PREP`，均从 release 模块树成功
`modprobe af_key/fuse`，均挂载 fake system/vendor/product/system_ext/system_dlkm/vendor_dlkm 与
tmpfs debug_ramdisk，并同样停在 `SQ2_NATIVE_PREP_FAILED: security-overlay`（仅因 QEMU 假分区
没有 Android 内容）。候选内核版本正确。这是**启动路径 smoke 证据，不是 Android/显示验收**。
证据：`out/kgsl-resume-2/round3/DEVICE-BRINGUP-EVIDENCE.json` 与 `evidence/qemu-*.log`。

## 一键候选启动与回滚（脚本已就绪，未执行）

- `out/kgsl-resume-2/round3/deploy-candidate.sh --stage-only|--activate`
  - stage-only：上传 `EFI/SQ2Sde` 并校验（本轮已完成）
  - activate：安装 GRUB 一次性 `next_entry=sq2-qcom-618-sde`（grubenv），重启；GRUB 在启动前
    消费并清除 next_entry，因此**下一次断电重启自动回到 native 默认**，不修改 `EFI/SQ2Nat` 或回退项。
- `out/kgsl-resume-2/round3/rollback-candidate.sh`：恢复部署前 `grub.cfg`、清空 grubenv、重启回 native。
- 已记录的失败模式：若候选在 init 前 panic/挂死（cmdline `panic=0`），需**人工断电重启**后才能回到
  native 默认；设备屏幕/物理可达性是激活前提。

## 未完成

1. 候选内核尚未在真机启动（仅 staging）；真机 boot/Android 行为未验证。
2. 显示栈切换未开始：当前 initramfs 仍加载上游 `msm`；要验证 KGSL-SDE，必须改
   `prepare-native-gpu` 为加载 `msm_drm.ko`（SDE）并避免与上游 msm/GPU 驱动冲突，
   同时评估 QTI HWC 对下游 SDE DRM 的兼容性；native KGSL 与上游 drm/msm GPU 也只能二选一。
3. 物理显示、真实提交/fence 退休、PM/性能、SQ1/SQ2 验收全部未做；`hardware_validated=false`。

## 下一轮计划

1. 先激活"仅换内核"候选（上游显示栈不变）验证真机 boot/Android/SSH，记录 boot_id 与 dmesg；
   失败则断电回 native 并保留日志。
2. 在候选 initramfs 中把 `prepare-native-gpu` 切换到 `msm_drm.ko`（SDE）+ 按需 KGSL，
   先 QEMU 验证绑定/日志，再一次性候选启动做真机显示 bring-up。
3. 显示通路稳定后再做提交/fence/PM/性能与 SQ1/SQ2 验收，更新 `full_*` 边界。
