# USB–NVMe 转接安装

目标：将准备装进 Surface Pro X 的 NVMe SSD 接入 USB–NVMe 转接盒，
从另一台电脑写盘，完成后安全移除并装回 Surface。
这不是把系统安装到普通 U 盘，也不是直接写工作站内部 NVMe。

当前入口：`scripts/deploy-experimental-usb.py`，在原生 Linux 或独占透传
USB 转接盒的 Linux 虚拟机运行。普通 WSL 中的 Windows 盘符不是块设备，
不能当作该脚本的目标。

脚本列出有序列号、未挂载、可写、512 字节逻辑扇区的 USB 磁盘。
选择编号后先显示预览；加 `--execute` 并输入包含目标序列号的擦除确认后
才执行全新安装。所选 SSD 的原有数据会丢失，需事先另行备份。
拒绝工作站内部 NVMe、已挂载盘、只读盘、无序列号盘和容量不足的盘。

输入参数：`--manifest MANIFEST --layout LAYOUT --image IMAGE`，三者必须来自
同一个准备好的镜像。镜像包含 GPT、ESP 与 Android 分区；脚本不是使用
Windows 通用分区向导重新生成 Android 布局，而是写入镜像中的布局。
写入前验证清单中全部源区段，写后逐区段读回，再移动 GPT 备份表到盘尾，
扩展物理位置最后的 userdata 分区，并用 e2fsck/resize2fs 扩展 ext4。
分区起点、GUID、类型及文件系统 UUID 保持，不重新格式化 data。
此处扩容只用于新安装，不用于修复现有系统的挂载故障。

限制：目标容量必须不小于输入镜像的逻辑大小；当前不支持将 512GB
镜像直接缩写到 128/256GB SSD。较小 SSD 必须先生成相应大小的基础镜像。
SQ1/SQ2 目前共用输入镜像的实验配置，部署器不改变 CPU/GPU 参数。
幽灵触控开关与默认高通/可选 Turnip 菜单必须在输入镜像构建阶段处理，
本部署器不负责把旧镜像自动升级到最新状态。

验证边界：上述 Linux 部署入口仅做离线设备筛选、容量和确认测试，未实写。
本次实际在 Windows 使用 Install-SourceBootSSD.ps1：按确认的 UniqueId
重新识别磁盘编号，写入 build-source-disk.py 生成的 128035676160 字节镜像，
全部 19366498304 字节初始化区段 SHA256 读回匹配，data 为111013072896字节。
装回后设备已启动并经 SSH 确认；触控 0996 临时适配及用户确认见
TOUCH-DEVICE-COMPAT.md。不是 SQ1/SQ2 全功能认证，不提供擦盘后的数据回滚。

源码重组入口 build-source-boot.py 使用当前启动助手及已构建组件，
原厂固件、闭源高通库与私有底包仍是固定输入。当前入口仅生成关闭两项
触控补丁的版本；build-test3-iptsd.py 的统一开关仍支持 on/off。
source-disk 入口为本次128GB配置，不是任意容量的通用安装器；更换容量
必须同步启动镜像中的容量/分区记录，否则启动保护拒绝。
