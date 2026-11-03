# D 盘工作区

VHDX: `D:\WSL\chromebookOS-workspace.vhdx`，512GiB 动态上限，ext4。
项目：`/mnt/spx-workspace/chromebookOS`。
Windows：`\\wsl.localhost\Ubuntu-24.04\mnt\spx-workspace\chromebookOS`。

WSL 启动后 systemd 服务 `spx-workspace-volume.service` 自动连接本地 QEMU/NBD 并挂载；Windows 不需要提权挂盘。不要同时从 Windows 或另一进程挂载/打开该 VHDX，不要在挂载时移动/压缩 VHDX。

构建：`./build-on-d.sh --output /mnt/spx-workspace/releases/NEW_NAME`。该入口将 SDK、NDK、AIDL 和 GNU-EFI 指向 D 盘工具目录，不再读取旧内核构建缓存。系统工具如 Python/GCC/JDK 仍由原 Ubuntu 提供。

已迁移源码/Git、集中私有输入、新版 IMG.GZ，以及必需的公开工具链。未复制旧构建备份、测试/实验数据。旧 C 项目与 WSL 缓存未删除，因此尚未释放它们占用的 C 盘空间；WSL VHDX 内删除文件也不一定立即缩小 Windows 上的 ext4.vhdx。

维护：`sudo systemctl stop spx-workspace-volume` 安全卸载；`sudo systemctl start spx-workspace-volume` 挂载。若卷未挂载，不要向空挂载目录写入。迁移验证记录：`D:\WSL\migration-result.json`。
