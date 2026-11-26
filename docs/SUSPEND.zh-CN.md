# 休眠（s2idle）

## 修复内容

- **Android 唤醒锁**：通过 DEX 解析器将 `DesktopPowerService$screenStateReceiver$1::onReceive` 的充电息屏 `WakeLock.acquire(J)` 从 `1585000` ms 缩短为 `1000` ms。
- **Venus no-TZ 休眠**：跨 PM 周期保留固件传输设备与 IOMMU domain；`venus_sq2_pm_stop()` 在关闭 IRQ、复位 CPU 与解除映射前调用 `venus_hfi_prepare_system_stop()`，复用 V4 `idle` / `PC_PREP` / `PC_READY` 握手。非 SQ2 完整 suspend 继续执行原 `venus_power_off()`。
- **AOSS/RPMh 防死锁**：[0013-interconnect-qcom-sc8180x-mm0-keepalive.patch](../patches/spx-portable-boot/0013-interconnect-qcom-sc8180x-mm0-keepalive.patch) 为 `bcm_mm0` 增加 `.keepalive = true`，避免显示/视频子系统在 `s2idle` 撤票时触发 AOSS 死锁。
- **GCC CX 域保护**：[0014-clk-qcom-gcc-sc8180x-enable-rpm.patch](../patches/spx-portable-boot/0014-clk-qcom-gcc-sc8180x-enable-rpm.patch) 为 `gcc_sc8180x_desc` 启用 `.use_rpm = true`，防止 CX 电源域在 GCC 活跃期间意外掉电。
- **MMCX 唤醒最低工作电平修复**：[0019-pmdomain-qcom-rpmhpd-skip-retention-mmcx.patch](../patches/spx-portable-boot/0019-pmdomain-qcom-rpmhpd-skip-retention-mmcx.patch) 为 `mmcx` 与 `mmcx_ao` 设置 `.skip_retention_level = true`，使 `rpmhpd_update_level_mapping()` 将 `enable_corner` 对齐到 SC8180X `cmd-db` 的硬件 `mmcx.mol = 0x40 (LOW_SVS)`（空闲时仍允许正常掉电至 `corner = 0`）。缺少该配置时，若显示、Venus 与相机同时完成 runtime-suspend（例如无外接 DP 且息屏后 Venus 5 秒自动释放），下一次唤醒会向 `mmcx` 投出低于 `mmcx.mol` 的 `RETENTION (0x10)`，导致全部 7 个 MMCX GDSC（`mdss_gdsc`、`titan_top_gdsc`、`ife_{0,1}_gdsc`、`venus_gdsc`、`vcodec{0,1}_gdsc`）超时（`status stuck at 'off'`），并将 `ae00000.display-subsystem` 永久置为 `runtime_status = error` 引发唤醒黑屏。已在 SQ2 无外接 DP 条件下实测连续 3 轮 `s2idle`（`mmcx` 下电 12 次 / 85.6 秒，`mdss_gdsc` 下电 10 次 / 69.0 秒，0 GDSC/显示/HWC 报错）；SQ1 实机复测与整夜长时待机仍待进一步观察。
- **电源键 PDC 唤醒路由**：[prepare-spx-spmi-pdc-dtb.py](../scripts/prepare-spx-spmi-pdc-dtb.py) 与 [0016-arm64-dts-qcom-sc8180x-spmi-pdc-wake.patch](../patches/spx-portable-boot/0016-arm64-dts-qcom-sc8180x-spmi-pdc-wake.patch) 将 `spmi@c440000` 切换为 `interrupts-extended = <&pdc 1 IRQ_TYPE_LEVEL_HIGH>`，使 `pm8941_pwrkey` 经 PDC pin 1 参与 APSS 唤醒。
- **UCSI 休眠工作队列竞态修复**：[0015-usb-typec-ucsi-cancel-resume-work-on-suspend.patch](../patches/spx-portable-boot/0015-usb-typec-ucsi-cancel-resume-work-on-suspend.patch) 在 `ucsi_suspend()` 同步取消 `resume_work`，[ucsi_surface_hid.c](../probe/typec/ucsi_surface_hid.c) 在 `ucsi_suspend()` 前后取消 `notify_work`。
- **按需观测**：[kernel-6.18-hybrid.config](../configs/kernel-6.18-hybrid.config) 开启 `CONFIG_QCOM_STATS=y`。

## 构建接入

`build.py full` 默认 `--kernel-mode source`，在 external Venus 构建副本上调用 `full_image_video.ensure_venus_source_hooks()` 应用补丁并编译，不修改原始输入源码。

Android JAR 不进 Git，需由同一 Android system 镜像提供：

```sh
python3 scripts/full_image_suspend.py \
  --desktop-services /ABS/ANDROID/system/framework/desktop-services.jar \
  --output /ABS/PRIVATE/build-inputs/suspend
python3 scripts/build.py full --private-root /ABS/PRIVATE/build-inputs --output /ABS/OUTPUT
```

使用 `--kernel-mode prebuilt` 时必须通过 `--venus-core ... --image ...` 提供与所选 `Image` 匹配的修复模块；manifest 绑定两者 SHA256，缺失或不匹配时报错。

首次启动 helper 执行一次性迁移：`screen_off_timeout=2147483647 → 60000`、`stay_on_while_plugged_in=15 → 0`，写入完成标记后不重复重置已有用户设置。
