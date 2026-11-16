# 2026-11-16：完整 hybrid 镜像装配

当前主线工作区已从组件阶段推进到实际整盘产物。没有连接设备、部署、重启，也未恢复 KGSL/SDE 开发。

## 已生成

- Linux `6.18.60-sq2-android1`，唯一构建线 `hybrid`。
- SQ1/SQ2 共用 Image/initramfs，保留不同型号 GPU/GMU OPP 配置与现有 SMBIOS 路由；普通及 rescue 入口均有 SLPI DT/EFI。
- 原始镜像 21,474,836,480 字节，SHA256 `ff26583159c8d428bcbadafbe7904533896e7a8b15a0a7070aedddc32818776b`。
- gzip 镜像 8,195,223,903 字节，SHA256 `c1eced47601a4d511fd463d8a6a1a51368ba0b99c9d6f63d32af56a7a45dc04d`。
- 最终 initramfs 1283 项，SHA256 `fef460cd2894a384c85a90d38856fee7afc258b54e97c7df38730a6a0f10572d`。
- 本地产物目录：`/mnt/spx-workspace/chromebookOS/out/hybrid-assembly-20261116/image/disk/`。

## 复用及接入

显式 SHA256 清单复用已经编译通过的内核及 493 个 staged 模块、摄像头完整 bundle、传感器模块/HAL/overlay、Mesa 六库、Vulkan router 和 gralloc adapter；本次整盘构建未重新编译这些组件。

HWC composer、OMX service、libOmxCore/libOmxVdec 与原已准备运行库逐字一致。OMX 保持 native ANW surface 输出、`RequiresSwRenderer=0`；HWC 阶段、摄像头与传感器启动 hook 在实际生成 initramfs 中各出现一次。显示 libdrm 与新 Mesa 一致，SDM 背光 shim 已接入。

静态 ARM64 hexagonrpcd 与 PAS12 EFI 已构建并装配；EFI SHA256 与既有验收版完全相同（`2b3a941b…`）。新 RPC SHA256 `f7761415…`，不是旧 RPC 二进制逐字复用，不据此宣称设备 RPC 已运行。

Windows 提取入口默认要求完整传感器 OEM 资产：SLPI 固件、23 项型号配置与 SurfaceLightSensor.sys，支持原 SQ1/SQ2 Windows 或原厂恢复 Windows 目录。Linux 导入提取型号默认光学校准，不采集每台设备 registry/persist。
本轮用固定 OEM 归档构造 DriverStore 测试输入，实际执行 Windows 提取→tar 打包→Linux 校验导入→sensor bundle 装配，与直接输入版本逐文件相等；**没有从一台连接中的 Surface 读取文件**。按用户要求，交付脚本默认走原 Windows 提取，不自动用该归档替代。

## 已观察验证与边界

GPT 主/备表、FAT 检查、ESP 全部文件回读、最终镜像 ESP 与 FAT 哈希、完整 gzip 解压流与原始镜像大小/哈希一致；新建 userdata/metadata 根目录仅有 lost+found。misc/persist/frp_persist/rawdump/desktop_security 两分区逐字检查为零。
运行时移除继承 Wi-Fi/SSH/账户输入和跳过首次设置的 helper，锁定 root、禁用默认 SSH，保留首次设置。

这是完成 host 装配与检查的通用候选镜像，**不是新镜像 SQ1/SQ2 真机验收**。HWC/OMX、显示、自动亮度/旋转、触控与摄像头的本轮声明是“已装入且启动链已接入”，不是“本轮实机运行通过”。
仍使用已准备的 Android/QTI runtime、EFI/固件及模板；只给公开下载与 Windows 导出即可从零重建全部运行库的 clean-room 流程尚未闭合，`--stage all` 不会报告整镜像完成。

续接证据：ignored `out/hybrid-entry-20261116/CURRENT.json` 和同目录四角色；新增证据扩展在 `assembly/`，旧记录保留。
