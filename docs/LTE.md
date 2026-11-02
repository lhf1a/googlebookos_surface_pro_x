# Surface Pro X SQ2 LTE 开发

## 2026-11-02：QMI EFS原厂IDL已恢复，实机元数据通过；文件读取返回Malformed，保存检查点

本轮没有重启、online、清FTM或NV写入命令。原厂MPSS SHA9ec7b6c6中的服务21对象
0xc4386888与类型表0xc215eae0已恢复：IDL6/1.2，命令1e/1f/20/21。只读lab helper的
mfs_request新增21编码；20（路径/数据/flags/mode）始终拒绝。以固定/test输入，原厂
表配合Qualcomm qmi-framework 9cd31554编码器得到与候选逐字节相同的报文，往返解码
一致；native和ARM64/QEMU自测通过。21读取语义来自完整输入/输出结构，尚未追完原厂
分页handler；不能把结构推断冒充原厂源码函数验证。

实机仍在boot7ce：EFS端点0/19的1e返回成功，支持001e,001f,0020,0021；1f成功。
固定不存在路径的21返回result1/error1(MalformedMessage)，无fs_errno或data；按停止
条件退出3，其余五个RF路径未发送，没有读取到NV内容，不能推断文件不存在或校准缺失。
下一步追原厂fs_qmi_service的请求校验和字段语义，不原样重跑、更不发20或盲换报文。

REA实际识别原MPSS为QDSP6；原Windows路径不支持已纠正为/mnt/c路径。LLVM18已在
私有out提取（未安装系统包），原件未改；合成分析ELF只增节表，原有可执行段反汇编完成。
该输出不包含所有分页代码。已定位高熵段、delta表与CLADE参考，尚未解压CLADE，不把
REA伪xref或未验证解压当证据。所有输入、错误、工具版本、源码、二进制留在私有归档。

用户要求保存并推送全部开发进度与必要二进制。停止新增实验，源代码/可公开构建产物
推送LTE分支；完整原件/NV/带登录配置启动包必须加密或进指定私有远端，私钥不上传。
保存前最后一次完整实机健康检查（本轮live-before）通过DMS2/NAS0/PS2/WDS1、IPA2、
Android1/user10、三remoteproc attached、ALSA/AUDIO_READY/HAL774及原两项菜单。
MFS实验后的保存复查SSH连接超时(exit255)，尚未取得新健康输出；不能称实验后检查已通过，
也不能把一次网络超时判断为板卡重启。当前冷回滚没有执行，副本源码回滚另有A/B/R记录。

## 2026-11-02：原厂FSG后端已定位，QDB已解码；DIAG出现端点但未取得内部日志

保持boot7ce90b0d的own NV/量产HW active/ROW active和IPA，不重启、不重复online。
实际firmware属性及Android namespace/ESP两份qcmpss8180.mbn均SHA9ec7b6c6，与原
SQ2 Windows原件相同；INF明确区分完整LTE与仅WLAN/GPS的_nm文件。本次排除所检
文件误用_nm，不把磁盘文件hash单独当成运行内存全量一致证明。

REA重新打开原qcremotefs/QcSOCPartition；REA地址反汇编未识别，改由PE.pdata和
ARM64原机器码验证。FSG特殊路径0x1400038f4调用0x140013870，GUID为标准FSG
638ff8e2-22c9-e33b-8f5d-0e81686a68cb；请求offset/length置+88/+92，通过
IOCTL ecaf2b46到QcSOCPartition的0x14003de7c->0x140042bb0。
后者分成块设备及普通文件两路，普通文件最终ZwReadFile；块设备路径含对齐和
partition-start+offset/block的计算。没有证据支持任意去掉512字节，也没有证明
FSG内容已经被modem接受、已解密或已经包含可用RF校准。块后端深层0x14003c840
仍须与实际介质对应，不能把未追完的分支称为透明/解密后端。

本机原厂qdsp6m.qdb SHA4723a56f，实际从0x40以zlib解压，完整消耗压缩流且无尾余，
得到23877098B/275576行。私人out保存解析器、原始解压文件及RF/NV/DIAG消息索引。
其中rfcommon_core.c的RFC NV读取失败、rfc_dynamic_load失败、fs_rmts认证失败等
只是固件消息模板，不是本机发生过这些错误的日志。不得据模板宣称校准缺失或密钥绑定。

新的实机区别：标准QRTR DIAG4097/control0临时发布后，modem CMD instance1的
0/110出现；AP服务移除后此modem端点仍在。修正本地helper opened()使用getsockname
取得AP node，而非把modem node0传给bind（初次EINVAL完整留档）。10秒未收到control
数据；其后raw/HDLC version均超时，停止报文变体。按上游对照补齐有界AP control0/
data2/DCI4会话后，EFS HELLO仍rx0/timeout，未发生feature协商、未取得目录或NV内容。
不把超时当“文件不存在”，不对未知4098/4099发DIAG。不再原样重跑这些实验。
AP三个服务与socket均已撤下；modem CMD0/110仍在，不谎称服务表完全恢复。

实验后同一finite observer实际exit0：DMS2、NAS0/PS2、WDS1、rmnet_ipa0保留；
Android完成1/user10、MPSS/CDSP/ADSP attached、物理ALSA/AUDIO_READY/HAL774cd7e7
以及原DT85b/init957/menu9f保持。没有发送RF/模式/PDC/NV/分区写入命令，没有新启动或后台诊断。
LTE仍未可用。下一步用原厂传输实现/已解码QDB还原DIAG握手或精确QMI EFS只读协议，
取得真实RF初始化/校准状态后再修正；不继续猜frame、盲换NV或清FTM。
同四角色新增rf-backend-20261102-v1。新QRTR bind native fixture A/B/R只证明源代码
本地节点修正，不是LTE通过或实机冷回滚；上一轮源/启动包A/B/R成果继续保留。
上游协议参考：[linux-msm/diag](https://github.com/linux-msm/diag/tree/23c12c167e93215853af4e59c021551767f6fec8)，
[EFS只读布局参考](https://github.com/catxeon/qcom-efs-browser/tree/9fde4f92a0cc49fc645cb70d19419f553108cb53)。

## 2026-11-02：本机量产MCFG已加载并随自生成NV启动生效；剩余FTM/RF就绪

实质推进：原SQ2 Windows包/运行固件精确匹配微软量产HW MCFG.28(20140B,f6e846c8,
DSSA-WD-SC8180X/08006000)和SW MCFG.12(25768B,cf5f056d,SW_DEFAULT/08e00003)。
不能只按名称选择：通用DSSA同名24588B，不是本机20140B原件。

新手动lab helper实现固定原件SHA/大小/metadata门控、PDC标准分块加载和选择；不含
activate/reset/deactivate/delete构造器，原sq2-pdc-readonly未改。实机首次每块新建QRTR
socket导致remaining重复19116；纠正profile_exchange为全序列同一socket且独立transaction，
实机第二块18092，20+26块分别到0，两个metadata精确匹配后PDC16均变为own pending32。
公开build-only脚本不会连接设备。native host/ARM64/QEMU及本地client-scoped服务fixture通过。

modem随后确实写入rmtfs -r的RAM shadow，非原文件。对精确hash85668122的原RMTFS，
只读小.bss验证完整10项rmtfd ID/路径/长度/映射，双读与两次间隔快照相同，私下保存
FS1 b4a03c5f / FS2 201ff660，各2MiB；FSG仍零。没有密钥扫描、信号/ptrace或进程写入。
这是一对本机新生成NV，不是SQ1移植或只分离的校准数据；原文件保持零，物理NV分区未操作。

一次启动initfsown.cpio.gz SHA b7b3465f…只追加这对own NV/seed，密码addon仍最后、
其他有效项相同。原957/DT85b/launcher/Image和Mesa未覆盖。boot7ce90b0d实际ARM消费、
own once0、RAM精确hash，RMTFS读2:4094正文而非早期只读sector1后FSG fallback。
实际ID比较确认本机20140B HW已active32；软件自动选ROW_Commercial active20，非猜列表顺序。
PDC端口本次26而非旧27；一次旧端口只读查询无有效回复后已改为重新发现，保留原错误记录。
原菜单9f已恢复。相同IPA9b6/原fw510正常PAS/setup并出现rmnet_ipa0；完整Android1/user10、
三remoteproc attached、ALSA/AUDIO_READY/实际HAL774cd7e7通过。

LTE仍未可用：有区别的own NV/active MCFG/IPA环境里，单次online仍52且恢复2。
随后仅一次标准1低功耗->0在线验证：1接受且回读1，0仍52，最终2/restore_failed0。
不继续试其它模式。DMS GetCapabilities成功但radio_interfaces0；GetSupportedMessages
返回94(NotSupported)，未发送FCC命令，不能据常见笔记本FCC案例假定本机需要/支持它。

已使用REA并以原PE.pdata/ARM64机器码复核qcmbb8180：0x140065c88比较mode2，写
context+32的FTM flag；0x140065970在该flag下直接阻止radio state set，未进入DMS2e。
这是原Windows也处理FTM的证据，不证明FTM成因，更不是清NV标志的许可。尚未验证单位
RF校准/初始化为何缺失；不得把有效EFS/MCFG等同LTE成功，也不得强行清FTM掩盖问题。

当前保留boot7ce的own-NV/active MCFG/IPA先进调试环境，DMS2和原菜单已恢复；没有
当前冷回原DT之说。下一正常重启仍原高通，Mesa第二。副本ROLLBACK/native A/B/R独立
记录，不能冒充实机冷回滚。下一步沿原单位FSG/校准后端和实际MPSS RF初始化查证，先
形成一项有证据的修正再online；不再重复MCFG/NV盲换、FCC magic或原样online。
原四角色继续扩展pdc-selection-20261102-v1；全部NV/原件/生日志仍在ignored out/private。
协议：[libqmi PDC](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-pdc.json)，
其qmicli明确activation可能reboot，故本轮使用正常整板一次启动，不直接reset adopted MPSS。

## 2026-11-02：IPA＋SQ1 FS12组合已实机验证并冷回滚；转向MCFG选择缺口

复用完整IPA4.1模块9b6e6dd6、原固件510f1002和DT49228267，直接完成新的全Android
一次性组合启动bfde2445：FS1/FS2 RAM分别精确匹配62528314/1247bc6f，FSG仍零。
新helper只改arm路径为ipa-fs12.ARM，原fs12-seed.ARM.used历史不动；选择器和ARM
均实际消费。独立initfsip4.cpio.gz SHA168fae14…，原init957/DT85b/launcher/Image和Mesa
均未覆盖。正常完整IPA再次初始化成功、rmnet_ipa0出现、GSI/IPA Edge IRQ14/1。
本次有新NV输入后只执行一次bounded online，仍error52；实际恢复mode2/restore_failed0。
RMTFS仍仅读FS1/FS2 sector1各1sector、随后FSG全4096sector，不能称NV解密/接受通过。

停止整份镜像试换。正常rmmod IPA实际exit0后，已冷启动回原高通boot e8efb7c4：原DT
实验节点不存在、三个RAM输入恢复同一零哈希5647f05e、rmnet_ipa0不存在，这是回滚预期，
不是丢失已保存驱动成果。原菜单9f恰好高通默认+Mesa两项。A/B/R均DMS2、NAS0、PS2、
WDS1；Android1/user10、三remoteproc attached、物理ALSA/AUDIO_READY/实际HAL774cd7e7
均通过。没有真实蜂窝流量、没有提升默认、没有物理NV分区写入或adopted MPSS reset。

推进到新readiness线索而非重复IPA对表：新增手动只读sq2-pdc-readonly.c，仅允许0x22
Get Selected /0x24 List /0x28 Get Info，不含select/load/activate/reset/delete；配置ID只在
进程内存，不输出，公开输出仅存在位/长度/计数/版本/描述。host与ARM64/QEMU fixture、
端点拒绝测试通过。实机PDC0/27：平台9、软件10，19项metadata全部成功；但两类当前
选中配置的异步indication均error16(NotProvisioned)。响应result0不等于最终操作成功。
平台包含SS-WD-SC8180X(08006000)、DSSA-WD-SC8180X等；软件包含MSFT_OpenMkt的
CU/CT/CMCC和ROW_Commercial。尚未选中/激活；此缺口不能单独证明error52根因。

REA已用于原qcremotefs8180.sys，另用原PE.pdata/ARM64 objdump/IAT交叉核对真实FileRW
0x1400034f8，而不信泛化线性伪C。实际sector左移9，非FSG普通文件走ZwReadFile，
不是“去掉外层512字节就修好”的证据；Linux原storage shadow按同一offset复制。FSG
另有专门后端分支，后续须分开。不能仅凭高熵宣布芯片绑定，也不提取/公开密钥或身份项。

下一步：按原SQ2 Windows/固件配置选择路径匹配现存平台/软件MCFG，并建立可恢复的
RAM验证方案，再做一次有区别的readiness实验；不再盲换NV、不原样重发online。
当前原版正常环境，IPA支持候选和全部原版参考仍在本地归档，可直接复用。
证据继续扩展原四角色的readiness-ipa-fs12-20261102-v1，不新建替代角色。
协议来源：[libqmi PDC schema](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-pdc.json)。

## 2026-11-02：完整IPA4.1已实机初始化，rmnet_ipa0出现；LTE尚缺online

用户要求加快、先假设参考可用。已以固定SM8150表实现完整6.3.8 IPA4.1 lab补丁，
不是旧6寄存器拒绝占位：新寄存器表/GSI selector/version support，8端点、4资源组、
完整SRAM表、原版IMEM146bd000/BCR39，独立ipa_sq2_lab模块只匹配sc8180x。
sq2_lab_mode默认0拒绝，1只读预检，2真实完整初始化/PAS15/IRQ/DMA路径。
公开patches/sq2-ipa-v4.1-lab.patch与build-only脚本/测试；原kernel源码未改。

原DT85b上新增独立ipa@1e40000、SID520/522、core RPMh12、ICC/SMP2P、
9b000000/60000 no-map+FDT reserve-map暂存区。原memory节点size0是EFI占位，
初次DT RAM断言失败exit1后按现机/proc/iomem确认EFI RAM且不重叠，不捏造DT RAM。
保留原ipa-virt。原DSDT的IPA/GSI均Edge ActiveHigh，不照搬手机GSI Level。
一次选择器实际消费once0；恰好原高通默认+Mesa两项。原DT/launcher/Image/init957
未覆盖，菜单已执行同一ROLLBACK恢复9f。当前仍运行实验DT492，不谎称已冷启动回原DT。

实机boot e5874e01-33ae-4127-9082-12a05eb9b687：SHARED_MEM_SIZE=00000500
=>0x2800，FLAVOR=0a0d0a17=>TX10、RX10..22，单设备translated IOMMU group13。
预检初始-34因6.3枚举空槽，改用既有ipa_gsi_endpoint_data_empty，未放宽有效范围；
native实际loop回归验证空槽与非空越界。补头ipa_gsi.h修复一次Kbuild implicit-decl exit2。
修正后mode1 gate0，预期-EAGAIN，clock释放。mode2首到empty region11(-22)，
删除原size0的可选AP_HEADER项而不虚构大小后，完整配置被驱动接受。

原版ipa_fws.elf37184B/SHA510f1002…经PAS15标准路径实际加载；不是手机固件。
firmware_class.path临时前置host RAM资产，用/proc/PID/root使init mount namespace可见；
初次路径尾newline导致request_firmware -ENOENT，改为printf无换行后成功，原
/debug_ramdisk/sq2-firmware搜索路径已恢复。没有写Android vendor或原启动包。
当前模块9b6e6dd6…实际bound，日志IPA driver setup completed successfully，
异步注册rmnet_ipa0；GSI IRQ464 Edge计数14、IPA IRQ343 Edge计数1。RX/TX仍0，
不称数据流量通过。早期unexpected init_completed response没有阻止随后网卡出现。

在这个实质不同的IPA已工作环境，仅一次重试现有bounded online助手：仍result1/error52
DeviceNotReady，实际恢复factory-test2并回读restore_failed0。未改APN/MCFG/NV或发起
数据呼叫；SIM在，NAS未注册/PS detached/WDS disconnected。不能把IPA成功当LTE成功，
也不能据error52单独认定EFS为唯一原因。Android1/user10、三remoteproc attached、
物理ALSA/AUDIO_READY/实际HAL774cd7e7…均在完整初始化后复核，WiFi保留。

为加快后续，保留当前活跃IPA实验环境，不额外冷启动来抹掉已取得的网卡。原菜单9f
已恢复，下一正常重启回原高通；Mesa可用。软件副本实际portable rollback/原哈希和
native loop A/B/R另记证据，不能拿它冒充实机冷回滚。下一步直接处理基带readiness：
核对当前RMTFS实际后端/接受路径，复用已保存原版EFS/MCFG证据；在工作IPA条件下做
一次有新输入的RAM参考组合验证，不再逐项重复IPA参考、不原样重发online。
当前模块/原固件为RAM手动加载，尚未持久集成或提升默认。sensor/相机/真suspend不动。
证据与旧四角色同父扩展：out/lte-reference-20261102-75886ecf/ipa41-lab-driver-20261102-v1/。

## 2026-11-02：SQ2本机IORT已取得，AP GSI实读未启用

上一目标轮为progress；本轮手动只读模块从当前EFI ACPI20=ffffe018沿RSDP/XSDT取得
本机IORT（物理fff91018，6467B，26nodes，XSDT14项，0拒绝），校验和全过。
SHA acfe9033…与保存SQ1 IORT逐字节相同。\_SB.IPA映射520..522到SMMU15000000，
id_count2为包含末端的三ID范围（同时核对本地Linux IORT映射条件）；不是当前流占用许可。
模块只允许EFI ACPI_RECLAIM/NVS+WB RAM，默认拒绝、无任意地址参数，不调用EFI服务。
实际A/B/R：无探针→有有效IORT→rmmod后无探针，三次observer exit0。

随后单次clock-owned AP GSI检查：IPA4.1 gate通过；物理1e23000的GSI_STATUS=0，
AP未启用，故按门控完全不读channels3..6上下文。contexts参数零是未读初值，绝非
“四通道空闲”证据，也不推断Q6状态。仅版本+AP状态两次readl，无GSI写/IRQ/DMA。
rate0→100MHz→0、enabled N→N、restore0，实际卸载后同observer exit0。
两种默认无allow/confirm参数均先验证insmod1且明确拒绝日志，确认后均insmod0。
两个三阶段QMI均factory-test2/未注册/PS detached/WDS disconnected；Android1/user10、
三个remoteproc attached、物理声卡/AUDIO_READY/实际HAL774cd7e7…及原DT85b/包957/
菜单9f保持。同boot2e535，无新重启、NV/启动文件/固件写入，LTE仍未可用。

实际host测试：纯ACPI助手、真实RAM范围guard fixture、严格IORT坏表/边界和GSI位域。
首轮host -Wextra/-Werror因内核正数保护后的signed nr_map比较报错exit1；修正host
命令按内核策略加-Wno-sign-compare后3测试通过，未改变已实测模块源码/字节。
IORT模块fef84843…、GSI模块cc295a27…构建通过；build-only脚本均不安装/加载。

已复用原采集包ipa_fws.elf（37184B，SHA510f1002…）：原始PHDR flags=08000007，
确认可重定位；Linux MDT规则计算最小暂存跨度0x57000，不是ELF文件大小或手机示例
0x10000。仅静态检查，未加载或调用PAS；Linux gsi_setup会在enabled=0直接-EIO，
所以不能用skip-loader假装已启动。host和Android mount namespace按名称检查均未找到
IPA/GSI固件，当前DT没有IPA保留区；这只是所检路径/名称结果，非全盘固件不存在证明。

下一步必须做完整IPA4.1候选（不再只增加诊断占位）：用固定SM8150原始memory/resource/
endpoint表、原版IMEM146bd000/BCR39及已知寄存器差异，翻译到6.3.8并编译。为原版
可重定位固件设计至少0x57000且不重叠的独立暂存区，核对PAS15/loader时序；新的一次性
DT保留原高通/Mesa，先实际preflight/probe再决定启用固件与DMA。不reset adopted MPSS，
不借手机固件/地址、不重复online或NV试换。本轮证据/旧四角色继续扩展在
out/lte-reference-20261102-75886ecf/ipa41-iort-20261102-v1/。

## 2026-11-02：新增SM8150/855同代IPA4.1参考

已实际下载/解析固定Qualcomm SM8150 DT：IPA4.1(enum15)，AP/WLAN/uC SID分别
520/521/522，IMEM146bd000/2000，与原SQ1参考吻合；Kona/865为IPA4.5(enum17)，
SID5c0/5c1/5c2。提取原ipa_4_1_mem_part的86字段、5类源/2类目的资源及QSB，
写入configs/sq2-ipa-v4.1-reference.json，全部reference_only、SQ2兼容性unverified。
还固定下载Hotdog Linux6.16形状补丁，8端点/23组SRAM offset-size与下游吻合，
但IMEM146a8000、resource mask BIT(0)及本项目6.3.8的BCR字段/loader必须区别处理。
未执行外部项目脚本、未移植或部署手机驱动；原硬拒绝guard保留，LTE未可用。
详见docs/LTE-IPA41-REFERENCES.md和原父四角色的ipa41-ownership-20261102-v1扩展。
当前同boot2e535；只读sysfs确认ACPI20=0xffffe018，acpi=off无IORT导出。
下一步受限读取当前固件IORT，再核实GSI AP流/环，不能因参考SID吻合就启动DMA。

## 2026-11-02：IPA4.1 寄存器差异、SMMU只读验证及离线候选

继续LTE目标。本轮在同一SQ2 boot `2e535372…` 实际扫描apps SMMU：
ID0 `7c017ed4` / sCR0 `002d1c06`，212组SMR中，原SQ1 IORT候选SID
0x520/521/522匹配数为0/0/0。探针不写SMR/S2CR/CB，不建DMA、不清固件映射；
init释放映射，已实际rmmod。无匹配不等于当前EL2可占用这些SID，所有权仍未验证。
同一observer三阶段BASELINE无探针→MODIFIED读到212/0,0,0→ROLLBACK无探针，
均exit0；QMI始终DMS2 factory-test、未注册、PS detached、WDS disconnected。
Android1/user10、三个remoteproc attached、物理ALSA/AUDIO_READY/实际HAL774cd7e7…
及原DT85b/包957/菜单9f保持，没有新重启或NV/启动包变更。

固定Qualcomm下游commit `c8bbb1e3a59cb5cde2da79a8db638691b70535d7` 的
ipahal_reg.c/_i.h及include/linux/ipa.h。实际解析14个有效寄存器项和8个端点：
4.1继承4.0；与SC7180/4.2不能直接等同：idle在0x220而非0x240；TX delay bit1
仍有效、RX suspend已移除；HOL timer整32位而非4.2 base/scale；资源组mask0x3；
BCR参考0x39而非0；aggregation bitmap为23位而非17位。均为源表/位域证据，
不是本轮向硬件写入的值。此前实测+0x30在该下游命名IPA_COMP_HW_VERSION，
与+0x34的IPA_VERSION不同；实测值0x40010000及4.1.0解码不变，未读+0x34。
辅助数现已由struct定义确认是ipa_if_tlv/ipa_if_aos，绝非TRE/event ring长度。

在原6.3.8 IPA子树副本生成 `patches/sq2-ipa-v4.1-reference.patch`：6个已核实
寄存器定义、8个endpoint/channel/TLV参考项及qcom,sc8180x-ipa早期硬拒绝分支。
真实完整模块编译exit0，SHA `f219b9c0…`；未安装/加载。它是不可部署的离线参考候选，
不是IPA支持完成：probe在power/MMIO/IOMMU/IRQ/DMA之前返回-EOPNOTSUPP，
version_supported仍拒绝4.1；不填猜测的memory/resource/ring。原源码/运行模块未覆盖。
`scripts/build-sq2-ipa41-reference.sh SOURCE BUILD FRESH_OUTPUT`只在新目录复制、
应用patch、运行实际C早期分支fixture并编译，不含安装或硬件操作。

原父四角色继续扩展，portable ROLLBACK新增ipa-main副本模式；组合patch逐字节重建
全部历史角色及新IPA子树。实际C fixture同输入qcom,sc8180x-ipa：原版0→候选-95
→执行ROLLBACK恢复副本后0；这只是早期分支验证，不是实机probe或LTE成功。
证据：out/lte-reference-20261102-75886ecf/ipa41-port-20261102-v1/。
下一步先核对本机固件IORT/SID角色与GSI AP环所有权，再建立独立4.1完整内存/资源配置，
不能解除guard后用SC7180参数试DMA。LTE目标仍要求online、注册/PS attach、bearer、
蜂窝接口/IP/路由/实流量和Android集成；目前未实现。以下为已完成的历史实验。

## 2026-11-02：实机读到 IPA 4.1.0，LTE 目标仍未完成

继续目标“LTE可用”。本轮实际单次读取 `0x01e40030` 得到 `0x40010000`，按原版
驱动位域解码为 **IPA 4.1.0**，不是 SC7180 的 IPA4.2。默认无确认参数的手动模块
先验证为拒绝（insmod exit1，明确日志证明未碰时钟/MMIO）；确认后只读一次，不写
IPA/GSI、不建DMA/IRQ、不重置MPSS、不改NV或启动包。没有新重启。

RPMh provider `qcom,sc8180x-rpmh-clk` index12，Linux记录 rate `0→100000000→0`，
enabled `N→N`，restore exit0。映射、时钟引用在 init 返回前已释放；随后实际 rmmod
成功。三阶段同一只读观察命令：BASELINE探针不存在 → MODIFIED存在且validY/版本4.1.0
→ ROLLBACK探针不存在，均 exit0。QMI三次均factory-test/未注册/PS detached/数据断开。
Android1/user10/三个remoteproc attached/物理声卡/AUDIO_READY/实际HAL774cd7e7…保持。
boot仍2e535372…，原DT85b/包957/菜单9f，高通默认和Mesa回退不变。

新增手动诊断源码 `probe/test2/sq2-ipa-version-read.c`、纯位域头、host测试和
`scripts/build-sq2-ipa-version-read.sh`，默认拒绝，绝不接入开机。公共build-only脚本
重建与实测模块SHA均 `2d686514…`；3项host/策略测试通过。它是已完成的诊断，不是LTE驱动。

已固定公开 Qualcomm 下游源码 commit `c8bbb1e3a59cb5cde2da79a8db638691b70535d7`
的 `drivers/platform/msm/ipa/ipa_v3/ipa_utils.c`，内容SHA `27c1d76e…`。
其IPA4.1 Q6 endpoint/channel四对与SQ1原版完全一致：3/0、4/1、13/3、14/4。
由该表得到 AP参考候选：COMMAND_TX=5/4、LAN_RX=10/5、MODEM_TX=2/3、MODEM_RX=11/6
（endpoint/channel）；所选端点方向亦与原版静态表一致，但没有进行数据传输验证。
`configs/sq2-ipa-v4.1-reference.json` 固定来源、全部8个角色和未决项，显式标记
implemented=false / data_plane_validated=false，不能当可部署的驱动配置。

下一步继续实现SQ2 IPA4.1独立候选前，先核实SMMU SID、memory/resource/bitfield/GSI
所有权；现6.3.8仅定义4.1枚举，未在supported或寄存器选择器中实现4.1。
LTE完成仍要求真实online、网络注册/PS attach、bearer、接口/IP/路由及明确走蜂窝的流量，
以及Android集成。不能把版本读取、静态参考表或Wi-Fi访问当成功。
证据：`out/lte-reference-20261102-75886ecf/ipa-version-read-20261102-v1/`，
同一父四角色继续扩展；以下原SC7180/FS12等为已完成历史实验。

## 2026-11-02：直接 SC7180 compatible 实机对照完成，probe 缺 core clock，已恢复

用户要求直接套用 `qcom,sc7180-ipa` 测试。本轮严格单字段：将有效 DT
`/soc@0/interconnect@1e00000` 的 `compatible` 从 `qcom,sc8180x-ipa-virt`
改成 `qcom,sc7180-ipa`。原节点不是完整 IPA 硬件节点；未猜测/导入 SC7180 的
寄存器、端点、内存、IRQ、SMMU、时钟表，也未修改 NV、APN 或请求 online。
反编译整树比较仅此一处不同；候选 DT `7988daed…`，原 DT `85b436a3…`。

- BASELINE boot `dfa2e915-3266-434b-92bc-47c6d4f4dc16`：原 compatible；同一 `modprobe ipa` exit0，节点无驱动绑定，无该 probe 错误。
- MODIFIED boot `a7469557-8608-4636-aa5f-9f7ef7fa8027`：运行树/alias 为 `qcom,sc7180-ipa`；确实触发 IPA probe，原始错误为 `error -ENOENT: error getting core clock`，随后 `probe of 1e00000.interconnect failed with error -2`。模块加载 exit0 不等于设备 probe 成功；没有 IPA 数据接口。
- ROLLBACK boot `2e535372-2eea-45ff-8073-fe7b3f09fb15`：恢复原 compatible；同一模块/只读 QMI 观察命令 exit0，无该 probe 错误，节点仍未绑定。三阶段均 DMS2 factory-test、NAS 未注册、PS detached、WDS disconnected。

各阶段 Android completed1、user10、三个 remoteproc attached、物理声卡、AUDIO_READY
和实际 HAL `774cd7e7…` 检查通过；不是声学听感或 LTE 联网证明。首个 probe 错误后停止，
本轮没有继续补猜测参数。**结果仅证明驱动匹配生效、缺 core clock 阻断初始化，不证明
SC7180 IPA 版本/端点适配或不适配 SQ2。**

原高通 launcher/DT/内核/initramfs `957b5ee6…` 与 Mesa 始终未覆盖。
只改 launcher 副本的等长 DT 文件名，选择 `android-ipa.dtb`；临时双项 GRUB 在链入前
持久消费/回读 once=0，实机已确认消费，后续走原高通。现菜单已由同一 `ROLLBACK.sh`
恢复 `9f1ee01e…` 并完成原 DT 重启，仍恰好高通默认 + Mesa 回退两项，无新 timer/诊断自启。
portable rollback 新增 `dtb` 角色，离线和板端独立候选副本均恢复原哈希；原生 patch 字节重建通过。
原始输出、退出值、选择器/候选/回滚证据在
`out/lte-reference-20261102-75886ecf/sc7180-compatible-20261102-v1/`，父四角色继续扩展而非替换历史记录。
后续若开发完整 IPA 节点，必须核验真实 core clock、地址窗口、SMMU、SMP2P、版本及端点，
不能把本次改名试验当成完整 SC7180 驱动移植。以下 FS1/FS2 等为已完成的历史实验。

## 2026-11-02：SQ1 FS1/FS2 一次性 RAM 启动验证完成，LTE 无改善，已恢复

用户明确授权整份 SQ1 FS1/FS2 的一次启动兼容性验证。这不是已分离的纯校准迁移。
只在现机已验收完整 EL2/高通包 `957b5ee6…` 上追加门控助手；未从较旧 LTE 分支
重建默认系统。SQ1 两个 2MiB 文件来自既有卷快照，两次读取哈希一致；不保证应用一致性。
实验只替换 RMTFS 打开前的 RAM FS1/FS2，FSG 保持全零原基线，保留 `rmtfs -r`
影子写入。标记在采用数据前持久消费，下一启动不重复装载；未请求 online、改 APN、
发起数据呼叫或写 NV 分区。高通默认、Mesa 回退恰好两项，菜单/launcher/固件/内核/DT 不变。

- BASELINE boot `89e5bbff-128e-46fe-8099-18e55f4a570e`：FS1/FS2/FSG 全零，DMS2 factory-test、NAS 未注册、PS detached、WDS disconnected。
- MODIFIED boot `fb07d8a6-b737-4eca-aa79-733c7005f675`：FS1 `62528314…`、FS2 `1247bc6f…` 精确装入 RAM，FSG 不变；标记已消费，RMTFS 打开两份并记录读取，但仍 DMS2/未注册/数据断开。
- ROLLBACK boot `dfa2e915-3266-434b-92bc-47c6d4f4dc16`：实际执行同一 `ROLLBACK.sh` 恢复原包并重启，包 SHA `957b5ee6…`、菜单 `9f1ee01e…`，RAM 三份全零且实验助手不存在，状态恢复/保持 DMS2。

三个阶段用同一只读观察脚本/输入，均 exit0；Android completed1、user10、三个 remoteproc
attached、物理 ALSA 声卡、AUDIO_READY、实际音频 HAL `774cd7e7…` 通过。不是听感或 LTE 联网证明。
只证明整份输入已由 RMTFS 提供、完整启动未见上述回归；不证明内容已解密、NV 被接受或校准生效。
此前本机 FSG 实验是 DMS2→5→2，失败后已回退；不能把它误记成本次 FS1/FS2 结果。
本次不继续换更多 NV 或重复切模式；后续需区分 EFS 内容接受路径与独立的 Radio HAL/IPA 缺口。

实现：`probe/test2/lte-fs12-seed`、`scripts/pack-sq2-lte-fs12.py`，默认无标记静默退出，
打包只生成文件。`python3 tests/test_sq2_lte_fs12_seed.py` 的5项真实助手 fixture 通过，
完整候选有效条目仅7处变化，其余全字节一致、凭据 member 仍最后。独立副本可执行回滚及
原生 binary patch 重建通过。候选 `c18316c0…` 只保存在私有忽略目录，不提升为正常功能。
证据与同一四角色扩展位于 `out/lte-reference-20261102-75886ecf/fs12-once-20261102-v1/`；
`result.json` 汇总，父目录 `VERIFICATION.txt` 保留字面命令/输出/错误/退出值和历史结果。
设备保留已记录的原包备份、已消费标记及早期取证文件，无活动 ARM；以下较早“未采集/未部署/暂停”段落是历史状态。

2026-11-02 开始，分支 `codex/lte-support`，基线提交 `73ea6ee`。
独立工作目录 `googlebookos_on_surface-lte`；原主工作目录的未提交改动保留原处。
目标为 192.168.8.194 当前完整 Android/EL2 环境，不从精简 initramfs 推断硬件限制。

**状态：2026-11-02 用户已恢复 LTE 分支上的 SQ1 Windows 原版采集与离线对齐。LTE 尚未实现。**
本轮取回 21 包/115 个 manifest 文件并全部校验，核对 IPA/GSI 实际资源与参数，
找到 Windows 的两个 2MiB bootmodem_fs 文件，但未取得其内容。REA 仅做本地静态分析。
详见 [LTE-WINDOWS-REFERENCE.md](LTE-WINDOWS-REFERENCE.md)。不原样重复 online
请求、不跨机复制 EFS/NV，也不部署尚未验证的 EFI 草稿；下面暂停记录为历史检查点。

## 暂停时的新增结果

1. 更正 remoteproc 映射：MPSS 是 `4080000.remoteproc`，固件为
   `qcom/msft/surface/pro-x-sq2/qcmpss8180.mbn`；`8300000.remoteproc` 是 CDSP。
   首轮把后者写成 MPSS 的记录不成立，以下表格已更正。
2. 核对有效 `wifi-services` 与 `el2-rmtfs-start`：开机为 `/run/rmtfs` 创建
   2 MiB 零文件，RMTFS 使用该目录；`modem_fs1`、`modem_fs2`、`modem_fsg`
   SHA256 均为 `5647f05ec18958947d32874eeb788fa396a05d0bab7c1b71f112ceb7e9b31eee`，
   并实际与 `/dev/zero` 比较确认全零。当前没有接入本机真实 EFS/NV 数据。
3. 单变量 DMS online 实验：先读原模式 2，再请求模式 0；固件返回
   `result=1 error=52`，libqmi 将 52 定义为 `DeviceNotReady`。
   随后恢复模式 2 的请求成功，回读 `RESTORED_MODE=2 restore_failed=0`。
   因首次写请求即被拒绝，没有进入30秒 online 观察阶段。没有复试其他模式、
   reset、NV 写入、APN 配置或数据呼叫。
4. 前后 boot ID 仍为 `836903c9-eda6-449e-8295-e4401fe660b9`，guard Y、
   Android boot completed=1，wlan0 仍 UP/LOWER_UP。错误52本身不能证明空白
   EFS是唯一原因，也不能据此判断射频/天线/SIM不可用。

### 本轮代码与验证边界

- `sq2-lte-online-test.c` 是独立的手动限时实验工具，不扩展只读工具的写能力。
  只接受既有 factory-test 状态；请求 online 后，成功或失败都尝试恢复原值并回读。
  模拟测试覆盖成功、写入后丢回复、明确拒绝、错误初始模式及中断。
  ARM64 静态构建通过，实机拒绝与恢复路径已验证；online 成功路径未实机验证。
- `sq2-lte-efi-snapshot.c` 与 `build-sq2-lte-efi.sh` 是**未部署的 EFI 草稿**。
  目的为在 EL2 前一次性枚举小容量 BlockIo/GPT，按明确 EFS 名称只读导出，
  然后链入字节保留的原启动器。ARM64 EFI 编译通过；QEMU、GPT 边界及交接
  尚未测试，未上传到 ESP，未安装 ARM 开关，也未替换启动器或重启。
  能否通过此接口读到 Pro X 的真实 EFS 尚未知，不称为已经实现存储访问。
- 所有输出与实验原始记录在忽略目录 `out/lte-online-20261102`；设备的
  临时探针副本在暂停收尾时清理。原始日志、固件、校准数据不提交到远程。

### 资料与恢复开发入口

[Surface Pro X EFS issue #2](https://github.com/linux-surface/surface-pro-x/issues/2)
及其 [Wi-Fi 文档](https://github.com/linux-surface/surface-pro-x/wiki/WiFi) 记录了
使用 dummy EFS 的背景；Windows remote-fs 驱动中的 `modem.fs1/fs2` 也是后续
需要核对的来源。不能把其他设备的文件当成本机校准数据使用。
[SCM storage 补丁讨论](https://patchew.org/linux/20260126-scm-storage-v2-v2-0-fa045c7e7699@radxa.com/)
提供固件管理 NOR 的实现线索，但其已测试平台不是本机，且报告 EL2 下访问失败；
不直接移植写/擦除接口。先验证读取接口和本机分区来源，再替换运行时 EFS 副本。

本机 NVMe 已核对：BC711 512GB/CYA5N050012105J77，ESP B027-0512，
marker `SQ2-INTERNAL-512-20261027`，root loop0 来自 p3。
现有 GPT 是 mica 移植布局，没有 Windows modemst/EFS 分区；本轮仅查元数据，
没有修改磁盘。已部署启动器/菜单哈希仍为：

- SQ2Default launcher：`dc7b5ac95097533ed131f280d476e45a68a9c0280c758acd94a966348e651476`
- SQ2Fallback launcher：`1e6da8d09afee823c448dbb4ad01285572cf936f593c9246d066321c8e1be39c`
- GRUB：`9f1ee01e43da1a769514ef6e344e7d7d69ffaa21db88d8870eb8a082f1d635ef`

## 首轮实机基线

boot `836903c9-eda6-449e-8295-e4401fe660b9`，Android user10，
`suspend.screen_only=Y`，正常 EL2。首轮只有按需执行的只读 QMI 查询；
未修改内核、DT、固件、Android 属性、启动包或默认菜单。

| 层次 | 观察 | 尚未证明的部分 |
| --- | --- | --- |
| 固件与控制通道 | MPSS `4080000.remoteproc` 为 attached；DMS/UIM/NAS/WDS/DPM/WDA/IPA 服务均可由 QRTR 枚举 | 服务存在不代表射频、数据通路或 Android 集成可用 |
| DMS | 固件 revision `200717-CA-ceeab88-c8.1-00082.0`；Get Operating Mode 返回 2，即 factory-test | 该模式的来源、可否进入 online 及稳定性未验证 |
| SIM | UIM 返回 1 个卡槽，首卡 present，1 个应用 | PIN/运营商/套餐状态未查询，未读取 ICCID/IMSI |
| NAS | registration=0，CS/PS attach 均为 2，selected_network=0，radio interface 为 0（none） | 当前未注册且未附着；不能判断覆盖、SIM 授权或正常射频模式下的行为 |
| WDS | packet service status=1，disconnected | 未发起数据呼叫、未配置 APN，未验证蜂窝网络流量 |
| Linux 数据通路 | 仅 lo/wlan0/ip6tnl0；有效 DT 只有 IPA SMP2P 子节点，没有 IPA 硬件节点或平台设备 | IPA MMIO/中断/时钟/内存/GSI 通道及匹配配置仍待核对 |
| 内核驱动 | `CONFIG_QCOM_IPA=m` 且 ipa.ko 已打包；模块未加载，无 SC8180X compatible；`CONFIG_WWAN` 未启用 | 不能把加载现有 ipa.ko 当作完整修复；也不能从 WWAN 选项缺失单独推断 IPA 不可用 |
| Android | 未发现标准 Radio HAL 实例、rild/qcril/netmgr；vendor noRil 目录含排除 telephony 的 XML；airplane_mode_on=0 | 排除文件存在不等于框架实际已读取；需核对属性、init、VINTF、feature 和依赖后再接入 |

首次口头将模式 2 称为 offline，核对 libqmi 枚举后已更正；offline 实际是 3。
此处及探针均以原始值与协议定义为准。没有向固件发送切换模式命令。

## 已实现和验证

复用 `probe/test2/sq2-qmi-readonly.c`，增加 NAS Get Serving System（0x0024）、
DMS 模式名称及限制原因字段，保留原 UIM/WDS/revision/audio-domain 查询。
LTE 输出只包含状态，跳过位置、运营商和身份 TLV，不增加任意 QMI 写命令。
解码前检查响应头、事务、完整 TLV 边界、唯一 result、原始 error 和必要载荷；
QRTR 接收端还检查来源及截断。未知或畸形响应失败退出，不伪造成功。

`test-lte-readonly` 每次重新发现 node0/instance0 的服务端口，增加 NAS 查询；
可用 `SQ2_QMI_READONLY` 指定临时二进制，不覆盖系统现有工具。
保留该平台既有的 node0/instance0 选择；不要将其作为其他平台的自动选择算法。

独立构建和测试无需音频依赖、SDK 或网络：

```sh
python3 tests/test_sq2_qmi_readonly.py
bash scripts/build-sq2-lte-tools.sh
```

测试覆盖实际 C 解码器：全部八种 DMS 模式及未知值、NAS/UIM/WDS/revision、
原 audio-domain 路径、缺失/重复/失败结果、事务和长度错误、短 TLV、
端点参数拒绝及身份/位置字段不输出。静态 ARM64 构建通过并在实机运行成功。

实测探针 SHA256：
`4beb426bda30a7ba4db1b5e4638c27ffac481d1cbd5cc65d20394cc29d9b6b84`。
原始输出和实验记录保存在忽略目录 `out/lte-baseline-20261102`，不纳入 Git。
工具为人工执行，不加入开机脚本或持续日志；测试后删除设备上的临时副本。
前后 boot ID 不变，screen-only 保持 Y。

协议参考为 libqmi 的 [DMS schema](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-dms.json)、
[DMS 枚举](https://github.com/linux-mobile-broadband/libqmi/blob/main/src/libqmi-glib/qmi-enums-dms.h) 和
[NAS schema](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-nas.json)。
本次下载的原始参考快照及哈希保存在忽略的 sources 目录；它们不参与构建。

## 接下来的实现顺序

1. 核对现有 modem 启动参数和匹配固件/Windows 配置，定位 factory-test 来源。
   本轮可恢复 online 请求已获 error52，勿原样重复；先解决 EFS 来源并记录新证据。
2. 从匹配本机的资源/驱动资料补齐 SC8180X IPA/GSI 与调制解调器通道映射。
   先核对有效 DT、模块依赖、固件及通道，再做独立候选；不能冒用其他 SoC 的
   compatible 或仅复制 SDM845 配置。保留 Wi-Fi 的现有 MPSS/RMTFS/SMEM 路径。
3. 先验证原生数据通路，再实现和注册 Android Radio HAL/数据服务、权限、
   init/VINTF 及框架配置；只删除 noRil 文件或伪造 SIM 状态不足以实现 LTE。
4. 分别验收 SIM、网络注册、数据呼叫、分配的地址/DNS、实际蜂窝流量和 Android
   ConnectivityService。Wi-Fi SSH 连通不能作为蜂窝数据成功的证据。

每次实机变更前记录假设、唯一变量、区分观察和停止条件；连续两次未增加区分
证据即换方向。本轮未验证 LTE 上网、通话、短信、eSIM 或长期稳定性。
保留已验收的音频、图形、触摸、温控、FUSE、Root 插件和两个正常启动载荷。
