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

目录：`/mnt/spx-workspace/chromebookOS/out/hybrid-current-20261116/image/`（本地成品，不是公开下载地址；
镜像、固件与私有运行库不随 Git 发布）。

| 产物 | 字节数 | SHA256 |
| --- | --- | --- |
| `disk/surface-pro-x-6.18-hybrid-generic.img` | 21474836480 | `d2fcdd9704b3d039de2654cd814a0e75d1a937de640c053cfff0a6d9aed0c37e` |
| `disk/surface-pro-x-6.18-hybrid-generic.img.gz` | 8199981130 | `1ffa84b553b5c504a3cbac42c3aa1d5f2cc48a8c9de4b96add09efa1f72e17a5` |
| 最终 initramfs | - | `f60315c86fb9b6305b5077c08c6857e2f40290ba7645a31bae93664eb9bc8bfa` |

20 GiB 原始容量，压缩约 7.64 GiB；解压后是整盘镜像，不是单独 ESP/EFI 载荷。

```sh
cd IMAGE_OUTPUT
sha256sum -c SHA256SUMS
gzip -t disk/surface-pro-x-6.18-hybrid-generic.img.gz
```

## 验证范围

主机侧已通过：完整构建 exit 0、GPT 主/备、FAT/ESP 全文件回读、最终 ESP、完整 gzip
解压大小与 SHA256、userdata/metadata 仅含 lost+found、六个继承身份分区清零、
39 项首启/复用/HOME/触控测试。

未做：设备写入、重启、SQ1/SQ2 真机启动、长期休眠与功耗验收。
构建成功和主机校验不能替代硬件验收。