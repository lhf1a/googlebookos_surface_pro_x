# Surface Pro X Googlebook OS

唯一编译基线为 **Linux 6.18 LTS / 6.18.60-sq2-android1**。

[统一构建入口](docs/BUILD.md) · [最新进度](docs/HANDOFF.md) · [迁移验证](docs/KERNEL-6.18-MIGRATION.md)

## 保持当前验收内容

以 `configs/kernel-baseline.json` 的文件清单为准，不重新混装旧模块。当前原件：
- Image：`db374853bb5df0cdaa5215b1226256adedda3292a93084e874438a4ba706c982`
- initramfs：`5b3a70c349735e58b404b0d3825dc0dde12ae991706cbbab7e15ed65c9911185`
- DTB：`c137b9604b908e39aba4be6ecd98e55cd9986fe3fd7522c93069bef7489c956b`

有效启动参数为 **Mesa、原生60Hz、EL2、unrestricted suspend、nohibernate**；GPU为当前700/RPMh432档。
保留音频、SSAM电池、SPI触摸response、Venus队列锁、IPA预留与RMNET、Android拥有网络及启动后SSH等当前修正。
不因为旧文档默认高通/661而改变当前验收内容。单机用户确认与六帧视频证明不等于跨设备或长期认证。
Scene跨用户链接属于已授权设备数据状态，不复制到其他设备；Scene GPU读数缺失仍是已知问题。

## 一个入口，明确区分核验、组包与编译

在具备Python3的主机运行；源码编译需要Linux/WSL与匹配ARM64工具链：

```sh
python3 scripts/build.py verify
python3 scripts/build.py package --output out/6.18-release-FRESH
python3 scripts/build.py compile --help
```

默认输入为 `private/build-inputs/kernel-6.18/EFI/SQ2L618C`。`package`逐字节复制当前EFI包并核对清单，
**不是源码编译，也不是生成新的完整磁盘镜像**。旧磁盘重组入口已撤销，避免悄悄装回6.3.8。
`build-surface-pro-x.py`、`build-source-boot.py`、`build-source-disk.py`、`build-sq2-default.py`兼容文件名均转到同一CLI，旧参数明确拒绝。

`compile`使用显式source/config/build-dir/output/cross-compile；实际执行Image、modules、dtbs与模块导出。
默认配置为 `configs/kernel-6.18-baseline.config`。完整基线还需要匹配的外挂源目录与模块清单，具体参数以`compile --help`为准。
新编译输出与验收发布包分离，未经配套打包/验收不得覆盖发布pins。不能以仅核心编译通过代替全部外挂模块与initramfs功能对齐。

## 本地输入与恢复

- `private/build-inputs/kernel-6.18/`：当前私有EFI包、源码归档与构建清单；不纳入Git。
- `out/mainline-618-20261107/`：改动前Git bundle、未提交diff、未跟踪文件归档和本轮输出。
- `out/kernel-baseline-20261107/retired-638/`：退休工具逐字节备份。
- `configs/retired-kernel-tools.json`：退休清单与原hash。仅退休旧工具，不删除真机回退入口。
- 既有四角色与验证记录继续保存在 `out/kernel-6.18-migration-20261105/`；新记录只追加。

## 隐私与验收边界

当前私有包保留原SSH身份，不能当成匿名公共镜像发布。Git仅保存源码、配置、清单、测试与文档。
不把IP、序列号、用户编号或原盘UUID作为编译/运行准入。SQ1/SQ2通用交付目标保持，但本轮基线是当前SQ2已观察产物，不能假称6.18双机新镜像全验收。
本轮主线整理不写设备、不重启，不清除userdata或已有恢复能力。