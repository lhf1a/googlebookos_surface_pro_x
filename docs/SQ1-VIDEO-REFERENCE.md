# SQ1 Windows 视频驱动参考（2026-10-29）

用户提供第二台 SQ1 Windows 设备用于分析。已按提供的 ED25519 指纹严格核验主机，使用操作者既有公钥登录。CIM 实测为 Surface Pro X、Microsoft SQ1 @ 3.0 GHz、ARM64、Windows 11 build 22631，64 位管理员 PowerShell 5.1。账户、地址、连接材料及原始文件仅在 private，不进入 Git。

## 已完成的只读采集

`scripts/Collect-SurfaceVideoReference.ps1` 是独立 SQ1/SQ2 参考采集器，不放宽原 Collect-SQ2 的型号保护。默认严格匹配 SQ1；`-CheckOnly` 不创建文件，实际采集另需匹配型号/ARM64、64 位进程及管理员。仅创建新输出目录/归档，不安装或卸载驱动，不改服务、网络、安全设置或启动项，不重启。没有采集许可证表 MSDM/SLIC、SMBIOS、用户文件、密码或 Wi-Fi 配置。

通过 Get-WindowsDriver 获取结构化 original/published INF 映射，再用 pnputil /export-driver 导出包。失败会留错误并尝试受限 DriverStore 复制；先检查每层重解析点再递归，避免跟随目录连接。采集 PnP 资源和白名单 ACPI；GetSystemFirmwareTable 不能完整区分重复签名的表，注册表补取也不保证 SSDT 完整。本次 DSDT 来自 ACPI 注册表二进制，IORT 来自固件表 API。

实测五包全部导出 exit=0，均为当前 active INF：

| 包 | 版本 |
|---|---|
| qcdx8180 / oem166 | 27.20.2140.0 |
| qciommu8180 / oem102 | 1.0.2000.0 |
| qcpil8180 / oem109 | 1.0.1760.0 |
| qcscm8180 / oem115 | 1.0.1760.0 |
| qcsmmu8180 / oem117 | 1.0.1040.0 |

另有95个相关设备资源记录、8张ACPI表（DSDT/IORT/CSRT/APIC/GTDT/FACP/MCFG/DBG2），全部ACPI校验和有效。无采集器记录的issue，不代表系统全部资料已采齐。SQ1采集后仍可SSH，显示设备Status=OK；没有实际播放/听感测试。

归档保存在忽略目录 `private/sq1-reference/collection-20261029-144115/collection.zip`；SHA256 `a0abc75056b996028d659c0b9cd705aa7d0ff4ecd548804828355276f04f2f97`。取回后71项manifest全部按长度/哈希验证。原始驱动、ACPI、反汇编不可提交。

## 与现有 SQ2 文件的逐字节对照

此前保存的 SQ2 Windows DriverStore `qcdx8180.inf_arm64_9b83a2ef84864dd7` 与本次SQ1包共有34个文件，**34个全部哈希相同**，不是仅同名：

- qcdxkm8180.sys：`14369cb4acec038f94e76bc42cb65db6bc63031c18fc0d632d7d77e1c5b2b112`
- qcdx8180.inf：`253d600d4672aee525945a0e3cfdf5e291a94e2383e3094f42b86dac43818873`
- qcvss8180.mbn：1142816字节，`891997ec156db83ca660359a30a7c139a923d9b9783ead523051816303614a68`，`VIDEO.IR.1.2-00079-PROD-2`

当前Android探针实测固件仍为固定linux-surface来源的4e93d345…，`VIDEO.IR.1.2-00042-PROD-1`。79版已在原SQ2采集中存在，不能把本次采集误记为首次发现，也不能因Windows包相同就宣称Android PAS/HFI通过；本轮未替换固件。

## 关键板级差异线索：不能继续把 sdmshrike 当作本机规格

ACPICA iasl 20230628 对DSDT/IORT离线反编译均exit=0；另用有界二进制解析核对IORT的26个节点和mapping边界。

- DSDT/PnP共同给出VPU MMIO `0x0aa00000`、跨度`0x200000`及IRQ206/207。该事实来自SQ1，不等于SQ2运行期IRQ已验证。
- DSDT将VIDEO/Venus引擎编号列为4，列出VideoNonSecurePT及4组secure页表。NonSecure条目的数值为起点`0x00100000`、范围`0xbff00000`（按起点+长度理解终点为`0xc0000000`；完整字段语义尚未逐项确认）。
- IORT的`\_SB.GPU0`中，输入高字节04的10项全部指向`0x15000000`的SMMUv2：`04030000/1/2→2300/2320/2340`；`04090000/1→2301/2305`；`040a0000/1→2303/2323`；`040b0000/1/2→2304/2324/2344`。将输入高字节解释为引擎编号是结合DSDT的推断，原始映射本身已核对。
- 现有SQ2探针却从固定下游sdmshrike-vidc.dtsi继承`SID1300/mask60`和非安全范围`25800000..e0000000`，实际分配`dfc00000`。主机IOMMU映射/CPU读回通过不能证明固件实际使用该SID；与Windows板级资料的差异必须优先核对。不要直接把2300或Windows保护范围灌到SQ2，也不要为试错读TZ寄存器。

下一步：离线交叉核对同字节Windows驱动的页表/引擎选择和现有SQ2板级参考，再形成可审计的SID/IOVA候选；采用真实共享队列写入/回复作为硬件门槛，而非控制状态1。仍需硬件帧、Android MediaCodec及实际应用播放，完整硬解未完成。

## SQ2 待取日志已取回，本轮没有新VPU试验

旧`/data/local/tmp/sq2-hfi-empty-76f0.kmsg`已取回至`out/video-decode/sq1-reference-analysis/sq2-log-retrieval.stdout.txt`。日志证实注册、probe、ICC获取已越过，PAS auth=0/protect=0，九次邮箱写入全部有结束标记。最后一条为540.670400s的`SQ2_HFI_BOOT begin`；没有CTRL状态、empty marker、SYS_INIT或关闭结果，也没有KMSG_END。该位置之后的源码顺序为trace等待、CTRL_INIT写、VERSION写、CTRL_STATUS读；落盘最后一条不能定位精确阻塞指令，不能把未出现结果记为失败返回或安全回收。

当前只读核查boot为`39906e2a-ade5-4e41-984f-e7ef6d063588`，Android boot_completed=1、screen_only=Y/nohibernate、system_server=2295、surfaceflinger=2104、临时视频模块已无、pstore空。与旧76f0不同；恢复方式本轮未确认，不称自主重启。用户此前明确说SSH中断时桌面/触摸正常，保留这一事实，不能回写为整机冻结。本轮未重启、未写启动盘、未复跑VPU。

## 采集器验证边界

事务角色保存在`out/video-decode/sq1-reference-transaction/`：BASELINE.ps1、MODIFIED_FILE.ps1、DIFF_FILE.patch、VERIFICATION.txt、可执行ROLLBACK.sh。基线/修改版/回滚副本用相同`-CheckOnly`输入在工作站均exit0、eligible=false；回滚哈希等于原始124a55e7…。差异补丁实际重建得到修改版76259775…；PowerShell AST和嵌入C#编译通过。型号不符路径不是导出行为验证；实际SQ1上修改版CheckOnly=true及五包导出才是本轮采集证据。首次本地执行被execution policy拒绝exit1，后仅在子进程使用Bypass；没有改持久策略。首次原生keyscan不支持默认KEX，改用WSL扫描后匹配用户指纹；没有绕过主机校验。
