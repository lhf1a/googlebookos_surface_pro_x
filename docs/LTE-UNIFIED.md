# SQ1/SQ2 通用 LTE：新机 RF 初始化追踪

## 范围与当前缺口

目标仍为同一初始镜像在新配置 SQ1 和 SQ2 上自动完成 SIM、网络注册、PS attach、
蜂窝承载及 Android CELLULAR。历史 SQ2 已完成 LTE/Chrome 实机验收；不能以此
代替新 SQ1。新 SQ1 可读 SIM，但正常默认内核下 NV453=1、rfnv 目录完整枚举0项、
DMS接口0、mode2、NAS未注册/PS detached、signal error74。

## 本轮已取得的新证据

原 SQ1 Windows 的 `QcSOCPartition.sys` SHA256 `9c9087f6…` 与
`qcremotefs8180.sys` `1ad254e9…` 已重新取得并核对。后端静态链补全为：

```
RemoteFS 0x13870 / READ IOCTL ecaf2b46
  -> QcSOCPartition 0x3dfb8 -> 0x42bb0
  -> block branch 0x42e90 -> 0x42010
  -> read operation1 at 0x422c4 -> 0x3cd08
  -> service selector2 at 0x3d190 -> 0x3c840
  -> TrEE IOCTL 0x568004 / service AE865C08-4A07-404D-BE51-D9A0465E23E5
```

SCM调用号 `0x02001a01`、arginfo `0x214`、40字节命令与32字节details、
storage_type1及read1与[公开SCM storage补丁](https://lists.openwall.net/linux-kernel/2026/01/26/752)
相符。该公开线索已在旧记录中；本轮新增的是原Windows机器码与实机读取对应，
不是宣称首次发现该协议，也不是宣称Linux EL2已可直接使用它。
`inspect-surface-fsg-backend.py` 固定两原件SHA，核验26处机器码/常量及两个服务GUID。
地址均为RVA；不是REA伪xref或只按字符串推断。

通过原驱动固定FSG GUID的READ IOCTL，先4096字节头部、后完整2097152字节均成功。
新原件SHA256 `31c3e5af3de7240c6b40bbac05a0bdfa600bd2034ab34a20ce1fab64f5d81b6d`，
与当前共享包 `bd124b2d…` 不同；IMGEFSG后缀为0d而非0f，两者外层几何一致。
它来自另一台Windows参考SQ1，**不是已证明属于待测SQ1的校准**。
未绕过文件锁、停驱动、调用任意SCM、写NV、清FTM或提取身份/密钥。
高熵/格式/哈希差异不证明设备绑定、解密失败或RF内容有效。

## 可复用工具

- `scripts/Read-SurfaceFsgReference.ps1 -SelfTest`：Windows PowerShell 5.1固定请求布局自测。
- 在匹配原驱动的参考Windows设备上，显式 `-OutputPath PRIVATE_PATH` 才读取；
  固定FSG GUID、零offset、2MiB和单一READ IOCTL，不提供任意读写命令参数。
  私有输出拒绝覆盖；不要将输出放在公开仓库。此工具不是默认镜像组件。
- `scripts/inspect-surface-fsg-backend.py --soc-driver DRIVER --remotefs-driver DRIVER`：
  仅离线输出机器码校验JSON，不连接设备。
- `probe/lte/surface-lte-rf-observe.c`：有界20秒手动DIAG观察，NV白名单453，
  固定rfnv目录只计数；不写NV、不改日志mask、不reset、不自动进入普通启动。
- Radio轮询修复拆分 firstModem/firstNetwork/firstSignal 并隔离查询异常，
  解决重复UNAVAILABLE/注册通知被信号错误阻断；不是基带RF恢复。

## 本轮启动试验设计

已按用户要求归档并清理旧SQ1PmTrial ESP目录；原默认/回退载荷未改变。
先重启原默认内核，移除PM trial标志后仍复现相同RF故障，排除本次PM内核切换干扰。
仅FSG及校验文件改变的候选在本地生成；全部其他有效cpio条目逐字节一致，
凭据成员保持最后。普通Image/DT/固件/菜单两项保持；使用一次性GRUB env，
先消费并回读once=0才进入候选，下一次启动自动回原默认。

离线匹配解析的BASELINE/MODIFIED/ROLLBACK是FSG `bd124…/31c3…/bd124…`，
退出均0；回滚镜像hash与原件一致，补丁重建候选逐字节一致。
这不是硬件RF通过；实机结果以本节后续记录为准。

## 不重复的已完成路线

不再复试相同FS1/FS2配对、无变化online、QMI21 EPERM或错误DIAG握手。
不把NV1878 inactive、空/rf或PDC error16单独当失败根因。
私有二进制、FSG、原始日志、带登录配置的启动包、完整A/B/R与工具错误记录
留在原工作区 `out/lte-main-review-20261103/efs-restore-v1/`，不提交Git。
## 一次性候选实机结果

正常原默认启动 `e98867d8…` 与候选启动 `6dfff0a2…` 都正常进入Android；
新FSG精确哈希同时存在于`/etc/sq2-lte`与`/run/rmtfs`，RMTFS读取0:4096。
两者均NV453=1、rfnv0、DMS接口0/mode2、NAS0/PS2/signal74，观察器exit0。
没有出现LTE注册，**第二份FSG迁移未改善，不提升默认、不用于通用发布镜像**。
一次性env确实消费0，默认菜单恢复原哈希。ROLLBACK boot `4451de32…` 已回原默认；同一脚本仍得到上述RF状态，exit0。
原FSG恢复bd124…，默认/回退镜像与菜单hash一致，试验目录已清理，常亮锁重新持有。
不将参考SQ1数据冒充待测机自有数据；不把失败自动解释为设备密钥绑定。

本轮5项Python测试、PowerShell布局/缺路径/拒绝覆盖测试及原件26处机器码校验通过。
未将诊断程序接入常规启动，未把失败FSG放入构建输入。
