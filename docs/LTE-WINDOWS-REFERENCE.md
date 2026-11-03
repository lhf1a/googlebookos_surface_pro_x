# LTE：SQ1 Windows 原版参考与 GooglebookOS 对齐（2026-11-02）

## 2026-11-02：SC7180 寄存器/端点进一步核对（静态交叉验证，未启动 DMA）

原版 qcipa8180.sys `8ba3feed…` 和 qcgsi8180.sys `412f2b0e…` 经 REA 字符串定位、
独立 ARM64 ADRP/ADD、PE unwind/leaf 边界及原始指令核验；37 条指令守卫、4 项测试通过。
REA 的统一 0x1000 xref 和无法识别函数结果不采作调用证明。

- PipeCfg 解码已证：每3字节展开为12字节记录，写到字段偏移0/4/8，原始四组为
  `(3,0,0), (4,0,1), (13,1,3), (14,1,4)`。第二字段保留 raw direction，第三字节在 Q6
  上下文被使用；不能仅凭数字指派 AP 角色或最终 GSI 语义。其 Q6 pipe ID 集合
  `{3,4,13,14}` 与 SC7180 MODEM 端点 `{4,5,10,11}` 不同，不可照搬端点表。
- CLKON_CFG 0x44、QSB_MAX_WRITES 0x74、QSB_MAX_READS 0x78、IPA_BCR 0x1d0、
  LOCAL_PKT_PROC_CNTXT 0x1e8、IPA_TX_CFG 0x1fc、ENDP_INIT_SEQ 0x83c 共7项偏移可对齐，
  endpoint register stride 都是0x70；这不证明全部位域、复位或初始化序列相同。
- GSI channel context 的 Windows 公式为 `base1e00000+(EE+8)*0x4000+channel*0x80`；
  Linux v4.0 表/SC7180 DT 为 `base1e04000+0x1c000+EE*0x4000+channel*0x80`。
  同 EE/channel 时物理地址相同，16个算例通过；实际 EE 身份/分配尚未新测。
- 原版还使用 `0x800+endpoint*0x70` 控制；6.3.8 v4.2 profile 无 ENDP_INIT_CTRL 条目，
  endpoint 源码明确禁止 >=4.0 suspend / >=4.2 delay，不能将两个配置整体等同。
- Windows OCI `0x146bd000` 与 SC7180 IMEM `0x146a8000` 不同；语义完全相同尚未证明。
- 原版版本读取位置已追到 IPA 映射基址+0x30（32位读），分割 bits31:28/27:16/15:0。
  本轮没有读取 SQ2 MMIO，实际值仍为 `IPA_HW_VERSION` 未确认槽，不宣称4.0/4.1/4.2。

已取得并哈希现机 ipa.ko `beb2cf09…`；与本地6.3.8构建模块的 .rodata/字符串节相同，
.text 不同。可支持配置常量对照，不能声称完整代码相同。原端点、位域、版本必须分别验证。
现机 boot2e535372…/原DT85b/原包957/菜单9f保持，IPA未绑定、无rmnet；本轮仅只读检查，
没有改DT、启用时钟、访问MMIO、启动DMA、修改NV或重启。此前硬件对照仍是原compatible
未绑定 → SC7180匹配但缺core clock(-2) → 原compatible恢复，三阶段DMS均2且未注册/断开。

完整逐项报告、来源hash、指令、工具错误与更正保存在忽略的
`out/lte-reference-20261102-75886ecf/ipa-register-endpoints-20261102-v1/`。
实际执行入口 `python3 …/test-config.py` 与 `python3 …/verify-config.py`，不访问硬件。
以下初始“PipeCfg尚未追到指令/未采集NV”等段落保留为早期阶段记录，不代表当前完成状态。

用户恢复 LTE 分支上的 Windows 参考采集/离线分析；本轮不做射频、固件、
启动盘或 SQ2 运行状态变更。当前分支为 `codex/lte-support`。
SQ2 的上一轮 error52、factory-test 与 dummy EFS 是历史实测，不是本轮新查询。
**LTE 尚未实现，不能把本轮静态对齐或 Windows 设备 OK 当作 Android 上网通过。**

## 采集与来源

- 通过既有操作者公钥、固定主机别名及严格 known_hosts 核验连接参考设备。
  当前 CIM 确认为 Surface Pro X / Microsoft SQ1 / ARM64 / Windows 22631。
  未改账户、驱动、服务、网络、安全策略或启动设置。
- 首批 11 个 OEM 包以 `pnputil /export-driver` 导出，另只读复制两个 inbox
  DriverStore 目录；共 13 包、72 个 manifest 文件、8 张校验和有效 ACPI。
  首批跳过全量设备资源关联查询，只保留设备清单。
- 根据真实依赖补采 8 包：IPC Router、GLINK、RPEN、TFTP、PDSR、Surface PEP、
  Surface subsystem 与 inbox NetVChannel；补充 manifest 43 文件。
  两批合计 **21 包、115 个 manifest 文件**，逐文件尺寸/SHA256 全部通过。
  数量不包括 manifest 自身；不是 21 个驱动均已验证可移植。
- 两批归档 SHA256 分别为
  `388ca94c1cbcdf65170f0c5be9a69f1aa6bd5429b93bf32cac7a212c0a6735ba`、
  `6220462215ff5c1c1c23e23415d6a5dd35ad2912fc803ff8e16c1531a217c7ce`。
  原始二进制、设备/注册表元数据和日志只在忽略的
  `private/sq1-reference/lte-collection-20261102-75886ecf/`；不提交或上传云分析。

## 已建立的对齐关系

| 层次 | Windows 原版观察 | GooglebookOS/SQ2 边界与下一缺口 |
| --- | --- | --- |
| MPSS | `surfaceprox_subextmpss` 包的完整 MPSS 为 75470832 字节，SHA256 `9ec7b6c6…`；NM 变体为 5217520 字节，`21528167…` | 两者均与已保存 SQ2 原版及 unpatched firmware 副本逐字节一致；只证明这两个固件相同，不证明 EFS/校准、IPA 或所有驱动兼容。没有替换固件。 |
| Windows MBB | `surfaceprox_mbb.inf` 匹配 QCMS DEV_0489，实际设备 service=mbb/status=OK；加载 `qcmbb8180.sys`，依赖 NetVChannel。二进制含 QBI/QMI、MBIM 相关字符串 | 这是 Windows NDIS/类扩展实现，不是可复制到 Android 的 Radio HAL；仍需原生控制/数据通路与 Android 服务。未读取 SIM 标识、APN 或订阅配置。 |
| IPA | QCOM0470 / qcipa；DSDT 与限定 WMI 查询都给出地址 `0x01e40000`、长度 `0x1ffff`、ACPI IRQ 343 | 已记录本机参考资源，不能直接把 ACPI IRQ 当 Linux GIC SPI 索引，亦不能冒用其他 SoC compatible。Linux 运行 DT 缺硬件节点仍是前轮记录，本轮未新测 SQ2。 |
| GSI | QCOM0483 / qcgsi；DSDT 与限定 WMI 均为地址 `0x01e00000`、长度 `0x30000`、ACPI IRQ 464 | 对齐 endpoint/事件环/中断/版本后才能构建独立 Linux 候选；现有 ipa.ko 的存在不等于支持该板。 |
| 实际驱动参数 | 只读核对 qcipa 的 `OciMemAddr=0x146bd000`、`OciMemSize=0x2000`、`NumPipes=4` 和 12 字节 `PipeCfg`，与 INF 一致；qcgsi 的 `NumOfBuffers=256`、`A2PCEnabled=1` 一致 | `PipeCfg` 原始字节为 `03 00 00 04 00 01 0d 01 03 0e 01 04`；其字段编码尚未追到指令，不把字节位置猜成 Linux 通道配置。 |
| RMTFS / EFS | qcremotefs 匹配 QCOM0417；REA 确认 ARM64 PE、SHA256 `1ad254e9…`，可见 `/boot/modem_fs1/fs2/fsc` 和 `/boot/modem_fsg` 路径引用 | Linux 已知 `/run/rmtfs` dummy 与真实 EFS 不同。普通 INF 导出没有运行时数据；不能据导出包没这些文件就判定 Windows 也没有。真实后端尚未定位。 |
| Windows 运行时 EFS 文件 | 在已确认 qcremotefs DriverStore **原目录**发现 `bootmodem_fs1`、`bootmodem_fs2`，各 2097152 字节 | 首次哈希请求被共享锁拒绝，保留原始错误；随后只读目录元数据。没有绕过锁、停驱动、导出内容或把 SQ1 的单机 NV/校准移植给 SQ2。文件存在/长度不证明内容、存储来源或可用性。 |
| SoC partition | QCOM04AA / QcSOCPartition；REA 确认 ARM64 PE、SHA256 `9c9087f6…`，含原始磁盘/分区路径模板 | Windows 标准 Storage API 仅枚举一个 NVMe 及 ESP/MSR/Basic/Recovery 四区；不排除其他固件管理存储，更不能据字符串就断言 EFI BlockIo/QSPI/SCM 路线可用。 |
| GooglebookOS vendor | 本地 mica 已解包 vendor 的 `ro.board.platform=hamoa`；有 libqmi/librmnetctl/libril 等库和 noRil XML；`qspa-modem.xml` 为无实例的 override 条目 | 已逐项检查 `bin/hw/rild`、`bin/netmgrd`、`bin/ipacm`、`lib64/libril-qc-hal-qmi.so` 均不存在。init 中 disabled 的 daemon2/3 文本不提供缺失二进制。仅删 noRil 或补声明不是实现；这也不是对 SQ2 当前有效 VINTF/进程的新实机查询。 |

### 静态工具的限制

REA 只分析 `out/lte-reference-20261102-75886ecf/samples/` 中的副本，报告在同一
忽略目录；未运行目标驱动。保留 session、字符串及一次 RemoteFS 反汇编结果。
REA 的该次 `find_xrefs` 返回统一 `0x1000 -> call`，未用独立指令验证，**不采为
真实调用边或文件读取证明**。没有从伪 C 推断可部署的 SCM/存储 API。

`iasl` 成功反汇编 DSDT。RFS0 `_CRS` 从 `RMTB/RMTX/RFMB/RFMS/RFAB/RFAS`
动态填写资源；其中 `0x88888888` 等是模板占位，不能当实际物理地址。
历史 EFS 线索见 [linux-surface issue #2](https://github.com/linux-surface/surface-pro-x/issues/2)；
其历史推测不是本轮已验证的后端或根因。

## 可复用源码与验证

`scripts/Collect-SurfaceLteReference.ps1` 从既有 video collector **复制**，原脚本
SHA256 仍为 `03687a8e…`。更改限于 LTE 包前缀/选择表达式、设备筛选、固件摘要
和标签；保留平台身份拒绝、管理员要求、输出拒绝覆盖、只读 ACPI 白名单、
reparse 防护及 manifest。补齐上述依赖前缀，不默认导出所有驱动或 Wi-Fi 包。
未来采集默认包含资源关联；批量远程场景可显式 `-SkipDeviceResources`，再按已知
实例做有界补查。`-AllDrivers` 仅显式启用，本次未使用。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/Test-SurfaceLteReference.ps1
```

25 个默认选择案例及显式 AllDrivers/AST 通过。对同一 fixture：
BASELINE 不选 qcipa/RemoteFS/MBB、选择 qcdx；MODIFIED 正好相反；独立副本
ROLLBACK 恢复原选择和原 SHA256，三次观察 exit0。真实补丁重建与 MODIFIED
逐字节一致。执行策略参数仅影响测试子进程，没有修改持久策略。
原只读 QMI 和 online 恢复模拟测试均 exit0；未运行新设备 online 请求。
首轮测试的执行策略错误、一次错路径/Windows glob 错误和哈希共享锁错误保留在证据记录，
不能把失败执行记作通过。

四角色仍为忽略目录 `out/lte-reference-20261102-75886ecf/` 中的
`MODIFIED_FILE.ps1`、`DIFF_FILE.patch`、`VERIFICATION.txt`、`ROLLBACK.sh`，
另附 pristine `BASELINE.ps1`；ROLLBACK 接受一个目标副本，不触及设备。

## 下一可执行动作与停止边界

1. 从已取回的 RemoteFS / QcSOCPartition / GLINK 副本离线追踪分区 GUID、
   请求与实际后端；把字符串地址和真实指令引用分开。**不再次哈希锁住的文件，
   不为取 EFS 停 Windows 驱动，也不照搬 SQ1 校准。**
2. 用已确认的 IPA/GSI 资源及参数核对固定 Linux 源码的寄存器版本、端点、
   GSI 通道和 IOMMU/时钟需求，再做不部署的候选。完成原生数据通路后才接
   Android Radio HAL/数据服务；逐项验收注册、拨号、地址/DNS 和蜂窝流量。

本轮没有连接/重启 SQ2、改射频模式、APN、DT、固件、内核、启动包或菜单。
正常双入口、screen_only/nohibernate、已验收音频/USB 等设备状态保持。
当前 LTE 分支尚未合入原工作分支的 USB 默认集成 `fee67b2`；**不得把旧分支的
默认包覆盖当前设备**。原工作区未提交改动已保存在本地 named stash 与独立
`out/` 快照；没有 apply 到 LTE、提交或推送原私有资料。
