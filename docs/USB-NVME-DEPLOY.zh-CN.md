# USB–NVMe 转接安装

目标：将待装入 Surface Pro X 的 NVMe SSD 接入 USB–NVMe 转接盒，从另一台主机写盘后装回 Surface。

## 运行环境与入口

当前入口为 `tools/deploy-experimental-usb.py`，需在原生 Linux 或独占透传 USB 转接盒的 Linux 虚拟机中运行；普通 WSL 中的 Windows 盘符不是块设备，不能作为目标。

## 部署流程

1. 脚本列出具有序列号、未挂载、可写且逻辑扇区为 512 字节的 USB 磁盘，拒绝工作站内部 NVMe、已挂载盘、只读盘、无序列号盘和容量不足的盘。
2. 传入同一准备镜像产出的 `--manifest MANIFEST --layout LAYOUT --image IMAGE`；选择磁盘编号后显示预览，加入 `--execute` 并输入包含目标序列号的擦除确认后开始写入。
3. 写入前校验清单中全部源区段，写入镜像中的 GPT、ESP 与 Android 分区布局并逐区段读回校验。
4. 将备份 GPT 移至盘尾，扩展物理位置最后的 `userdata` 分区，并使用 `e2fsck`/`resize2fs` 扩展 ext4；分区起点、GUID、类型及文件系统 UUID 保持不变，不重新格式化 `data`。

## 限制与构建配置

- 目标磁盘容量必须不小于输入镜像的逻辑大小；较小 SSD 需先生成对应大小的基础镜像。
- 更换目标容量时，必须同步更新启动镜像中的容量/分区记录，否则启动保护将拒绝启动。
- 幽灵触控开关与默认高通/可选 Turnip 菜单需在输入镜像构建阶段处理；当前 `build-source-boot.py` 仅生成关闭两项触控补丁的版本，`build-test3-iptsd.py` 仍支持 `on`/`off`，触控 0996 适配见 `TOUCH-DEVICE-COMPAT.zh-CN.md`。
