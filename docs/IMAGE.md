# 当前镜像、首启策略与触摸校准

默认构建为 Hybrid，SQ1/SQ2 共用 Linux `6.18.60-sq2-android1`。构建命令见 [BUILD](BUILD.md)。

## 首次启动策略

`surface-first-boot` 在 Android 启动完成且当前为用户 0 后：

- 设置 `device_provisioned=1` / `user_setup_complete=1`；
- 仅停用实际安装的初始设置向导，然后启动 HOME；
- 成功写入 `/data/system/surface-first-boot-v1`，后续启动不重复执行；命令失败不写成功标记，下次启动重试。

原有 HOME 修复保留，不强制覆盖用户选择的桌面。`PRIVACY.json` 记录
`first_setup_preserved=false`、`first_setup_skipped=true`。

镜像仍清除继承的个人账户、Wi-Fi 和 SSH 凭据，锁定 root、默认关闭 SSH，使用全新
userdata/metadata；不关闭 captive portal 探测、不预置网络。
跳过 Android 初始设置不等于跳过独立的触摸校准向导。

## 触摸校准向导

向导从当前 `probe/touch-calibration/` 源码构建（中英双语），保留滑动、静置、单指点击、
坐标和拇指检测流程及确认/撤销行为。默认不再自动循环优化：零意外触点与残余信号分开报告，
无参数改动不强制复测，坐标/拇指为按需专项。算法共享，学习到的配置按设备+屏幕+描述符派生键存储。

仅向导 Java/Manifest 变化时，`full_image_reuse.prepared_runtime` 会让旧 APK 失效并由正常
装配流程重编；图形适配库与 userdata 扩容工具继续复用。

## 当前本机构建成品

目录：`/mnt/spx-workspace/chromebookOS/out/hybrid-main-minimal-5part-20261118/disk/`（本地成品，不是公开下载地址；
镜像、固件与私有运行库不随 Git 发布）。

| 产物 | 字节数 | SHA256 |
| --- | --- | --- |
| `surface-pro-x-6.18-hybrid-generic.img` | 8589934592 | `e781f3c5a2459a798c6c47f81b82de6bbc41b3f6fd23fb685793610dd50c70fa` |
| `surface-pro-x-6.18-hybrid-generic.img.gz` | 3685632793 | `9f51c5986aa02e705b2bbfc7aebe11d3411f25c6893bad71b28b8d4449c6de09` |
| 最终 initramfs | - | `f80a63b53e145e60759e60fc7dd931d9cbadf3de965c505278a4f7f02f9205ba` |

默认精简 5 分区布局（`super` 3.40 GiB 仅含 6 个 `_a` EROFS 切片、`metadata` 64 MiB、`misc` 4 MiB、`SQ2PROBE` 1 GiB、`userdata` 3.53 GiB）：8 GiB 原始容量，压缩约 3.43 GiB。

```sh
gzip -t surface-pro-x-6.18-hybrid-generic.img.gz
```

## 验证范围

主机侧已通过：完整构建 exit 0、GPT 主/备、FAT/ESP 全文件回读、最终 ESP、完整 gzip
解压大小与 SHA256、6 个压缩后 EROFS 切片魔数校验、userdata/metadata 仅含 lost+found、misc 清零。
另在远程 Surface Pro X Windows 11 ARM64（`192.168.5.76`）通过 PowerShell 双启动脚本完成 `C:` 压缩、5 分区创建与流式写入、`WindowsESP\EFI\GooglebookOS\` GRUB 双菜单写入及 `{fwbootmgr}` UEFI 注册检查。

未做：重启进入 GRUB 菜单后的双系统真机冷启动、长期休眠与功耗验收。
构建成功、主机校验及 Windows 写盘准备不能替代硬件启动验收。