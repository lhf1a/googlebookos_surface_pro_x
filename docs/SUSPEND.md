# 休眠（s2idle）

默认构建已接入 SQ2 s2idle 修复，主机与 SQ2 实机各一次验证；不是零中止、Windows
Modern Standby 认证、长时功耗或 SQ1 验收。

## 修复内容

- Android `DesktopPowerService$screenStateReceiver$1::onReceive` 的充电息屏
  `WakeLock.acquire(J)` 从 1585000 ms 缩短为 1000 ms；用现有 DEX 解析器定位并更新校验，
  不清除应用锁、不改凭据、不伪造电池状态。
- Venus no-TZ 固件传输设备与 IOMMU domain 跨 PM 周期保留；固件 CPU 仍按原语义停止，
  映射仍解除，错误仍传播。只保留设备不能解决恢复超时。
- `venus_sq2_pm_stop()` 在关闭 IRQ、复位 CPU、解除映射前调用
  `venus_hfi_prepare_system_stop()`，复用 V4 idle / PC_PREP / PC_READY 握手。
  非 SQ2 完整 suspend 继续执行原 `venus_power_off()`，不放宽超时、不清除终态错误。

## 实测范围

SQ2 同一次启动：`success=4`，Venus `stops=5/starts=5`、`error=0`、`failed_resume=0`；
四段 machine_suspend 为 0.009790、0.058555、0.067198、59.199252 秒，完整轨迹
31819/31819 条无丢失；用户确认显示、触摸、播放、声音和拖动正常。

仍有 timerfd 提前唤醒与 freezer 中止，不能称零中止或长时功耗已验收；
用户播放确认不等于独立逐帧硬解像素验证。

## 构建接入

`build.py full` 默认 `--kernel-mode source`，在 external Venus 构建副本上调用
`full_image_video.ensure_venus_source_hooks()`，应用两份 s2idle 补丁后实际编译；
原始输入源码不修改，重复调用幂等，未知源拒绝。

Android JAR 不进 Git，由同一 Android system 镜像提供：

```sh
python3 scripts/full_image_suspend.py \
  --desktop-services /ABS/ANDROID/system/framework/desktop-services.jar \
  --output /ABS/PRIVATE/build-inputs/suspend
python3 scripts/build.py full --private-root /ABS/PRIVATE/build-inputs --output /ABS/OUTPUT
```

`--kernel-mode prebuilt` 必须另提供与所选 Image 匹配的修复模块（增加
`--venus-core ... --image ...`）；manifest 绑定两者 SHA256，缺失/不匹配报错，
不能只改 vermagic 假称匹配。source 模式只选本次新编译的 Venus core，替换所有有效副本。

新 helper 只做一次性迁移：`screen_off_timeout=2147483647 → 60000`、
`stay_on_while_plugged_in=15 → 0`；已有其他用户选择不改，写完成标记后不重复重置。

## 边界

完整磁盘镜像没有随本次修复重新构建；新 source 构建也不自动继承整机硬件验收。
正常入口保留原已验收候选字节（含只读诊断日志/采集器），公共默认构建不安装实验采集器。