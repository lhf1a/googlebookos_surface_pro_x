# Suspend (s2idle)

## Fixes

- **Android wakelock**: Reduced the charging screen-off `WakeLock.acquire(J)` in `DesktopPowerService$screenStateReceiver$1::onReceive` from `1585000` ms to `1000` ms via the DEX parser.
- **Venus no-TZ suspend**: Preserved the firmware transport device and IOMMU domain across PM cycles; `venus_sq2_pm_stop()` calls `venus_hfi_prepare_system_stop()` before disabling IRQs, resetting the CPU, and unmapping, reusing the V4 `idle` / `PC_PREP` / `PC_READY` handshake. Non-SQ2 full suspend continues using `venus_power_off()`.
- **AOSS/RPMh deadlock prevention**: [0013-interconnect-qcom-sc8180x-mm0-keepalive.patch](../patches/spx-portable-boot/0013-interconnect-qcom-sc8180x-mm0-keepalive.patch) adds `.keepalive = true` to `bcm_mm0` to prevent AOSS deadlocks when display/video withdraw votes during `s2idle`.
- **GCC CX domain protection**: [0014-clk-qcom-gcc-sc8180x-enable-rpm.patch](../patches/spx-portable-boot/0014-clk-qcom-gcc-sc8180x-enable-rpm.patch) enables `.use_rpm = true` on `gcc_sc8180x_desc` to prevent the CX power domain from powering down while GCC is active.
- **MMCX minimum operating level on resume**: [0019-pmdomain-qcom-rpmhpd-skip-retention-mmcx.patch](../patches/spx-portable-boot/0019-pmdomain-qcom-rpmhpd-skip-retention-mmcx.patch) sets `.skip_retention_level = true` on `mmcx` and `mmcx_ao` so `rpmhpd_update_level_mapping()` selects `enable_corner = LOW_SVS (0x40)` matching SC8180X `cmd-db` `mmcx.mol = 0x40` (while still allowing `corner = 0` power-off when idle). Without this, when display, Venus, and camera all runtime-suspend simultaneously (e.g., screen off with no external DP connected after Venus idle autosuspend), `rpmhpd_power_on(mmcx)` votes `RETENTION (0x10)` below `mmcx.mol`, causing all 7 MMCX GDSCs (`mdss_gdsc`, `titan_top_gdsc`, `ife_{0,1}_gdsc`, `venus_gdsc`, `vcodec{0,1}_gdsc`) to time out (`status stuck at 'off'`), poisoning `ae00000.display-subsystem` into `runtime_status = error` and leaving the screen black on wake. Verified on SQ2 across 3 consecutive `s2idle` cycles with external DP disconnected (`mmcx` `off-0` 12 cycles / 85.6 s, `mdss_gdsc` `off-0` 10 cycles / 69.0 s, 0 GDSC/display/HWC errors); SQ1 hardware re-test and multi-hour overnight standby remain to be observed.
- **Power button PDC wake routing**: [prepare-spx-spmi-pdc-dtb.py](../scripts/prepare-spx-spmi-pdc-dtb.py) and [0016-arm64-dts-qcom-sc8180x-spmi-pdc-wake.patch](../patches/spx-portable-boot/0016-arm64-dts-qcom-sc8180x-spmi-pdc-wake.patch) switch `spmi@c440000` to `interrupts-extended = <&pdc 1 IRQ_TYPE_LEVEL_HIGH>`, allowing `pm8941_pwrkey` to wake APSS via PDC pin 1.
- **UCSI suspend workqueue race fix**: [0015-usb-typec-ucsi-cancel-resume-work-on-suspend.patch](../patches/spx-portable-boot/0015-usb-typec-ucsi-cancel-resume-work-on-suspend.patch) synchronously cancels `resume_work` in `ucsi_suspend()`, and [ucsi_surface_hid.c](../probe/typec/ucsi_surface_hid.c) cancels `notify_work` before and after `ucsi_suspend()`.
- **On-demand stats**: [kernel-6.18-hybrid.config](../configs/kernel-6.18-hybrid.config) enables `CONFIG_QCOM_STATS=y`.

## Build Integration

`build.py full` defaults to `--kernel-mode source`, invoking `full_image_video.ensure_venus_source_hooks()` on the external Venus build copy to apply patches and compile without modifying original input sources.

Android JARs are not committed to Git and must be supplied from the same Android system image:

```sh
python3 scripts/full_image_suspend.py \
  --desktop-services /ABS/ANDROID/system/framework/desktop-services.jar \
  --output /ABS/PRIVATE/build-inputs/suspend
python3 scripts/build.py full --private-root /ABS/PRIVATE/build-inputs --output /ABS/OUTPUT
```

With `--kernel-mode prebuilt`, a patched module matching the selected `Image` must be supplied via `--venus-core ... --image ...`; the manifest binds both SHA256 hashes and errors on missing or mismatched inputs.

The first-boot helper performs a one-time migration (`screen_off_timeout=2147483647 → 60000`, `stay_on_while_plugged_in=15 → 0`) and writes a completion marker so existing user settings are not reset again.
