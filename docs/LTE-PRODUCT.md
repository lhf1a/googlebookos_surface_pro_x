# SQ1/SQ2 LTE：当前产品路径

更新：2026-11-04。此页取代旧 LTE 文档中的 Radio04 donor、单机序列准入、手工移植 FSG、Java NetworkAgent 与实验观测器的构建说明；旧文档作为历史记录保留。

## 保留的运行链路

1. `scripts/build.py` 默认启用 LTE，共用源码/运行包；原非 LTE Mesa 回退不变。
2. 默认 EFI router 在进入 EL2 前读取本机 SMBIOS UUID。有效 `EFI/SPX/LTE/<uuid>.bin` 缓存直接复用；缺失或损坏则通过原生 SCM GET_INFO、经 CRC 检查的主 GPT 定位并完整读取本机 FSG。只在启动介质保存私有缓存，不写物理基带 NV。失败记录真实错误并继续 Android，不借用另一台机器的校准。当前格式容量上限 2 MiB，超限返回错误，不截断冒充有效数据；同步固件调用可能阻塞，调用数有界不等于超时可抢占。
3. `lte-unit-seed` 检查本机缓存来源/长度/CRC，原子更新 RAM 中 `/run/rmtfs/modem_fsg`；不内嵌单机 NV。保持原 MPSS、FS1/FS2 与启动顺序。
4. 原 IPA4.1/DPM/WDA/WDS/RMNET 提供 bearer；Radio AIDL 是唯一控制者，Android 管理 SIM、网络、APN和移动数据。AOSP APN 表及许可证/固定来源在 `probe/lte/overlay/`，保留用户已有 APN 编辑。当前 bearer 是 IPv4，表中 IPV4V6 不等于已支持双栈。

UUID 是动态缓存来源标识，不是单机准入白名单。缓存包含私有校准，不能随镜像分发；CRC 仅检测损坏，不是认证/加密。缺少有效 SMBIOS UUID 或固件读取 ABI 不兼容的设备尚无替代采集路径。私有固件、模块和 Binder 链接库仍需操作者提供，缺少必要构建输入必须明确失败。

## 已确认范围

- SQ2：此前已验证原生 Radio、SIM/信号、LTE IPv4 数据、移动数据/飞行模式和 Wi-Fi 回退；本轮复用，不重复整套验收。
- SQ1：空缓存启动自动读出本机完整 FSG，SIM LOADED、LTE 注册/PS attached、Android data connected；删除实验 APN 后自动选中 AOSP 广电 APN，强制蜂窝 HTTPS 返回 200。普通默认重启复用同一缓存、LTE 数据连接恢复，正常 EL2、screen_only/nohibernate 和音频就绪保持。
- 自动采集版本已构建过共用初始镜像；**完整新镜像尚未完成双机全新启动验收，SQ2 新自动采集入口尚未实测**。局部已验收载荷不替代整镜像验收，未声称覆盖任意样机/运营商。
- 不支持语音、SMS、IMS、eSIM、5G；IPv6、漫游、长期稳定性及所有运营商未验收。偶发注册拒绝仍需保留为已知限制。

## 本次清理版的结果

生产 EFI、Radio HAL、QMI data/mode、RMNET 与缓存解析器编译完成。SQ1 已安装清理版，原默认载荷/清单/router 留有本地备份，Mesa、共享菜单/固件保持。用户确认重启后正常启动；远程 SSH 重连仍在握手前关闭/重置，**本次清理版的 LTE 联网复测尚未完成**，不把用户的启动确认扩大为 LTE 验收。

本轮修改前，旧运行版仍显示 SIM LOADED/LTE/data connected，但绑定蜂窝的 HTTPS 已超时；保留此前成功的历史结果，同时不声称持续联网稳定。本轮未执行真机回滚启动；源码副本回滚已恢复原字节，不能替代硬件回滚结果。未重跑离线功能测试/QEMU。

## 清理边界

删除旧 LTE/IPA/PDC/MFS/EFI 单次探针及专用打包脚本、旧 Java bearer；移除生产二进制中的 selftest/parcel-selftest 和调试转储，协议测试留在 tests。标准组包删除基线遗留 LTE observer，关闭 RMTFS verbose，保留实际错误、QMI 边界、会话退出和 HAL 必需 IPC/状态通知。`ipa_sq2_lab.ko` 是当前实际驱动名，不因含 lab 字样删除；也不误删其他功能所依赖的共用源码。

必要的构建/产物读回不是新增离线功能测试门禁。历史工具可从 Git 历史取回；原始日志、NV、带登录配置的镜像和回退证据仅本地保存，不提交。该清理不是 root/permissive 系统的生产安全认证。
