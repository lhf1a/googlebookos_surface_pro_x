# Surface Pro X Windows 视频参考采集方案

供设备操作者执行并私下回传，2026-11-01。脚本为同分支的
[`Collect-SurfaceVideoReference.ps1`](../scripts/Collect-SurfaceVideoReference.ps1)。
可以直接转交这份说明与脚本，不需要提供设备远程登录权限。

## 要解决的问题

SQ2 的正常 EL1 Android 已完成硬件解码；EL2 Android 中同一视频固件加载成功，
但 PAS9 认证返回 `-22`，原始结果低32位为 `0xffcfffb4`，尚不清楚厂商错误含义。
Linux 终端本身已可运行。此次采集用于离线分析 Windows 的视频固件加载、
PIL/SCM 调用及 IOMMU 配置，补充固件取指流与复位交接的依据。

优先使用 **Surface Pro X SQ2**；SQ1 也可作为比较材料，必须如实选择型号。
普通 x64 Windows 电脑或 SQ3 不是替代设备。此次无需切换 Windows 的 Hyper-V、
VBS、Secure Boot 或固件版本，也不需要进入调试启动。

## 操作步骤

1. 保持设备当前正常 Windows 配置；先不要升级、回滚或重新安装驱动。
2. 在设备上打开**管理员 Windows PowerShell（64位）**，进入包含 `scripts` 的目录。
   若只收到单个脚本，把下面命令中的 `scripts\` 去掉。
3. 先执行型号检查。SQ1 设备将两处命令的 `SQ2` 改成 `SQ1`：

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Collect-SurfaceVideoReference.ps1 -ExpectedProcessor SQ2 -CheckOnly
   ```

   输出应含实际 `model`、`cpu` 和 `eligible: true`。型号不符就停止并回传这段输出，
   不修改脚本中的型号保护。`CheckOnly` 不创建采集目录，也不证明后续权限/导出成功。

4. 型号匹配后只采集一次：

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Collect-SurfaceVideoReference.ps1 -ExpectedProcessor SQ2
   ```

   `ExecutionPolicy Bypass` 仅作用于这次子进程，不修改系统持久执行策略。
   默认在桌面创建新的 `surface-video-reference-时间` 目录、同名 `.zip` 和
   `.zip.sha256.txt`。不要使用 `-AllDrivers`、`-SkipDeviceResources` 或 `-NoArchive`。
   如需指定位置，可加 `-OutputDirectory 'D:\SurfaceVideoReference-本次唯一名称'`；
   路径必须尚不存在。确认终端打印 `Archive saved to` 和 SHA256 后再回传。

5. 查看目录内 `summary.json` 的 `issues`。有失败项也保留已有材料和完整错误，
   不通过安装驱动、修改注册表、关闭安全功能或反复重跑来“补齐”。
   若未生成 ZIP，告知失败步骤；保留已创建目录供分析，不需要删除或清理系统目录。

## 采集内容与用途

| 材料 | 用途与限制 |
| --- | --- |
| `identity.json`、`versions.json` | 绑定 SQ1/SQ2、Windows 和 UEFI 版本，避免混用平台结论 |
| `platform-state.json` | HypervisorPresent、Secure Boot 与 VBS/Device Guard 状态；查询失败是 `null`，错误列入 `summary.json`，不当作关闭 |
| `drivers/`、`driver-store/` | 导出显示及 PIL、SCM、SMMU、IOMMU、PEP、RPMh 相关完整驱动包；含 INF、SYS、DLL、CAT、包内 MBN 等原始文件，供追踪固件调用与配置 |
| `active-drivers.json`、`driver-packages.json`、`exported-packages.json` | 区分当前活动版本、残留版本、导出方法与失败；不能仅凭目录里存在就认定正在使用 |
| `device-resources.json` | 相关 PnP 设备的服务、硬件ID、MMIO和中断资源 |
| `acpi/`、`acpi-tables.json`、`acpi-enumeration.json` | DSDT、SSDT、IORT 等白名单表及校验和；Windows API 对同签名多表有局限，注册表补取也不能保证完整 |
| `manifest.json`、ZIP SHA256 | 逐文件长度/哈希与传输完整性；清单不包含自身，ZIP哈希单独提供 |

脚本只查询系统信息、导出既有驱动并写入新目录。不安装采集驱动，不访问任意物理
内存/MMIO，不执行 PAS/SCM 调用，不改账户、网络、启动配置或安全设置，不重启。
不采集密码、Wi-Fi配置、许可证表 MSDM/SLIC、原始 SMBIOS 或用户文档。
**原始包仍可能包含设备实例标识、驱动软件清单和本机路径，须私下传输，不入公开仓库。**

## 回传清单

请回传 `.zip`、对应 `.zip.sha256.txt`，并附以下文字；截图不是必需项：

```text
CPU型号：SQ1 / SQ2
采集时间及所在时区：
Windows当前桌面/触控/视频播放是否正常：
最近是否更新过显卡驱动或UEFI；若已知请说明：
是否有视频播放异常；若有，给出应用名、版本和症状：
采集脚本是否打印 Archive saved to：
summary.json 中的 issues（没有则写无）：
```

不要发送账户密码、BitLocker恢复密钥或远程登录凭据。不要只发送截图、INF
或固件文件；离线分析需要完整包中的对应实现和清单。

## 如方便，补一项播放观察

使用现有播放器播放一段**非私人、无DRM的本地 H.264 视频**约30秒，记录播放器、
文件编码/分辨率、是否流畅，以及任务管理器 GPU 的 `Video Decode` 引擎是否有活动。
若方便截图，只截该性能图，避免账户、桌面和私人文件名。没有该引擎图或无法识别
编码就写“不确定”，不要为此安装工具或修改配置。此项是辅助观察，不是逐帧解码
正确性证明，也不能据它推导固件SID或EL2兼容性。

## 收到材料后的处理与停止条件

分析方先核验ZIP及manifest，核对型号、活动INF、导出失败项与ACPI校验和。
优先检查 qcdx8180、qcpil、qcscm、qciommu、qcsmmu、qcpep 相关包；某包不存在
可能与版本/打包方式有关，应记录缺项，不要求操作者另找不明来源驱动。

与已有Windows参考按文件哈希比较；相同文件不重复反汇编或再次采集。重点查找
固件启动/关闭调用、SID/IOVA配置、保护及复位交接，分别记录直接证据和推断。
现有IORT的 `2300/2320/2340` 是视频数据流证据，**不能把缺失的 `2302` 自动认定为固件流**。

本轮不要求ETW/WPR、内存转储或内核调试。只有在静态材料中确认具体可用provider、
事件/关键字和所需动作后，才另给短时动态采集命令、容量限制和停止清理步骤。
若新包仍没有固件流/复位依据，应明确报告缺失；Windows驱动正常不证明EL2下权限相同。
本采集可能提供新证据，**不保证能够直接读出安全固件内部映射或解决EL2硬解**。
