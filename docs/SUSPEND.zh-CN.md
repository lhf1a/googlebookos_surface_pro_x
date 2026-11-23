# 休眠（s2idle）

## 修复内容

- **Android 唤醒锁**：通过 DEX 解析器将 `DesktopPowerService$screenStateReceiver$1::onReceive` 的充电息屏 `WakeLock.acquire(J)` 从 `1585000` ms 缩短为 `1000` ms。
- **Venus no-TZ 休眠**：跨 PM 周期保留固件传输设备与 IOMMU domain；`venus_sq2_pm_stop()` 在关闭 IRQ、复位 CPU 与解除映射前调用 `venus_hfi_prepare_system_stop()`，复用 V4 `idle` / `PC_PREP` / `PC_READY` 握手。非 SQ2 完整 suspend 继续执行原 `venus_power_off()`。
- **AOSS/RPMh 防死锁**：[0013-interconnect-qcom-sc8180x-mm0-keepalive.patch](../patches/spx-portable-boot/0013-interconnect-qcom-sc8180x-mm0-keepalive.patch) 为 `bcm_mm0` 增加 `.keepalive = true`，避免显示/视频子系统在 `s2idle` 撤票时触发 AOSS 死锁。
- **GCC CX 域保护**：[0014-clk-qcom-gcc-sc8180x-enable-rpm.patch](../patches/spx-portable-boot/0014-clk-qcom-gcc-sc8180x-enable-rpm.patch) 为 `gcc_sc8180x_desc` 启用 `.use_rpm = true`，防止 CX 电源域在 GCC 活跃期间意外掉电。
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
