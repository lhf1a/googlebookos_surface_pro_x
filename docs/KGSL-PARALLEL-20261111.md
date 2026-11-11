## 2026-11-11：KGSL/SDE 已暂停，保存到当前分支

用户明确暂停后，仅授权保存进展并提交当前 `codex/kgsl-sde-port-v83`。目标保持 paused，
三个子代理已中断；不得因旧段落的“继续/active”自动恢复开发、连接设备或重新构建。
已集成 KGSL58 / SDE17；SDE 三态整体构建仍 exit2/25对象/0模块，16组专项检查通过。
本次将7组未完成候选的52个源码改动另存为 `patches/wip/kgsl-sde-paused-20261111`，
仅做精确patch重建/保存完整性校验，不加入正常配置、不当作功能通过。源/私有产物及原始
证据保留本地ignored目录，不上传；原六个rescue-console文件保持未提交。
暂停续接入口：`docs/KGSL-SDE-CHECKPOINT-20261111.md` 与
`configs/kgsl-sde-checkpoint-20261111.json`。须用户明确恢复后再执行未完操作。

## 2026-11-11：SDE第17组，现代framebuffer格式契约

当前17补丁/315源。msm_framebuffer_create接受6.18 core选定的drm_format_info，同一指针
传至私有initializer、线性大小检查和真实drm_helper_mode_fill_fb_struct，不再重查而覆盖
core选择。内部writeback/stolen入口保持原调用方式，按真实pixel_format/modifier参数查询一次。
保留vendor modified-format校验、失败传播、每plane GEM引用及重复handle引用计数；不是
简单函数指针强转或跳过UBWC校验。msm_drv.h只更新create原型，未改IRQ/mode字段。

线性plane边界改真实rounded width/height及min_pitch，使用u64 min_size避免原32位回绕；
拒绝无效格式/plane数量和尺寸。327个HOST断言（非327独立场景）使用实际构造函数、真实
fourcc/fill helper，KMS/分配/GEM叶子仍模型；涵盖core-info身份、奇数chroma边界、>4GiB
大小、modifier、部分lookup/alloc/register失败、借用和重复handle。五变异先compile0后run1。
三态HOST编译1/0/1，新运行0；真实ARM64 callback/lookup/fill接口probe三态2/0/2。
完整msm_fb.c三态仍2，模式公共头阻塞，不能称完整consumer通过。最初harness把计数变量
命名puts与libc冲突，真实compile1保留；更名put_count后才统计上述有效验证。

整体BASELINE/MODIFIED/ROLLBACK均exit2/25对象/0模块，framebuffer目标API诊断清零。
317文件精确回滚/重应用、315父源与KOUT五输出保持。合并16组checker全部0，额外合并
ARM64 framebuffer probe0。完整模块/MODPOST/实际scanout和provider退休仍未验收。

IRQ/vblank代理仍实现真实PM/IRQ注册失败与原子回调睡眠边界，不接受异步work假成功。
mode代理已将精准增量rebase至独占integrated16副本，继续catalog/bridge/全消费者。
MST发现QTI primary缓存固定connector ID且热插拔路径跳过primary，因此不能用简单动态重建
取消常驻fixed-primary语义。已选择独立真实6.18 core opt-in persistent生命周期扩展，默认
其他驱动行为不变；atomic基础四角色明确未可集成。未来须新配对内核构建，不借旧dev2运行。

证据out/kgsl-resume-20261111/sde-integration-17与sde-framebuffer-api四角色；旧角色全部
保留。下一仍接收完整IRQ/vblank，并完成mode、MST persistent core/driver与late GEM owner。
物理显示、真实提交/fence退休/DDR投票/PM/性能/SQ1-SQ2未完成，无设备操作/发布pins变更/
提交推送，goal active。既有known controller清理/list锁旧open描述已在新config注明后继修复。

## 2026-11-11：SDE第16组集成，controller成功清理与fence诊断接口

当前16补丁/315源。第15组独立修复dsi_ctrl_debugfs_init：debugfs_root发布后立即return0，
不再成功落入error_remove_dir。原API-only四角色和已知缺陷反证不改写。实际同输入主机三态
均compile0，运行1/0/1；成功目录removed从1变0，3个真实file失败仍-13/只清理一次，dir
失败不清理未创建目录。72严格场景通过，5变异先编译0再运行1；不是实机debugfs并发验收。

第16组移除dma_fence_ops已不存在的字符串回调槽；sde_sync_wait/list_dump调用本地
sde_fence_timeline_value_str，先确认ops身份才访问本驱动ctx，以READ_ONCE读取done_count；
外来fence明确unavailable，不把target seqno伪装为当前timeline、不盲转私有结构。seqno仍
单独输出。13个同步/释放/信号相关函数字节不变；不能把诊断迁移称fence生命周期完成。
实际函数/真实ops声明47项HOST检查通过，三态compile1/0/1；foreign仅分配base的负控可
被ASan捕获，另错误计数/吞wait错误变异均先compile0再run1。真实6.18 ARM64窄caller TU
三态2/0/2，完整sde_fence.c三态仍2。初始提取器误禁函数body分号导致ValueError，错误保留，
修正抽取式后才产生编译/运行证据；没有将抽取错误算产品红例。

整合后BASELINE/MODIFIED/ROLLBACK均exit2/25对象/0模块；旧fence成员诊断在modified
消失，317文件精确回滚/重应用，父315源和KOUT五输出保持。合并15组checker全部0（debugfs
已启用require-controller-root严格门禁），额外合并ARM64 fence probe0。完整模块未通过。

模式v2用户态已另完成42TU/5source库严格链接；root重开6个ARM64 ELF及hash（含1个复制
libdrm），证据ROOT-USERLAND-v2-REOPEN.json。kernel catalog/bridge/全消费者仍未完成，
不据此称joint mode通过。MST代理正处理真实atomic payload/固定connector生命周期与
真实链路参数，不能删MST绕过；IRQ代理处理真实request/free、preinstall失败传播/局部回收、
vblank锁与睡眠边界，不能用假成功异步工作吞掉错误。三代理均gpt-6.1-sol/high，写集独立。

证据out/kgsl-resume-20261111/sde-integration-16与debugfs-cleanup/fence-diagnostics原
四角色。下一步验收完整IRQ/vblank、mode及MST候选并语义合并；late GEM/private-owner、
物理显示、真实提交/fence退休/DDR投票/PM/性能/SQ1-SQ2仍未完成。未操作设备、未改发布pins、
未提交推送。历史open条目按组记录前态，controller成功清理缺陷现由第15组闭合。

## 2026-11-11：SDE第14组集成与真实接口验证

当前14补丁/315源。第13组四源21位点改用真实6.18 debugfs scalar void/best-effort
接口，45个原独立调用未改；节点名/权限/value与真实file/dir错误处理保留，不提供假返回值。
host实际15个initializer函数72场景通过，三态旧/新/回滚编译1/0/1，新运行0；五个源码
变异先编译0再运行1。实际ARM64窄caller TU三态2/0/2；完整dp_debug.c仍编译2。
dsi_ctrl_debugfs_init成功落入remove目录是独立已证实遗留缺陷，严格检查运行1；API-only
补丁未掩盖或修复它，后继补丁单独处理，不能称controller debugfs生命周期已通过。

第14组为五个真实消费者补linux/pinctrl/consumer.h，并把sde_plane的
drm_gem_atomic_helper.h放回原pr_fmt定义之后。该include顺序问题由本项目第4组PRIME
插入头文件引入，本轮修正，保留原日志格式、不压制warning。六个ARM64窄probe三态
2/0/2（对象0/6/0），逐项撤销六个负例均2且仅5对象；原日志格式在实际ARM64对象常量中
核对，不冒充运行期日志验证。六个完整consumer TU仍均2，目标诊断27/0/27。

合并后实际全量构建BASELINE/MODIFIED/ROLLBACK对象数25/25/25，三者exit2、零模块。
317文件真实回滚/精确重应用、315父源及五个KOUT输出hash核对；本轮三类目标诊断清零。
合并源14组checker全部exit0；其中提取函数模型和窄API探针不替代完整模块/MODPOST/真机。

模式v2仍未集成，owner/cache基础已有35+134实际函数主机断言与29分配点失败覆盖；root
独立复跑冻结候选两组exit0。尚需完整catalog/identity/bridge/CRTC/encoder消费者，不据此
关闭模式路线。新增用户指定gpt-6.1-sol/high MST代理，负责真实6.18 topology/payload和
atomic commit生命周期，与mode代理协调msm_atomic.c增量；不是删除MST的简化路线。
late GEM/private-owner退休仍无完整交付，KGSL58与配套dev2内核复用，不重复构建。

证据out/kgsl-resume-20261111/sde-integration-14与原sde-debugfs-void、sde-api-headers
四角色，旧角色保留。下一步接收独立controller成功清理修复并继续mode/MST/owner；物理显示、
真实提交/fence退休/DDR投票/PM/性能/SQ1-SQ2仍未完成。无设备操作、发布pins变更或提交推送。

## 2026-11-11：SDE第12组整编与字符串验证纠错

当前12补丁/315源。第11组逐项审计32处strlcpy：30个固定边界改strscpy，strlen后独立
分配的完整字符串用memcpy含NUL，evtlog外参容量限到真实filter数组，保留C字符串截断/终止
及后续分支，不承诺NUL后padding逐字相同。第12组15源/21处kzfree改真实kfree_sensitive，
保留完整分配大小清零再释放语义、补直接slab头，不退化为普通kfree；不是完整析构/退休证明。

实际整编BASELINE exit2/24对象/0模块，MODIFIED exit2/25对象/0模块，真实ROLLBACK
exit2/24对象/0模块。精确317文件回滚/重应用、315父源和KOUT五输出hash核对；两种已移除
API的编译诊断在本轮MODIFIED均消失。模式private_flags/SEAMLESS、MST、bridge/DSI等仍阻塞
完整模块；25对象与减少的诊断条数都不是完成百分比。合并源码12组checker全部exit0。

重要纠错：子代理最初把字符串checker wrapper exit1说成编译失败，root逐字核对发现旧
host native-api编译/运行均0，实际链接strlcpy@GLIBC_2.38（宿主glibc2.39）；wrapper1来自
残留调用扫描。错误说法已撤回，旧记录/工具保存在pre-gate-correction。更正后显式标记为
HOST compiler-poison模型门禁，三态1/0/1；真实4.19语义scalar/word各234均通过，新版三通道
各234。root另实际6.18内核头ARM64窄API TU三态2/0/2，不能将其称完整调用者编译通过。
敏感释放实际21语句与6.18 helper有210模型场景，真实ARM64 API TU三态2/0/2；原扫描器
漏计删除调用的弱点保留并加缺失/重复site拒绝，3个生成wrapper运行反例与3个源码门禁分列。

模式入口v1仍独立未集成：125断言及精确回滚已由子代理完成；另真实Android42 TU和5个
source库--no-undefined链接成功，root重开6个ARM64 ELF/hash（其中libdrm是固定复制件）。
不是HWC服务、全内核模式消费者或真机验收。前三次真实构建失败保留；修正错误架构libc++
输入及独立C++兼容头后才通过。v2继续处理requested/derived/selected、bridge、cache identity
及共享payload资源owner，不能把入口测试或库链接当整条模式路线完成。

证据out/kgsl-resume-20261111/sde-integration-12；旧四角色保留并扩展，绝对路径/SHA见
FINAL-ROLE-PATHS.txt。late GEM owner候选只有部分测试，代理交付阶段工具错误，未闭合/未集成，
不将该工具错误当编译或产品结论。下一步debugfs真实void API及完整mode消费链；owner寿命、
硬件退休/显示/提交/PM/长时/SQ1-SQ2仍必须完成。无部署/重启/提交推送，pins不改，goal active。

## 2026-11-11：SDE第10组完成三态整编，模式请求迁移仍在进行

当前10补丁/315源，配对dev2内核沿用已完成构建。新增三组：SDE/DP五处日志判断改为真实
drm_debug_enabled(category)，保留日志路由/CORE门控；六文件仅加八条实际公开类型头；
dsi_parser_utils.get_named_gpio及函数声明/定义/禁用stub改为const device_node，与6.18 OF匹配。
GPIO读取/index/error原语义不改。原checker一度错认OF形参名，保留失败，v2改为提取实际名字，
不把该工具错误计为产品红例。新GPIO64实际源码模型场景通过，完整真实头两配置ARM64 TU通过。

实际整编三态BASELINE exit2/21对象/0模块，MODIFIED exit2/24对象/0模块，ROLLBACK
exit2/21对象/0模块。原件、317归档文件恢复/重应用及KOUT五输出hash均核对。目标日志符号、
framebuffer/bridge_funcs完整类型、GPIO回调签名错误在本轮MODIFIED中消失，但模式状态、
MST/bridge/DSI/DSC等接口仍阻塞整个模块；不能把24对象或错误条数当完成百分比。

整合树独立运行10组checker全部exit0：此前native50/joint14/active21/PRIME31/fence9保持，
debug1320/GPIO64和6个真实公开头窄TU通过。debug真实完整consumer仍有其他旧API错误；
公开头六个完整原TU仍失败，仅目标86条类型诊断消失。以上不证明硬件或完整模块可用。
原四角色保留并扩展，整合证据out/kgsl-resume-20261111/sde-integration-10；角色路径/SHA统一
更新到FINAL-ROLE-PATHS.txt。无部署/重启/提交推送，默认发布pins和运行10档OPP未改。

mode审计已保存并恢复实际QTI builder源码请求链：6.18拒绝旧VID/CMD/SEAMLESS高位，
不能仅补常量。driver-owned requested/adjusted状态、配对用户态atomic请求、mode cache payload
所有权与完整DSI消费者仍须迁移。审计中的共享payload重复释放调用仅为实际语句/函数模型，
不是完整cache或硬件UAF证明。下一步并行推进真实mode入口/消费者、字符串API、late GEM
private-owner寿命；所有候选保持独立验证再合入。整体目标active，全部full_*边界保持。

## 2026-11-11：配对内核完成，SDE 第7组已集成并实际三态整编

本节覆盖下方“新内核仍在构建／PRIME尚未集成”的旧检查点。隔离dev2的Image、486内核模块、
KGSL58的60对象/3模块及489模块depmod已完成；Image 7e0d1129…，msm_kgsl 5ff1f2e3…。
这是编译候选，不是部署/真机验收；旧配对内核和默认发布pins未改，不重新启动已完成构建。

SDE目前7补丁/315源。PRIME之后合入真实公共framebuffer/fourcc头、删除无用户的ION头依赖、
实际MDSS DMA provider及active-list引用快照/回调锁/完整DMA span校验。native DMA与active-lock
是成对int回调API迁移，不能单独拿第6组当可用候选。local走DMA API，EXTBUF保留attachment映射
所有权；不借用后释放provider域、不重复map/unmap。secure/NRT及固件identity/splash仍显式不支持。

同一dev2 KOUT、相同完整模块命令实际三态：BASELINE exit2/2对象/0模块，MODIFIED
exit2/21对象/0模块，真实ROLLBACK exit2/2对象/0模块；317个归档文件逐hash恢复并精确重应用。
原315源和KOUT关键输出不变。整合树独立复跑native50、provider→VMA14、active21、PRIME31、
fence9均通过。旧PRIME模型缺三项新增公共primitive，初次编译失败保留；仅补模型overflow/SG
遍历定义，断言/产品不改，旧/回滚失败，新/重应用31通过。不是硬件DMA、TLB、退休或safe unbind证明。

共享工具scripts/check-sde-native-{dma,kms,secure,joint}.py和check-sde-active-lock.py已归档，
支持显式源码/内核路径，不依赖私有out目录。原四角色与失败证据保留；整合扩展为
out/kgsl-resume-20261111/sde-integration-7/{MODIFIED_FILE.tar.gz,DIFF_FILE.patch,VERIFICATION.txt,ROLLBACK.sh}。
完整编译仍失败：新暴露的日志category API、mode private_flags/SEAMLESS/vrefresh、MST/bridge/DSI等
需继续真实移植；错误条目数不是独立缺陷数或完成百分比。mode请求须兼顾原厂用户态，不能把6.18
已拒绝的旧flags硬塞回核心，或用永false/无人设置的属性冒充兼容。

下一步：独立debug-category候选和mode请求/状态所有权迁移，继续完整模块构建、KGSL真机提交及
SDE物理显示/联合fence/PM/长时/SQ1-SQ2验收；full_*不提升。目标active。未提交推送、未部署重启。

## 2026-11-11：DDR联合链与SDE PRIME／DRM核心后续状态

第58 KGSL完整构建结果保持。另完成同一loader表的实际源码联合链：32场景、9个编译后运行反例；
BASELINE/ROLLBACK均0绿32红exit1，MODIFIED32绿exit0。逐项核对39 DDR+8 CNOC HFI words、
七频default/min/max及280次hint，覆盖RPMh分配失败和ICC重试；不是硬件投票/Surface校准。
证据 `out/kgsl-resume-20261111/ddr-joint/JOINT-RESULT.json`，不改运行10档OPP。

实际DRM `drm_gem_plane_helper_prepare_fb`发现并修正两条引用泄漏：无FB先get显式fence后直接返回；
chain分配失败漏put尚未转移的new fence。原件/实际回滚3定点各LSan报1个24B模型fence；
新副本41场景和4个编译后运行负例通过，主代理独立重跑也通过，KERNEL/WRITE语义不变。
已归档 `configs/drm-618-plane-fence-ownership.json` 及精确patch，旧paired kernel未改。
新隔离 `/mnt/spx-workspace/chromebookOS/work/qualcomm-618/kgsl-sde-fence-20261111-build`
已真正启动Image/modules -j12；仅source helper和CONFIG_LOCALVERSION(-sq2-kgsl-sde-fence-dev2)变化。
记录时session27827/orchestrator354/make1096运行，已有原ashmem warning；尚未宣称构建完成。
续接必须复核 `drm-fence-chain/build/STATUS.json` 和真实进程/会话，不因观察超时重新开构建。

SDE PRIME九源候选未集成，独立审阅进行中：31 owner、9 fence/state、11 native fault场景及
10个编译后运行负例通过；BASELINE/ROLLBACK owner与fence exit1，MODIFIED exit0。
真实PRIME TU三态均exit2，完整候选v4也exit2/仅2对象/无模块；共享KMS/provider旧API仍须迁移。
import_aspace从attachment前保留到真实unmap/detach后，核心resv持锁入口不递归加锁；
不读foreign私有数据，不伪造secure属性，不丢必要fence。真实PFN/SG/resv公开API迁移已纳入。
core新副本另与实际SDE九场景及chain OOM联合通过，但旧paired kernel仍含旧helper，不能混称已修部署。
下一步先独立审阅合并此候选，再移植真实MDSS DMA provider；不把DMA映射和手动paging域混搭。

原四角色及所有扩展已重开；绝对路径和SHA统一在ignored `out/kgsl-resume-20261111/FINAL-ROLE-PATHS.txt`，
执行续接点为 `CURRENT.json`。未提交/推送、未部署/重启、不改发布pins；整体目标active、full_*仍未完成。

## 2026-11-11：原生 indexed DDR 加载器与高频来源修正

第58补丁 `kgsl-xiaomi-618-native-indexed-policy.patch` 已加入完整准备链；只改 `kgsl_bus.c`。
显式 `qcom,kgsl-bus-policy = "indexed"` 保留完整十进制表及每频率 default/min/max，
原生OPP仍决定频率/电压；非零average拒绝，缺selector保留原peak-only模式，坏selector不回退。
完整表/三值与ICC成功后才发布；启用10频率但只有7行政策明确失败，不插值、不填max、不删OPP。
同输入BASELINE/ROLLBACK均23绿73红exit1，MODIFIED96绿exit0；原99 bus场景三态全绿，
12个变异均编译成功后运行断言失败。新鲜58准备副本再次通过96+99；58组现已独立完成
60对象（59个-Werror编译单元+1链接对象）/3模块/MODPOST和489模块depmod，编译stderr空。
msm_kgsl SHA409318f2…；原内核91,246源、配对输出和56/57源/对象/模块保持。
主代理新58回归bus99、含实取固件SQE51通过；component/devres严格反例仍exit1，不宣称safe unbind。
测试含真实提取函数，但OF/OPP/ICC仍为模型，不证明硬件动态投票。

纠正下方历史“高三档缺失OEM表”的解释：590/661由项目提交277c0d08…添加，700由6337faea…
复制661并调角添加，均是项目实验档；原177..514七档的SDMSHRIKE V2来源完整，仍非Surface校准。
当前10个运行OPP保持。实际DTB SHA91625e29…，GPU `/soc@0/gpu@2c00000`，所有OPP无peak/avg带宽。
同boot只读取得cmd-db静态元数据（独立mount namespace，父mountinfo未变），没有部署、重启或采样。
已用实际输入重跑：RPMh59+DDR18、GMU OPP36、GPU OPP25通过；最后一项原checker本来可用，
fdtget支持省略unit-address，未把错误的路径失败假设写成产品缺陷，也未为此修改checker。
这些验证不证明真实投票/PLL/功耗。MSM的单OFF HFI占位表不构成已验证动态DDR路径。
Windows P007输出为原始性能状态遥测，不再称带宽；兼容旧JSON字段且数值/指令不变。

四角色扩展 `ddr-indexed/`、`reference-semantics/`、`reference-classification/` 均位于
ignored `out/kgsl-resume-20261111/`；原SQE角色保留。分类元数据三态1/0/1，仅验证证据分类，
不冒充驱动回归。SDE PRIME仍是独立未集成候选，完整SDE尚未编译通过。safe unbind、
动态OPP并发、完整fence/退休、设备显示/提交/PM及SQ1/SQ2验收未完成；整体目标active，full_*不提升。
子代理按用户指定 gpt-6.1-sol/high；共享KOUT单一持有者。默认发布pins、EFI、运行OPP不变。

### indexed 输入契约（未激活）

```text
NATIVE INDEXED DDR POLICY CONTRACT (unactivated loader capability)

Selector and authority
- Supported only for qcom,adreno-680.1. qcom,kgsl-bus-policy must be exactly one valid string, "indexed". Unknown/empty/multiple/unterminated values or read failures are rejected, never silently converted to peak-only. A selector on a non-native device is rejected with EOPNOTSUPP.
- Absence retains existing peak-only OPP bandwidth semantics (coalesced peaks, min=1, default=max=peak index); no automatic table-based fallback.
- Native OPP remains the sole frequency/voltage authority. The loader does not call the legacy voltage parser or change enabled frequencies, voltage_level, ACD, ordering, or default powerlevel. Indexed mode explicitly replaces OPP peak bandwidth as the bandwidth-policy input: OPP peak is not read or combined with the indexed policy. Nonzero OPP average floors remain unsupported and return EOPNOTSUPP. Static source AB=0 is not imposed on the existing runtime AB governor.

Indexed input
- qcom,bus-table-ddr is an array of u32 decimal kB/s, not MB/s, not divide-1024 units, not measured physical DDR throughput. Entry0 must be OFF=0; total count 2..MAX_GX_LEVELS inclusive (currently16); every following value must be strictly greater than its predecessor. The entire table is retained without rescaling/coalescing, including intermediate entries not selected as a default.
- Available qcom,gpu-pwrlevels container is mandatory. Each available child must have exactly one u32 cell for each qcom,gpu-freq (Hz), qcom,bus-freq (default index), qcom,bus-min and qcom,bus-max. Node/reg numbering and child order do not select the frequency; exact Hz is the key. Disabled child rows are outside the active policy; a disabled container is rejected. No speed-bin/DDR-type child fallback is attempted in this native mode.
- Exactly one available OFF row with gpu-freq=0 and all three indices0 is required. Each enabled nonzero OPP must have exactly one available policy row; missing/duplicate/extra active rows are rejected. Enabled OPP count must match the staged native powerlevel count; every staged frequency is strictly descending, nonzero, and independently found as an enabled exact OPP. A GPU performance table must also fit its OFF slot (active count<MAX_GX_LEVELS). No nearest-frequency match, interpolation, max substitution, or implicit OPP deletion is allowed.
- For every active row: 1<=min<=default<=max<table_count, and max<=the max of highest GPU frequency (pwrlevels[0]), matching the existing global clamp. No extra cross-frequency policy is inferred.

Transaction and existing consumers
- Probe-only loader: an already owned table/path/count returns EBUSY without touching the old owner. Table and full copied powerlevels remain temporary until ICC gfx-mem acquisition succeeds. Mode/OF/OPP/allocation/validation/ICC errors release temporary allocations and OF/OPP references and leave all previously published pwrctrl fields unchanged.
- Final successful publication is a probe-stage transaction, not a lock-free concurrent publication protocol. The enabled OPP set is assumed stable during probe; dynamic OPP enable/disable, overlay mutation, and runtime policy replacement concurrency are not proved.
- Runtime AB arithmetic, ICC failure dirty/retry handling and bootstrap rollback are unchanged. Bootstrap uses the selected default GPU powerlevel's actual bus_min, not table index1 or bus_freq. Existing RPMh cmd-db/BCMs/addresses/units/width/VCD code is byte-identical to parent.

Fixture and remaining target boundary
- Positive reference is the fixed SDMSHRIKE V2/A680.1 family source: 13 table entries, seven exact 177..514MHz active frequencies plus OFF; not Surface OEM DDR calibration. Fixture values come directly from configs/kgsl-sc8180x-downstream-policy-reference.json with its SHA recorded in each test RESULT.
- Separate synthetic boundaries test table counts2/16, u32 maximum, and independent OPP peak inputs. These are tests, not deployment values.
- Current project has ten enabled frequencies including experimental 590/661/700MHz additions. Seven policy rows with all ten enabled are deliberately rejected; this task neither removes those OPPs nor invents three bandwidth bindings. The higher three are unvalidated project additions, not an assertion of three missing OEM policy rows.
- No DT, release pins, deployed kernel, shared config, RPMh topology or device state is changed by this subtask. No hardware votes, performance, retirement or full safe unbind claimed.
```

# KGSL/SDE 并行续接：2026-11-11

## 当前增量：第57组输入停机、SDE首两组

第57项 `kgsl-xiaomi-618-input-worker-stop.patch` 在 `adreno_unbind` 的首次 remove/free 前
注销仅属于本设备的 input handler，清除 private，然后在 device mutex 外执行
`cancel_work_sync(input_work)`。无 CONFIG_INPUT 时仍排空已初始化 work；probe失败的空drvdata
不进入该操作。不是完整probe/minor发布修复，也未解决其他恢复/PM生产者或component/devres保留。
实际 input event/work/unbind 函数及注册/error尾提取测试26场景：BASELINE4绿22红 exit1，
MODIFIED26绿 exit0，ROLLBACK4绿22红 exit1；6个变异编译后运行失败。内核input同步、调度、
PM和provider为模型；父源与回滚SHA相同。57补丁/109源已独立准备，新的配对模块构建单独记录
在 `out/kgsl-resume-20261111/ordinary-fix/build/`，不得把56组链接自动算给57组。
主代理独立回归 input26、SQE51、HFI19、recovery26、genpd47通过；外层解绑严格模式仍失败exit1。

SDE正式准备器现在固定两组补丁/315源：

1. `sde-xiaomi-618-public-dp-link.patch`：移除旧DRM umbrella/display头依赖，新增driver-owned
   `dp_link_info`及真实AUX配置/电源事务；检查短传输/errno、合法legacy rate和lane、增强帧，
   RMW保留电源其他位，D0成功后保留1ms间隔。修改实际调用点及现代训练delay AUX参数。
   实际helper80场景、7个编译后运行反例、11项源码接线检查通过；baseline/rollback引用固定
   donor原helper并在同一case27短传输失败。该baseline不是未移植原驱动完整内核编译。
   支持率表型eDP/DP_LINK_RATE_SET不在首组，未知速率明确拒绝，不作静默舍入。
2. `sde-xiaomi-618-uapi-core-include.patch`：只将下游MSM UAPI的 `"drm.h"` 改为
   `<drm/drm.h>`，保留所有ABI定义。相同ARM64单TU输入，BASELINE/ROLLBACK均缺头exit2，
   MODIFIED exit0；原头另以同一真实6.18 core目录解析作参考，14个sizeof和11个ioctl常量
   与改件目标文件section逐字一致。仅结构大小/编码比较，不是用户态ioctl运行验收。
   首次夹具误把注释中的placeholder宏当定义导致编译失败，已保留并修正，不计作产品反例。

两组独立prepare逐hash核对后，全SDE严格构建仍exit2，仅2对象（含真实dp_link_helpers.o）。
旧UAPI缺头错误消失；ION、SMMU、手机Type-C/音频/secure/debug提供者和其余DRM API未完成。
没有用旧DRM核心头、假成功stub或禁用MODPOST来制造通过，未加载SDE。

新增测试入口：
```sh
python3 scripts/check-kgsl-input-worker.py --source CANDIDATE/drivers/gpu/msm/adreno.c --out FRESH_INPUT_CHECK
python3 scripts/check-sde-dp-link.py --source SDE/techpack/display/msm --kernel-source KERNEL --output FRESH_DP_CHECK --check-wiring
python3 scripts/check-sde-uapi.py --header SDE/include/uapi/drm/msm_drm.h --kernel-build KOUT --output FRESH_UAPI_CHECK
```

原SQE四角色保持，新增证据作为扩展，不替换历史结果：
`ordinary-fix/{MODIFIED_FILE.c,DIFF.patch,VERIFICATION.txt,ROLLBACK.sh}`；
`sde-port/transaction-v1/{MODIFIED.tar.gz,DIFF_FILE.patch,VERIFICATION.txt,ROLLBACK.sh}`；
`sde-uapi/{msm_drm.h,DIFF.patch,VERIFICATION.txt,ROLLBACK.sh}`，均在
`out/kgsl-resume-20261111/`。完整基线、改件、回滚/重应用及字面命令日志分别保留。
设备与发布pins未改，整体目标active；以下记录按阶段追溯。

## 当前结果及边界

- 用户要求切换、拉取并继续 `codex/kgsl-sde-port-v83`，随后明确允许 sub agent 并行。
- 本地及远端同步到 `690acd69ea98402aec01f3251d670b28516f0010`；原六个未跟踪救援工具文件保留。
- 三路独立任务：SQE 修复；component/devres 只读审计及反例；固定输入恢复和独立审查。
- 主代理串行管理设备连接、配置/文档集成。不得并行操作同一文件、构建目录、设备部署或重启。
- 整体目标“实现移植高通 Android 下游的 KGSL＋DRM/SDE 方案”保持 active；不是恢复另一条已暂停的 GPU v5 性能实验。
- 当前仅新主机源码候选；未提交推送、未部署、未重启、未修改发布 pins/EFI。full_* 不提升。

## 输入与可重建候选

固定小米 donor `7ede20c8692e0e0fc044754429e33cfd1d75c40d` 的426个清单文件逐 SHA 核对，
按原54补丁恢复109源，输出 `out/kgsl-resume-20261111/inputs/kgsl-618-v83`。
原 `SOURCE.json` SHA为 `9edd73c93b7cf9568758c86bc2a88a1348d4d23c27561182984d605361bce802`。
恢复不是复用未经核实的旧输出；输入、下载、准备及18项unittest的literal日志保存在 `inputs/`。

新第55项 `patches/kgsl-xiaomi-618-sqe-bounds.patch` 只改变 `drivers/gpu/msm/adreno.c`：

| 项目 | SHA256 |
|---|---|
| 原始 adreno.c | `59284830a6faee8d710b02f36823827049463184d9abc61ce359d0d5c8f58264` |
| 修改 adreno.c | `281dff7ccb065bc99a330b4f0149554e10c238073c7ce9e68e0f8900eca3ba16` |
| 精确 patch | `aec5bd48471dfd0b10ed1600ebe9a754593b1e18750910afc268d019a620ff03` |

新候选 `out/kgsl-resume-20261111/integration/kgsl-618-sqe-candidate` 由准备器执行完整55补丁生成；
其状态为 `source-host-tested-not-device-ready`。历史v83/native-kernel-v3整内核/模块链接结果
不能算作本候选编译通过。必须后续恢复匹配内核、所有消费者和符号表，再完整重编；不能借旧版本字符串混装。

## SQE 修复、测试与四角色

`get_ucode_version` / `adreno_get_firmware` 先校验普通头至少8字节、扩展头至少16字节及整dword，
再分配和复制；版本字段使用 `get_unaligned_le32`。合法payload仍跳过首4字节，逐字节保持，
request/alloc错误保持，拒绝输入不发布size/version。共享loader还影响A5xx PM4/PFP，
本轮没有真实A5xx固件或设备验收。未虚构最大固件大小、DDR参数或硬件执行能力。

最终同一51输入，GCC `-Werror` + ASan/UBSan：

| 运行 | literal stdout | exit |
|---|---|---|
| BASELINE | `SQE cases: 11 passed, 40 failed; compile=0` | 1 |
| MODIFIED | `SQE cases: 51 passed, 0 failed; compile=0` | 0 |
| ROLLBACK | `SQE cases: 11 passed, 40 failed; compile=0` | 1 |

回滚脚本实际用于单独的修改副本，恢复SHA与原件精确相等；patch重建也精确相等。
最终短文件使用精确 `malloc(size)`，标准5字节及扩展8/12字节等基线输入触发实际ASan越界；
4字节被模型零长度分配拒绝，不是直接ASan越界。40项红例含契约断言、UBSan等，不称全是ASan。
初版以40字节底层存储覆盖短文件，属于较弱夹具，原日志保留而不替代最终证据。

6个变异反例均编译成功后运行失败：移除最小长度、整dword、扩展头检查，漏release，错误复制偏移，错误版本发布。
独立审阅重跑51项通过；另42项独立精确长度对照为旧6绿/36红（14个实际ASan越界）、新42绿。
request/allocator/DMA等仍为模型，不能由此声称固件在GPU上运行或完整分配错误归属已验证。

四角色均位于 `out/kgsl-resume-20261111/sqe/`，后续扩展沿用这些角色：

1. `adreno.c` — MODIFIED_FILE。
2. `DIFF.patch` — DIFF_FILE，精确重建修改源。
3. `VERIFICATION.txt` — JSON可解析账本，含命令、输入、stdout/stderr、状态、hash和初版/最终记录。
4. `ROLLBACK.sh TARGET_COPY` — 可执行、嵌入原件的便携回滚，不依赖原源码仍在。

逐场景日志在 `baseline-check-final` / `modified-check-final` / `rollback-check-final`；
独立复核在 `inputs/sqe-review/`，变异反例在 `sqe/negative/`。固件字节仅ignored存储，不进入Git。

## 并行审计：仍未闭合的外层释放

实际调用链：

```text
adreno_remove -> component_master_del -> take_down_aggregate_device
  -> adreno_unbind -> adreno_unbind_gmu -> component_unbind_all
    -> component_unbind -> a6xx_gmu_unbind / a6xx_hwsched_unbind
      -> a6xx_gmu_remove(error)
    -> component bound=false + devres_release_group(component)
  -> kgsl components_bound=false
  -> devres_release_group(master) + aggregate bound=false
```

实际提取并执行六个callback/core函数，普通/HWSCHED × 成功/EUCLEAN四场景中，两个错误场景
仍各释放component和master devres一次、清除三个bound状态。HWSCHED dispatcher未继续close，
证明其局部错误检查有效，却无法阻止外层释放。严格 `--require-retention` 退出1：
`FAIL: cleanup error did not retain component/master devres ownership`。

这是未修问题的可执行证据，不是生命周期通过或实机UAF证明。remove返回值、devres释放和锁为模型；
完整 `adreno_unbind`、bind失败及driver-core remove路径目前仅源码审阅。Linux核心三文件与本地
6.18.60逐字匹配，但不因此宣称整个native-kernel-v3已经恢复。

证据为 `out/kgsl-resume-20261111/teardown/AUDIT.txt`、`run-final/RESULT.json`、
`run-final/VERIFICATION.txt`；生成器及首轮失败记录保留。不得通过void callback早退、
清错误、假完成、只加bound位或仅隐藏sysfs unbind来关闭这个问题。

已整理可提交的 `scripts/check-kgsl-component-teardown.py` 与
`tests/kgsl_component_teardown_test.c`；输入为准备后的KGSL源码和匹配核心源码。
默认严格模式实际退出1，显式 `--expect-gap` 模式实际退出0，仅表示再次复现两个已知反例；
两模式结果中的 `lifecycle_accepted` / `hardware_validated` 均为false。
两轮命令及总退出保存在 `teardown/TRACKED-CHECKER-RUNS.json`，不上传外部核心整文件。

下一阶段需要独立生命周期owner及两阶段停止/释放协议：阻止新提交与恢复/PM生产者，
实际停止并排空worker/HFI/IRQ/GPU/GMU、checked SMMU drain/root退休，再释放；失败保留完整
owner、模块/provider及资源寿命，覆盖bind失败和组件删除。`get_device`不等于保留devres。

## 主代理回归与设备观察

- v83实际父域45、GMU76、固件55场景通过（此55项没有捕获GMU固件输入，不与历史69项混算）。
- 精确hash匹配的provider源码顺序应用三补丁后，LPAE18和checked-drain32通过。
- 最初误选vanilla核心源时SHA检查拒绝，改用真正匹配的 `linux-6.18-sq2` 输入；未改hash pins。
- 配置集成初次用不存在的 `notes` 键在写入前失败；修正沿用 `host_validated`，错误保留。
- Windows `python` 未运行成功，改用已确认的WSL `python3`，不将前者计作通过。
- SSH扫描因Windows KEX兼容性失败，改用WSL扫描并与既有主机公钥逐字匹配，再严格验证连接。
- SSH处于initramfs视图，第一次直接 `/system/bin/getprop` 不存在；改为 `nsenter -t 1 -m -r` 后
  观察SF与audioserver running、boot_completed=1。首次批处理exit0不代表其中getprop成功。
- 本次仅只读核对和取回SQE，不改设备设置；MSM已加载、render节点存在，不能称KGSL已经运行。
- 私有主机地址/固件/原始日志留在ignored证据，默认pins、EFI、运行模块和原救援文件未更改。

所有本轮新源码、配置、文档尚未提交或推送。完整移植目标仍进行中。

## 2026-11-11：第56组 HFI worker 修复及新配对内核已编译

本节覆盖下方第55组“未新编译”的阶段记录；整体 KGSL＋DRM/SDE 目标仍 active。
子 agent 按用户指定统一使用 `gpt-6.1-sol` / `high`，保留独占写入与主代理串行集成边界。

第56项 `hfi-worker-lifecycle` 将 F2H cache 改为每实例，初始化两个链表并检查 task/cache/IRQ
创建结果；IRQ 申请最后进行，probe 失败先 join worker 再释放 cache。等待条件改为 stop 或有包，
修复 stop 唤醒后仍睡眠的问题。19 个实际函数/pthread 场景及7个编译后运行反例通过；
旧版及回滚均退出1（worker stop 超时），新版退出0。仅为 probe/worker 局部修复，未接入 live unbind，
IRQ/slab/内核调度仍为模型。component/master devres 错误释放的严格反例仍 exit1，不能称生命周期闭合。

独立复制本机 full-image 内核源和运行配置，应用全套提供者补丁，完整 Image＋486模块完成；
6个提供者对象 `-Werror` 重编。第55/56组各新鲜目录完成60对象/3模块严格编译与 MODPOST，
两套489模块 depmod 均无输出。新 release `6.18.60-sq2-kgsl-sqe-dev1`，未复用历史符号表；
第56组 `msm_kgsl.ko` SHA256 `a98eceec3635d4bc114885d59dcc04fe7b7ed58a8e741dcb8c3f5f908f435ad1`。
73个软链均留在复制树内；原源码保持。完整首编37条既有编译警告、2条KSU元数据提示及中间
5条.cmd NUL警告均保留；最终增量只有2条KSU提示，不能称全内核首编零警告。
外置 KGSL 必须使用本次真实 **6.18** 的 `drivers/devfreq/governor.h`；首次误用5.4头失败已保留。

原 Quest 2 4.19 的 DDR 输入来自 DT 政策表，经 MSM bus 拓扑/cmd-db 转成 RPMh TCS，
再由 HFI 发送整表、运行时选索引；Windows 并非必需。Kona/A650 表不能直接当作 SQ2/A680 校准，
cmd-db 也不替代频点对应的政策。DTS `/1024`、MSM bus `×1000` 换算差异已核对。
Windows Type25 仅写 MemoryFrequency 等频率字段，MemoryBandwidth 字段清零，不得把该频率当带宽。

SDE 已恢复固定313源并启动真实6.18 API移植；完整构建仍 exit2，未生成可用模块。
DP helper 第一组和UAPI include修复的单TU/主机通过不等于完整SDE或Surface eDP验收。
普通A680输入worker/独立owner、目标DDR、SDE其余API及provider、实际提交/显示/双机验证继续。
本轮没有部署、重启、修改默认pins/EFI、提交或推送；full_*仍未提升。
证据：`out/kgsl-resume-20261111/build/FINAL-BUILD-56-RESULT.json`、`teardown-owner/`、
`quest2-ddr/FINDINGS.md`；详情和沿用四角色见 `docs/KGSL-PARALLEL-20261111.md`。


## 2026-11-11：第57组配对编译及SDMSHRIKE V2带宽来源

当前57补丁/109源已完成新鲜配对编译：60对象（59个GCC命令均-Werror、1个链接对象）、3模块，
MODPOST无错误警告，489模块depmod无输出；`msm_kgsl.ko` SHA `a63381e52d818bfb43cd1e01097eed1d7fa13930a038f23b67a9f5ae3afb5583`。
只修 `adreno_unbind` 输入producer/work的停止顺序：同输入BASELINE/ROLLBACK4绿22红exit1，
MODIFIED26绿exit0。原内核源/配套输出/56源与模块未改变；初次59/60计数断言失败保留。
这不是完整safe unbind/硬件退休验收，外层component/devres严格反例仍失败。

DDR找到比旧graphics参考更完整的官方4.14.105 SDMSHRIKE V2/A680.1来源，固定commit
`04749e7fec2a8a5784a984d09f03fd0f2715fd1b`：13档十进制kB/s，7个177..514MHz档位明确
对应default/min/max，且GPU/LLCC/EBI通道宽度与SC8180X拓扑相符。仅落非激活参考
`configs/kgsl-sc8180x-downstream-policy-reference.json`；590/661/700MHz仍无来源，
不能降为7频点冒充完整目标，不能从Windows MemoryFrequency换算出缺失带宽。
当前native peak-only加载会覆盖原三值，后续需显式indexed-policy模式并精确匹配全部enabled OPP；
不复制源cmd-db地址、不插值、不把max当default。本轮未改变任何运行DT/OPP或发布pins。

SDE三组已独立prepare315源，第三组将DP PHY旧常量迁到现代定义和独立CP2/CP3名称；
2308实际函数场景及9个运行反例通过，保持原支持集合与MMIO顺序。完整header/getter TU
基线/回滚exit2、改件exit0，不等于dp_link.c完整TU。全模块仍未通过，
不是Surface eDP或联合显示验收；设备未部署/重启，目标active。
详细命令、回滚和未完成边界见 `docs/KGSL-PARALLEL-20261111.md`。


## SDE 第三组：DP PHY常量与真实调用链

`patches/sde-xiaomi-618-dp-phy-pattern.patch` 仅四源，将旧DP PHY test常量改用当前core定义；
CP2/CP3用driver-owned明确名称，数值取公开link-qualification编码，不把SEL_MASK冒充CP3。
保持原支持集合0/1/2/3/4/5/7、CP2=6拒绝、其它输入拒绝，不夹带协议放宽。
getter诊断名称随新符号改变；归一化语义trace比较不是旧日志字符串逐字不变声明。

实际getter/supported/parser/catalog/caller五函数2308场景通过（包括0..255输入与1536个caller组合），
固定donor宏仅用于旧源语义参考，其trace与改件逐字一致；9个反例均编译成功后运行失败。
BASELINE和ROLLBACK在当前core宏下host编译失败exit1，MODIFIED/REAPPLIED exit0；
额外旧源+固定donor宏参考则baseline/rollback均通过2308场景，未把缺宏失败当协议错误。
实际ARM64 Kbuild完整 `dp_link.h`/inline getter fixture BASELINE2/MODIFIED0/ROLLBACK2；
它不是完整 `dp_link.c` TU。候选全source严格构建仍exit2、2对象；旧PHY常量错误消失，
其余DRM/provider/ION错误保留，不声称模块链接通过。真实AUX/MMIO/屏幕未执行。

四角色扩展 `out/kgsl-resume-20261111/sde-phy/transaction-v1/`：
`MODIFIED.tar.gz`、`DIFF_FILE.patch`、`VERIFICATION.txt`、`ROLLBACK.sh`。
回滚与重应用在独立副本实际执行并比对全树hash；主代理正式3补丁/315源在integrated-3，
PHY2308及先前AUX80+接线独立重跑均0。最初function-like宏提取失败及变异编译失败保留，
修正后才计入最终运行反例；所有制品/原始日志仅ignored out，未复制旧DRM核心头进产品。
