# 默认 s2idle 修复（2026-11-16）

## 已确认的修复与边界

- `DesktopPowerService$screenStateReceiver$1::onReceive` 的充电息屏
  `WakeLock.acquire(J)` 从 1585000 ms 缩短为 1000 ms。使用现有 DEX 解析器定位，
  更新 DEX 校验及原日志；不清除应用锁、不更改凭据或伪造电池状态。
- Venus no-TZ 固件传输设备与 IOMMU domain 跨 PM 周期保留；固件 CPU 仍按原语义停止，
  映射仍解除，错误仍传播。仅保留设备不能解决恢复超时，失败证据未当作通过。
- `venus_sq2_pm_stop()` 在关闭 IRQ、复位 CPU、解除映射前调用
  `venus_hfi_prepare_system_stop()`，复用 V4 idle / PC_PREP / PC_READY 握手。
  非 SQ2 完整 suspend 继续执行原 `venus_power_off()`，没有放宽超时或清除终态错误。

SQ2 实测：相同启动中 `success=4`，Venus `stops=5/starts=5`、`error=0`、
`failed_resume=0`。四段 machine_suspend 为 0.009790、0.058555、0.067198、
59.199252 秒；完整轨迹 31819/31819 条无丢失。用户确认显示、触摸、播放、声音和拖动正常。
此前驱动恢复 `hfi-resume=-110`；加入握手后连提前中止的恢复也成功。
仍有 timerfd 提前唤醒与 freezer 中止，不能称零中止、Windows Modern Standby 认证、
长时功耗或 SQ1 验收。用户播放确认不等于独立逐帧硬解像素验证。

## 默认构建

`build.py full` 的默认 `--kernel-mode source` 会在 external Venus **构建副本**上
调用 `full_image_video.ensure_venus_source_hooks()`，应用两份 s2idle 补丁，再实际编译。
原始输入源码不修改；重复调用幂等，未知源拒绝。独立 `--system-sleep` 准备也接同一路径。

Android JAR 不进 Git。由同一 Android system 镜像提供一次可复用依赖：

```sh
python3 scripts/full_image_suspend.py \
  --desktop-services /ABS/ANDROID/system/framework/desktop-services.jar \
  --output /ABS/PRIVATE/build-inputs/suspend
python3 scripts/build.py full --private-root /ABS/PRIVATE/build-inputs --output /ABS/OUTPUT
```

其他既有 camera/sensor/graphics 私有依赖仍按原构建文档准备。构建时解析并补丁 JAR，
handoff 前 bind 到 `/newroot/system/framework/desktop-services.jar`。
source 模式只选本次新编译的 Venus core，替换所有有效副本；不复用同版本旧模块。
没有默认实验 trace、pm_test、临时唤醒门或 SSH 一次性诊断操作。

`--kernel-mode prebuilt` 必须另提供**与所选 Image 匹配**的修复模块：上述准备命令增加
`--venus-core /ABS/FIXED/venus-core.ko --image /ABS/PAIRED/Image`。
预编译 manifest 绑定两者 SHA256；缺失/不匹配报错，不能只改 vermagic 假称匹配。
本次实机模块配对 Image 与旧私有 baseline Image 不同，不能将它直接混装到旧 baseline；
默认源码构建不依赖这个预编译模块。

新 helper 仅一次迁移 donor 的 `screen_off_timeout=2147483647` 到 60000、
`stay_on_while_plugged_in=15` 到 0；已有其他用户选择不改，记录完成标记后不重复重置。
现机这两个值已持久为 60000/0，不需要每次启动重写设置。

## 设备默认入口与回退

用户明确授权后，GRUB 默认项从 `sq2-hybrid-618-a6xx` 改为已验收的 `sq2-sensors`，
仍加载 `EFI/SQ2Sns/el2launch.efi`，`next_entry` 为空。原 Hybrid/Mesa 菜单及载荷保留。
已实际执行一次不写 one-shot 的重启：新 boot ID、内核 initrd 路径指向 SQ2Sns，
加载模块 BuildID 与验收模块一致，Android boot_completed=1，用户确认桌面亮屏。
默认入口保留原已验收候选字节（含只读诊断日志/采集器）；公共默认构建不安装实验采集器。
本次不复制私有启动包、固件、凭据、原始设备日志或用户媒体到 Git。

主机验证包括补丁幂等/未知输入、全部模块副本替换、默认构建接入、原设置保留、
真实匹配 ARM64 编译、实际 JAR 结果与已验收 JAR 字节相同、真实 initramfs 独立解包，
以及原/改/实际回滚三态。完整磁盘镜像没有在此次重新构建；新 source 构建也不自动继承
整机硬件验收。原始四角色及扩展证据留本地 ignored 的 `out/suspend-policy-20261116`。

本轮 `test_full_image*.py`：43 项，35 通过、8 因可选私有依赖缺失跳过。
休眠专项包含 7 项；设置迁移运行真实 shell 分支、模拟 Android settings API，
验证旧值迁移、已有自定义值保持、完成标记后不重复改值、写入失败不生成完成标记。
