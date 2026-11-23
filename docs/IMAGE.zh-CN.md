# 镜像策略、布局与触摸校准

默认构建为 Hybrid，SQ1/SQ2 共用 Linux `6.18.60-sq2-android1`。构建命令见 [BUILD](BUILD.zh-CN.md)。

## 首次启动策略

`surface-first-boot` 在 Android 启动完成且当前为用户 0 后：

- 设置 `device_provisioned=1` / `user_setup_complete=1`；
- 停用已安装的初始设置向导并启动 HOME；
- 成功写入 `/data/system/surface-first-boot-v1` 后后续启动不重复执行，失败则下次重试。

原有 HOME 修复保留，不覆盖用户选择的桌面。`PRIVACY.json` 记录 `first_setup_preserved=false`、`first_setup_skipped=true`。

镜像清除继承的账户、Wi-Fi 与 SSH 凭据，锁定 root、默认关闭 SSH，使用全新 `userdata`/`metadata`，不关闭 captive portal 探测或预置网络。跳过 Android 初始设置不跳过独立的触摸校准向导。

## 触摸校准向导

向导从 `probe/touch-calibration/` 源码构建，包含中英双语界面，保留滑动、静置、单指点击、坐标和拇指检测流程及确认/撤销行为。默认不自动循环优化：零意外触点与残余信号分开报告，无参数改动不强制复测，坐标与拇指为按需专项；学习到的配置按设备+屏幕+描述符派生键存储。

仅向导 Java/Manifest 变化时，`full_image_reuse.prepared_runtime` 会使旧 APK 失效并重编，图形适配库与 `userdata` 扩容工具继续复用。

## 分区布局与校验

默认采用精简 5 分区布局，整盘原始容量 8 GiB：

- `super`：3.40 GiB，仅含 6 个 `_a` EROFS 切片
- `metadata`：64 MiB
- `misc`：4 MiB
- `SQ2PROBE`：1 GiB
- `userdata`：3.53 GiB，首次启动自动扩容

校验压缩镜像完整性：

```sh
gzip -t surface-pro-x-6.18-hybrid-generic.img.gz
```
