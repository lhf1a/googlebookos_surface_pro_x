# 2026-11-16：默认跳过初始设置与最新触摸校准镜像

## 默认行为

唯一默认构建仍为 hybrid，SQ1/SQ2 共用 Linux `6.18.60-sq2-android1`。
`surface-first-boot` 在 Android 启动完成且当前为用户 0 后，设置
`device_provisioned=1` / `user_setup_complete=1`，仅停用实际安装的初始设置向导，
然后启动 HOME。成功后写入 `/data/system/surface-first-boot-v1`，后续启动不重复执行；
命令失败不写成功标记，下次启动可重试。原有 HOME 修复保留，不强制覆盖用户选择的桌面。

`PRIVACY.json` 记录 `first_setup_preserved=false`、`first_setup_skipped=true`。
仍清除继承的个人账户、Wi-Fi 和 SSH 凭据，锁定 root、默认关闭 SSH，使用全新 userdata/metadata；
不关闭 captive portal 探测、不预设网络。跳过 Android 初始设置不等于跳过独立的触摸校准向导。

## 最新触摸校准与组件复用

本轮已从当前 `MainActivity.java` 等源码编译中英双语校准 APK，并通过 APK 签名校验。
APK SHA256：`99e43d6de478880dadd568294dc18152a29a3d86273a6c49c390affdbd291bbf`。
保留原有滑动、静置、单指点击、坐标和拇指检测流程及确认/撤销行为。

`full_image_reuse.prepared_runtime` 在仅向导 Java/Manifest 变化时淘汰旧 APK，
由正常装配流程重编；图形适配库和 userdata 扩容工具继续复用。
运行时摘要不匹配，或其他 helper 源码变化，仍拒绝使用旧清单。
当前新镜像使用已校验的新 APK 缓存；内核、摄像头、传感器、Mesa、HWC/OMX 及匹配 Venus 模块未重编。

## 构建

```sh
# 已准备好依赖的标准完整构建（输出目录必须不存在）
python3 scripts/build.py --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT

# 复用明确记录且摘要匹配的已完成组件；仍完整装配及验证整盘
python3 scripts/build.py --private-root PREPARED_INPUTS \
  --reuse-components VERIFIED_REUSE.json --output FRESH_LINUX_OUTPUT --jobs 12
```

Windows 依赖准备、所需已准备 runtime/模板与剩余从零构建缺口见 [DEPENDENCIES](DEPENDENCIES.md)。
复用清单内是本机构建输入路径，移动构建目录后需重新生成并核对，不可只改摘要跳过校验。
本机本轮清单位于 `/mnt/spx-workspace/chromebookOS/out/hybrid-skip-setup-20261116/with-latest-touch/REUSE.json`。

## 新镜像

本机目录：`/mnt/spx-workspace/chromebookOS/out/hybrid-current-20261116/image/`。
Windows 可通过 `\\wsl.localhost\Ubuntu-24.04\mnt\spx-workspace\chromebookOS\out\hybrid-current-20261116\image` 访问。
这是本地成品位置，不是公开下载地址；镜像、固件及私有运行库不随本次 Git 提交发布。

| 产物 | 字节数 | SHA256 |
| --- | --- | --- |
| `disk/surface-pro-x-6.18-hybrid-generic.img` | 21474836480 | `d2fcdd9704b3d039de2654cd814a0e75d1a937de640c053cfff0a6d9aed0c37e` |
| `disk/surface-pro-x-6.18-hybrid-generic.img.gz` | 8199981130 | `1ffa84b553b5c504a3cbac42c3aa1d5f2cc48a8c9de4b96add09efa1f72e17a5` |

原始容量 20 GiB；压缩约 7.64 GiB。解压后是整盘镜像，不是单独 ESP/EFI 载荷。
`FULL-IMAGE.json`、`disk/IMAGE-MANIFEST.json` 和 `disk/LAYOUT.json` 为构建器产物；
本轮额外保留 `RELEASE.json`、`SHA256SUMS` 与本地清理/验证记录。

```sh
cd IMAGE_OUTPUT
sha256sum -c SHA256SUMS
gzip -t disk/surface-pro-x-6.18-hybrid-generic.img.gz
```

最终 initramfs SHA256：`f60315c86fb9b6305b5077c08c6857e2f40290ba7645a31bae93664eb9bc8bfa`。
与上一轮已验证的最新运行时有效文件逐项相同；相对旧休眠镜像，
只更新 `init`、新增 `surface-first-boot`、替换校准 APK，其余 1283 项内容保持。
两处 `venus-core.ko`（视频目录与匹配内核模块树）均为
`043ba0ae4c176db04c94fd32f9a06b596ee98a1a173832fb13d7c052440285f8`。
摄像头、自动亮度/旋转、触控、音频和原 hybrid 图形/视频链继续保留。

## 清理与验证

- 按用户要求删除了 6 个旧整盘/压缩成品，包括旧 `hybrid-assembly`、`hybrid-sleep` 和两个旧 release 压缩副本。
  模板、分区输入、固件、组件、运行时和历史验证记录未删除；[旧休眠镜像记录](HYBRID-SLEEP-20261116.md) 仅作历史证据。
- 完整构建退出 0；GPT 主/备、FAT/ESP 全文件回读、最终 ESP、完整 gzip 解压大小与 SHA256 均通过。
- userdata/metadata 新建且只有 lost+found；六个继承身份分区清零；成品原始/压缩哈希再次独立核对。
- 39 项针对首启、复用、HOME 和触控的测试通过。两项旧中文标签断言已更新为实际双语标签，原流程/撤销断言保留。
- 源码同输入三态：原版保留设置且没有最新校准/按需 APK 失效逻辑（exit 1），新版满足三项目标（exit 0），
  真实回滚恢复原行为（exit 1）；恢复哈希与基线相等，补丁重应用逐字一致。这些退出码是新策略验收结果，不是硬件启动结果。

成品是在 `291301b` 基础上叠加本轮功能修改后构建；发布时只追加文档，没有重新构建镜像。
未连接设备、部署或重启；新整盘尚未完成 SQ1/SQ2 真机启动、长期休眠及功耗验收。
