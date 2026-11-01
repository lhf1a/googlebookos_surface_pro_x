# SQ1 Windows 视频驱动参考（2026-10-29）

## 2026-11-01：Windows中间保护边界不能直接用于Linux启动

修正扩展SMR管理后，SID2342冲突消失，但复制Windows PIL的FW/CPA=0..5MiB、
NONPIX=0..0仍无法启动Linux42固件；延长等待也无响应。对固定固件早期ARM代码
的分析显示，后者使其五个固件section页表项无效。no-TZ v4将NONPIX_START/END
均设为5MiB（上游no-TZ的最终边界），随后同boot约8ms就绪并发布/dev/video0。
测试test_sq2_venus_boot_ranges.py校验完整固件hash、对应指令与页表模型。
Windows PIL初始化后还有主机保护设置，不应把其一个中间阶段当成Linux最终配置。
随后SQ2驻留驱动通过V4L2六帧CRC、Android High+B六帧Surface逐字节校验及1080p
High300帧播放；完整重启3a8da95c…又通过首次启动、自动选中硬解和原终端HTTPS。
这使SQ1参考真正形成SQ2上的运行验收，但不证明两机所有板级细节完全一致。

## 2026-11-01：SID2342已在SQ2实际故障流中出现

新增no-PAS启动候选实机成功映射固件并释放CPU reset，SMMU故障明确报告SID2342，
从而把下文CSRT/PIL的静态关联推进为实际启动流证据。GFSR80000004的bit2是SMCF，
与未知SID的USF不同。只读快照确认硬件有212组，固件已在第183组配置2342；
旧Linux只管理128组，造成重复匹配。上游5583a55e074b移植后重启复测不再报告
该冲突，但HFI启动等待超时-110，末次CTRL_STATUS未记录，尚无解码帧。保留“固件流已观测”与
“固件完成初始化/硬解可用”的区分。实现、构建及回退记录见HANDOFF/BUILD最新条目。

参考：[上游补丁及同类视频启动故障讨论](https://patchew.org/linux/20250821-arm-smmu-qcom-all-smr-v1-1-7f5cbbceac3e@linaro.org/)。

## 2026-11-01：新归档的安全PIL与CSRT

当前分支已快进到8f64975，完整采集位于`reference/sq1-video-20261101/`。
该归档有用户明确的提交授权，取代下文旧归档“原始资料不可提交”的范围限制；
临时反汇编、设备日志和解包结果仍留在忽略目录。72项manifest全部验证，
qcdxkm8180、qcvss8180和IORT与此前参考逐字相同。平台采集报告
HypervisorPresent/SecureBoot=true、VBS=2；这些状态不证明某段驱动代码实际执行。

新增分析对象是PIL包的`QcSkExt8180.exe`，SHA256
`da09677b8bc3f35c4217e768f73a1ccc19e112cedf724bd14d393e1545511e22`。
INF以`async secure PlatformExecute`注册它；它导入IumSdk的MapSecureIo、
CreateSecureSectionSpecifyPages、AssignMemoryToSocDomain和EmitSmc。
函数标签由日志字符串及调用链推断；精确PDB请求返回404，没有取得符号。

PAS9静态描述符位于VA14001b200，+176=9；+200回调为14000a280，
+224回调为140009760。子系统启动14000af48的顺序为：

1. 14000afa4调用a738(desc,1)，认证前做内存处理。
2. 若描述符+198为0，14000afc8调用ce48，包装TZ PIL认证02000205。
3. 认证成功后14000afe4调用a738(desc,2)，再调用视频启动回调。
4. 视频启动a280→a1c0，成功路径依次调用a090、b4f8、b930。
   a1c0要求CPU仍在reset，a090设置FW/CPA/NONPIX范围；b930清bit4。
   3d20(1)通过性能计数器忙等1微秒，不能把它误当时钟启动函数。

内存处理6590中的源/目标分类结合日志确认3=HLOS、4=HYP、14=HLOS_UNMAPPED。
3→4先以Windows安全section/view管理内存，再返回成功；4↔14在trustlet内
为空操作。视频回调9760随后将有效固件段14→12，权限取自段flags，
支持链接地址与装载物理地址不同，最终进入AssignMemoryToSocDomain。
这不是“Linux只缺一条assign_mem”的证明；编号12也不是SID。

CSRT SHA256为`4620734b5ec83582882a965070952d95fa2bc2cdae5af0a4d14c147dc32f94ee`。
安全扩展3470选择QCOM device100c资源组（文件offset5c）。该组的两个SMMU记录、
40个domain记录、76个权限规则依次有明确长度边界。首个SMMU base15000000，
条目起点relative60、数量592、stride10。视频数据条目与既有IORT相吻合：

| 条目SID | 条目byte6 | 关联 |
|---|---|---|
| 2300/2320/2340 | 3 | 既有非安全视频数据流 |
| 2301/2305 | 9 | 既有安全数据流 |
| 2303/2323 | 10 | 既有安全数据流 |
| 2304/2324/2344 | 11 | 既有安全数据流 |
| **2342** | **12** | 与视频固件段目标编号相同，固件流候选 |

2342位于组offsetaf4（文件offsetb50），原始10字节为
`42 23 01 00 00 00 0c 01 00 00`。其余字段没有全部命名。
同组规则66/67明确为14→12与12→14；规则61为4→14。
没有2302条目。**2342现在有静态交叉依据，但尚未在SQ2观察到实际取指或成功解码。**

安全扩展的177项HWIO表在RVA1b8a0，32字节一项。
相关物理地址为aae0000(version)、aae3000(reset)、
aae1028/102c(FW)、aae1020/1024(CPA)、aae1030/1034(NONPIX)、
aae2010/2000(clock)。CSRT扩展策略列出这四个4KiB页面，
39b8通过OpenCurrentExtension/MapSecureIo获取映射；这是Windows安全扩展
授权上下文，不是Linux EL2直接访问权限证据。

固定输入校验器只做离线解析，不执行PE或访问设备：

```sh
python3 scripts/inspect-sq2-windows-pil-reference.py \
  --extension out/el2-android/sq1-reference-20261101/data/drivers/oem109.inf/QcSkExt8180.exe \
  --csrt out/el2-android/sq1-reference-20261101/data/acpi/CSRT-4620734b5ec83582.dat \
  --output out/el2-android/sq1-reference-20261101/pil-csrt-verified.json
python3 tests/test_sq2_windows_pil_reference.py out/el2-android/sq1-reference-20261101/data
```

真实输入11组指令与数据校验通过；8项负例覆盖hash、ACPI校验和、长度边界、
stride、候选流、权限规则和关键指令。输出文件禁止覆盖。报告不是硬件验收。
下一步应核对2342的Linux固件IOVA、reset访问与TZ认证前置条件；不再猜2302，
也不重复已经无差异的EL1 GR0实验。没有新设备操作；ffcfffb4仍未释义。

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

## Windows 初始化指令已钉住（续接）

新增`inspect-sq2-windows-video-reference.py`，严格固定上述14369cb4…驱动和acfe9033…IORT哈希，只解析PE/ACPI，不执行二进制或访问设备。SQ1新导出驱动和原SQ2保存驱动分别运行，产生完全相同结果。

- `VedHfiReset`失败路径字符串的代码交叉引用指向直接调用`0x1402f5d54 → 0x140301e68`。该初始化函数在`0x140301ed0..1ef0`将version=0/size=192、qhdr0=24/qhdr_size=56、num_q=3/active_q=3实际存到表头。因此24字节布局现在有同字节Windows主机驱动的直接指令证据；原compact设备试验仍无完成结果，不能倒推为已验证。
- `0x140301f04..1f3c`分别算ring设备地址为table地址+192+index×engine_ring_bytes，queue header指针为table+24+index×56。ring大小仍来自engine字段，本轮没有把它冒称为已确定常量。
- `0x1402f61cc`装入`0x01000000`，`0x1402f6330..6334`写共享区域大小；`0x1402f68c8`常量表为d2068/a0068。该Windows路径共享区大小是16MiB，而当前Android私有探针是3MiB。这不是16MiB为所有Linux固件所必需的证明。
- UC基址表达式在`0x1402f61ec..6200`为`u32(object+36)+u32(object+40)-16MiB`，object来自`*(engine+184)`。把该object字段解释为ACPI非安全页表的起点/长度仍需追踪解析入口；不能据此直接设置固定物理地址。
- `0x1402f6398..63a0`向CTRL_INIT写1，常量表区分d2048/a0048，后续为CTRL_STATUS轮询。未对所有其它寄存器访问作不存在性断言。

六项指令/布局检查和十项IORT映射核对全部通过。另执行9项检查：两个独立驱动输入结果一致；错误驱动/IORT哈希、非ARM64 PE、错误qhdr0写指令、坏ACPI checksum、越界mapping均拒绝；已有报告不可覆盖。结构/指令负例仅在测试内重绑hash，正式工具没有放宽固定输入。测试夹具首次误用空patch.multiple导致exit1，已改nullcontext并重跑全部通过，不是被测硬件失败。报告和原始反汇编仍在忽略的`out/video-decode/sq1-reference-analysis/`。

本步是有效进展：把队列表头/区域大小从猜测提升为可重复的主机写指令证据，但SID编码语义及SQ2实际stream仍未硬件证实。下一可执行步骤是创建显式、可回退的2300参考SID候选，**先独立DMA分配/映射/解除映射，不启动VPU、不加载固件或读MMIO**；不得静默替换正常启动配置。该阶段只排除主机侧映射错误，仍需随后真实共享队列响应、解码帧、MediaCodec和应用播放。

## SQ2 待取日志已取回，本轮没有新VPU试验

旧`/data/local/tmp/sq2-hfi-empty-76f0.kmsg`已取回至`out/video-decode/sq1-reference-analysis/sq2-log-retrieval.stdout.txt`。日志证实注册、probe、ICC获取已越过，PAS auth=0/protect=0，九次邮箱写入全部有结束标记。最后一条为540.670400s的`SQ2_HFI_BOOT begin`；没有CTRL状态、empty marker、SYS_INIT或关闭结果，也没有KMSG_END。该位置之后的源码顺序为trace等待、CTRL_INIT写、VERSION写、CTRL_STATUS读；落盘最后一条不能定位精确阻塞指令，不能把未出现结果记为失败返回或安全回收。

当前只读核查boot为`39906e2a-ade5-4e41-984f-e7ef6d063588`，Android boot_completed=1、screen_only=Y/nohibernate、system_server=2295、surfaceflinger=2104、临时视频模块已无、pstore空。与旧76f0不同；恢复方式本轮未确认，不称自主重启。用户此前明确说SSH中断时桌面/触摸正常，保留这一事实，不能回写为整机冻结。本轮未重启、未写启动盘、未复跑VPU。

## 采集器验证边界

事务角色保存在`out/video-decode/sq1-reference-transaction/`：BASELINE.ps1、MODIFIED_FILE.ps1、DIFF_FILE.patch、VERIFICATION.txt、可执行ROLLBACK.sh。基线/修改版/回滚副本用相同`-CheckOnly`输入在工作站均exit0、eligible=false；回滚哈希等于原始124a55e7…。差异补丁实际重建得到修改版76259775…；PowerShell AST和嵌入C#编译通过。型号不符路径不是导出行为验证；实际SQ1上修改版CheckOnly=true及五包导出才是本轮采集证据。首次本地执行被execution policy拒绝exit1，后仅在子进程使用Bypass；没有改持久策略。首次原生keyscan不支持默认KEX，改用WSL扫描后匹配用户指纹；没有绕过主机校验。
