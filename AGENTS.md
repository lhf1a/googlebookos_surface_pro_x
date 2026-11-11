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

## 2026-11-11：KGSL/SDE 续接允许 sub agent 并行开发

用户明确要求切换并更新 `codex/kgsl-sde-port-v83`，随后授权使用 sub agent 并行。
互不依赖的源码修复、只读生命周期审计、固定输入恢复与主机测试可并行；各代理使用独占
文件/输出目录，先声明写入范围，不同时修改同一配置、补丁清单或检查点。主代理负责
交叉审查、串行集成配置/文档、统一回归与收尾。设备访问由主代理串行管理；部署、
服务切换、重启及共享内核构建目录不得并行操作。并行测试不是硬件验收，也不放宽
真实退休、错误归属、配套内核/模块及私有资料边界。地址只作本次连接参数，不进入产品准入。
原有未跟踪的救援工具文件保留；不因续接此分支恢复另一条已暂停的 GPU v5 性能实验。

## 2026-11-11：并行续接，SQE 边界候选与解绑反例

已切换并同步 `codex/kgsl-sde-port-v83` 到 `690acd6`，用户授权 sub agent 并行。
固定 donor 426 文件逐 SHA 核对，重建 v83 的54补丁/109源；当前新候选追加第55项
`sqe-bounds`，只改变 `adreno.c` 的 `get_ucode_version` / `adreno_get_firmware`。
分配前验证普通版本头至少8字节、扩展头至少16字节及整dword，使用非对齐小端读取；
合法载荷仍跳过首4字节原样复制，错误不发布size/version。A5xx PM4/PFP共用该loader，
本轮仅含捕获的A680 SQE，不能外推A5xx硬件验收。

最终精确长度夹具51场景：旧版11绿/40红、新版51绿/0红、实际回滚11绿/40红，
回滚SHA等于原件；6个反例均编译后运行失败。另独立42项对照旧6绿/36红（14个实际ASan越界）、
新42绿；独立完整51项也通过。初版较大底层缓冲的记录保留，不称40个失败全是ASan越界。
重跑v83父域45、GMU76、固件55（未带捕获GMU文件）、LPAE18、provider32及准备器18通过。

实际callback/core六函数复现普通/HWSCHED两种错误路径：即使GMU remove保留资源或callback
提前return，component与master仍各释放一次devres并清bound。严格保留契约测试exit1，
这是未修问题，不是生命周期绿例；remove errno/devres/锁仍为模型，未证明实机UAF。
下一步须处理独立owner及两阶段停机/释放，包括bind失败回收，不能靠void callback返回值阻止core清理。

当前新候选为 `source-host-tested-not-device-ready`，**未新做完整内核/模块编译、未部署/重启**；
最近完整配套构建仍是历史v83/native-kernel-v3，不得把旧Module.symvers混入新候选。
本次SSH只读核对SF/audioserver running、Android boot_completed=1，运行的仍是MSM而非原生KGSL。
默认发布pins、EFI及原未跟踪救援文件保持；整体KGSL＋DRM/SDE目标active，full_*不提升。
完整命令/证据、四角色与并行边界见 `docs/KGSL-PARALLEL-20261111.md`。

## 2026-11-11：小米 KGSL v83 保留同步失败的中间页表

新增 smmu-walk-v1 提供者补丁。ARM LPAE 撤销非叶项时使用可返回错误的 walk 同步；失败恢复完整父项及非一致性 DMA 可见性，保留子树，只返回此前成功撤销的前缀长度。原有映射替换调用收到短撤销后拒绝覆盖。未启用的其他调用者保留原有回调行为；整个 domain/root 销毁仍未改为可检查同步。
第54项 checked-unmap 补丁使 GPU 父域和 GMU 域在发布映射前启用此能力；启用验证实际域/context、PM 和 checked drain，失败沿保留归属的路径退出。GMU 启用失败后先 detach 并核对实际域，再释放空域；仍附着则保留并锁存错误。提供者 map/unmap 的 PM 获取失败在修改页表前返回。
18个真实 ARM LPAE 子树/映射替换、32个提供者、76个GMU及45个父域场景通过；15个反例均编译成功后运行失败，另18项准备器检查通过。首个缺屏障反例因 mock 未使用而编译失败，已修正为禁用调用并验证运行断言，初始记录保留。
54补丁/109源独立复现，仅KGSL两个源改变。native-kernel-v3 完整 Image/486模块成功，SMMU两个对象和 ARM LPAE 对象均 -Werror；首编47条既有驱动/WSL时钟警告原样保留，最终增量零错误警告。KGSL60对象/3模块 -Werror/MODPOST零错误警告，489模块依赖无输出。91,246个内核源逐项核对，仅5个预期文件改变，native-kernel-v2保持。内部 io-pgtable/SMMU 布局改变，必须整套重编配对，不能按相同版本字符串混装。
**实际索引/撤销/递归释放代码已执行，但物理地址、DMA、MMIO、PM和锁仍为主机模型；没有硬件TLB或并发正确性证明。整个根/域销毁、完整GPU/GMU/HFI/worker/IRQ停机、错误保留恢复及component/devres解绑仍未闭合。** SQE边界、目标DDR校准、真实提交、KGSL+DPU、下游SDE/Surface eDP及双机验收继续待完成。本轮只读复核调试设备不锁屏/接电常亮已生效，未部署或重启；设备保持B0m。

## 2026-11-11：小米 KGSL v82 接通 GMU 撤销后的 checked TLB drain

新增独立 smmu-drain-v1 提供者：arm_smmu_flush_tlb_checked 校验实际 domain owner、AArch64 S1/context bank，持初始化锁与 runtime-PM 引用，在 cb_lock 下执行 PTE 写屏障、真实 ASID invalidation 和 Qualcomm checked 同步，返回 PM/超时错误；不改变根或替调用者停止设备。不支持的提供者返回 EOPNOTSUPP。
第53项 gmu-drain 补丁将真实接口接入普通 GMU 全局内存释放。只有撤销字节数精确匹配且 checked drain 成功才释放 DMA backing；失败锁存并保留对象/domain，不重试带洞区间。HWSCHED 成功导入设置 GMU_MAPPED，GPU 父域/共享释放和直接析构保留该对象；退出时逐项撤销并同步后才清标记，最后清导入表。a6xx_gmu_remove 改为返回错误，HWSCHED unbind 收到错误不再继续销毁 dispatcher。
22个实际提供者、74个GMU/导入/共享释放、47个genpd/remove、43个父域/全局和17个释放场景，13个编译后运行反例及18准备器检查通过。53补丁/109源独立复现；9源改变、100源保持。native-kernel-v2 完整 Image/486模块成功，SMMU两个对象 -Werror；KGSL完整60对象/3模块 -Werror/MODPOST 零错误警告，489模块依赖无输出。
内核首次构建45条原有驱动/WSL时钟警告，随后4条时钟警告，最终增量复核零错误警告；不称首次整内核严格零警告。91,245原有源逐项核对，仅3源改变、1头新增，native-kernel-v1保持。
**checked同步的源码链已接通，但MMIO/PM/锁仍为模型，不能据此宣称真实GPU/GMU停机、硬件TLB或安全unbind已验证。通用unmap中的页表页释放仍使用void回调，外层component/devres及错误保留恢复继续未闭合。** 本轮未访问/部署/重启设备。SQE边界、目标DDR校准、原生提交、KGSL+DPU、下游SDE/Surface eDP及双机验收继续待完成。

## 2026-11-11：小米 KGSL v81 保留映射失败的 GMU 内存归属

第52项 gmu-map-retain 补丁将失败保留接到两个实际分配调用方。a6xx_gmu_map_sg 对真实映射尝试的负值、零和非完整长度锁存错误，不再重复撤销不确定区间；输入校验失败仍正常释放。普通 DMA 分配把 backing、尝试地址和完整大小保留在 gmu_globals，推进 VA 游标但向调用者返回原错误；后续分配、固件解析/加载拒绝继续使用该实例。
HWSCHED 导入失败复用 KGSL_MEMDESC_MAP_UNCERTAIN，保留 mem_alloc_entry、GPU 全局对象与尝试的 GMU 地址。process_mem_alloc 不发布成功句柄；已有共享内存释放入口拒绝不确定对象，保留地址和 backing。纯校验失败的未使用 GPU 对象仍由 device->globals 持有至正常清理，不宣称即时回收。
69个实际 GMU/导入/共享释放场景、69个固件场景（含捕获 A680 字节比对）、17释放和42 genpd/remove 回归、11编译后运行反例与9准备器通过。52补丁/109源重复准备一致，仅两个源改变；60对象/3模块 -Werror/MODPOST 零错误警告，489模块依赖无输出。
**这是分配/导入错误归属保留，尚无不确定映射的安全回收协议。IOMMU/DMA/PM/全局分配仍为模型；并未证明真实TLB排空、GPU退休或外层devres安全解绑。** 未部署/重启，设备保持B0m。SQE边界、目标DDR校准、真实KGSL提交/退休、KGSL+DPU、下游SDE/Surface eDP及SQ1/SQ2完整验收继续待完成。

## 2026-11-11：小米 KGSL v80 保留未完整撤销的 GMU 全局内存

第51项 gmu-unmap-errors 补丁让 a6xx_free_gmu_globals 返回真实清理结果，逐段核对 iommu_unmap 撤销长度后才释放 DMA backing。长度不符锁存 EUCLEAN，保留当前及后续对象、global_entries 和 domain；已有错误或对象缺失domain时停止清理，不对带洞区间自动重试。此前已完整释放的前缀槽位保持清零。
remove 在释放 QMP、固件、ITCM shadow 或 genpd 前检查全局清理结果；失败立即保留这些资源。成功释放后清空 fw_image/itcm_shadow 指针，重复 remove 不再重复释放它们。42个真实 genpd/全局释放/remove 场景（新增8个）与58个GMU回归通过；10反例均编译后被断言拦截。
51补丁/109源复现一致，仅GMU源改变；完整60对象/3模块 -Werror/MODPOST 零错误警告，489模块依赖与9准备器通过。**撤销长度相符不等于已证明TLB排空或硬件退休，provider/PM/DMA仍为模型，外层devres解绑仍未安全闭合。** 未部署或重启，设备保持B0m。
同时核对匹配6.18源码：iommu_map_sg 在错误回滚中忽略 iommu_unmap 的长度。因此负返回值不足以证明 backing 已安全脱离。当前 reserve_gmu_kernel_block 的映射失败释放和 HWSCHED 导入失败的归属仍未修复，已明确列为下一步；本轮只修复显式全局释放路径，未把映射失败风险标为完成。SQE边界、DDR校准、真实KGSL提交/退休、KGSL+DPU及下游SDE/eDP双机验收继续待完成。

## 2026-11-11：小米 KGSL v79 将 GMU VMA 游标归属到各实例

第50项 gmu-vma-owner 补丁把 GMU 的 VMA 指针改为内嵌数组，两个家族模板改为 const 且显式覆盖 GMU_MEM_TYPE_MAX（含不支持区间的零槽位）。分配和 HWSCHED 导入仍更新所属 GMU 的 next_va，不再改变全局模板或另一实例的游标。
probe 在修改 pdev 和请求任何 provider 资源之前初始化 VMA；存在 domain、global对象、fw引用、preallocation、原生genpd、时钟/电源引用、power_error 或 GMU_ENABLED 时返回 EBUSY，保持原对象和游标。这只是重复初始化的防覆盖检查，不能代替安全解绑/硬件退休协议；旧释放流程残留的状态不会被偷偷清零。
67场景通过（13个新增实例隔离/家族/保留对象场景＋54个固件回归），14个反例均编译后断言失败。初版“少复制最后槽位”未被全零夹具发现；已改为非零初始存储，最终完整检查通过并保留初版缺口记录。
50补丁/109源复现一致，GMU源和头两文件改变；完整60对象/3模块 -Werror/MODPOST 零错误警告，489模块依赖与9项准备器通过。**内部KGSL对象布局已改变，必须使用整套重编模块；provider ABI不变。测试执行初始化/固件函数，完整probe接线只做源码检查，未验证内核并发或真实GPU。** 部分map/unmap失败归属、safe unbind、SQE边界、目标DDR校准、真实提交/退休、KGSL+DPU和下游SDE/eDP仍未完成。未部署或重启，设备保持B0m。

## 2026-11-11：小米 KGSL v78 校验完整 GMU 固件后再分配/写入

第49项 firmware-bounds 补丁新增共享块解码与完整检查：使用 size_t/剩余长度检查头和载荷，局部复制并按 little-endian 解码以支持非对齐头；VMA/已分配区域的终点比较改为 u64，拒绝空区间与回绕造成的错误命中。
解析阶段先验证完整文件的结构、VMA、TCM地址对齐和预分配页扩展范围，再处理元数据；加载阶段在任何载荷拷贝前检查全部非TCM段的有效 backing。遵守元数据在载荷前的来源约定，保留未知版本元数据及现有 A680 载荷的非零 type 字段。旧 raw 固件分支检查空值/最大长度，按 raw 数据处理。
新增检查器提取真实解析/加载函数、VMA与结构定义。54场景通过 ASan/UBSan，包含实际 A680 文件的6个 backing 分配、9段完整载荷逐字节比对、重复加载、6个分配失败和重试，以及截断/回绕/缺映射。12反例均编译后被断言或UBSan拦截；两个顺序反例将检查移到分配/拷贝之后，确认测试观察到提前副作用。
49补丁/109源复现一致，仅 GMU 源改变；60对象/3模块 -Werror/MODPOST 零错误警告，489模块依赖和9项准备器通过。**分配/DMA/MMIO/固件请求仍为模型，未证明硬件执行或IOMMU失败回收；部分分配失败仍保留原有对象，不宣称完整回滚。** 静态VMA归属、SQE边界、目标DDR校准、真实提交/退休、KGSL+DPU及下游SDE/eDP双机验收仍未完成。未部署或重启，设备保持B0m。

## 2026-11-11：小米 KGSL v77 修正 A680 固件请求路径

实机只提供 `qcom/a680_gmu.bin` 和 `qcom/a680_sqe.fw`，匹配的 MSM catalog 也使用这两个 A680 文件；KGSL 仍请求旧的 a640/a630 名称。第48项 native-firmware 补丁只把 A680 descriptor 的两条请求改为现有 `qcom/a680_*` 路径，保留其他 GPU 定义和 ZAP 的独立生命周期。
从调试机只读采集固件到 ignored out，并与远端 SHA256 比对。离线检查20个 GMU 块（11个预分配元数据、9段载荷），6个非TCM页对齐分配区间无重叠且覆盖相应载荷；SQE 的32452字节载荷按双方 loader 相同的跳过4字节头规则检查。**这是实际文件的路径/边界/布局证据，未执行原生 KGSL loader 或 GMU/SQE 硬件，不能称为启动成功。** 数据块 type 字段为已有固件的非零值，不能擅自按 DATA=0 拒绝这些块。
48补丁/109源独立准备一致，仅 adreno-gpulist.h 改变；60对象/3模块 -Werror 与 MODPOST 零错误警告，489模块依赖检查通过，准备器9项通过。没有因两条名称变化新增模拟功能测试；v76 的 RPMh 函数和测试输入保持，既有验证仅按相同源码复用。
原生 DDR 带宽仍缺校准；固件截断/边界检查、静态 VMA 生命周期、部分分配失败回收与完整 KGSL 提交/退休仍待完成。KGSL+DPU、下游 SDE/Surface eDP、性能/PM/双机验收均未完成。本候选未部署或重启，设备保持 B0m。

## 2026-11-11：小米 KGSL v76 接入有来源的原生 CNOC OFF/ON 输入

第47项 native-cnoc 补丁移除不存在的 `qcom,bus-table-cnoc` 属性依赖，使用固定 SDMSHRIKE/A680 下游来源的 OFF=0、ON=100 输入（KGSL 消费单位 kBps）。SH2/SN15/SN3/CN0 拓扑保持；地址、unit、width、VCD 仍读取目标运行时 cmd-db，未复制来源 DT 的资源或 DDR/频点表。
来源固定为 graphics-devicetree 提交 b8c849e838ca1ce51342ececd79dc8e8dc46611b 的 gpu/sdmshrike-gpu.dtsi，配置记录 SHA256。来源是 A680 revision0，目标为 A680.1，故只作为家族输入依据，不能声称已完成 SQ1/SQ2 硬件校准。
59项 RPMh、18项 DDR 解析、9个编译后运行反例和9项准备器通过；47补丁/109源独立准备一致，仅 RPMh 源改变。60对象/3模块通过 -Werror 编译与 MODPOST，489模块依赖检查通过。测试覆盖目标 OFF/ON 命令、运行时地址/unit 变化、CNOC 元数据错误、全部分配失败及 HFI 消息失败时不变。
**CNOC 输入已接通，硬件投票未验证。当前 GPU OPP 仍没有 DDR 峰值带宽，不能从主线占位 OFF 表、手机表或 SQ1 ACPI 未确认单位直接补值。原生 probe/submit、根/回放回收、KGSL+DPU、下游 SDE/Surface eDP 与双机验收仍未完成。** 本候选未部署；调试机保持 B0m，常亮/无锁屏偏好已另行实机复核。

## 2026-11-11：SQ1原始ACPI带宽证据已可重复提取（KGSL仍v75）

新增`inspect-sq1-gpu-bandwidth-reference.py`，固定SQ1存档DSDT和Windows GPU驱动SHA，校验ACPI长度/校验和，以ACPICA离线执行原始AML的PMCL，只用SSDT包装读取graphics两张表。实际选中P004/P007，完整返回值与反编译的原始Package逐项一致；SOID=0x154、SIDV=0x20001、SIDT=1仅是该存档的静态身份，不代表当前SQ2。
P004含9个频率状态（含OFF），P007含11个带宽状态（含OFF），不能按行配对生成GPU OPP。P007非零原始数值1198000000～8369000000；**单位尚未证实，不将其直接标为GB/s或写入DT。** 固定Windows指令证明查询结果还乘`((device_u32_at_0x46c4 >> 14) & 31) * 2`；该字段会被其他路径更新，运行时因子/物理含义及HFI投票路径未闭合。
10项解析/边界/哈希拒绝测试通过；两次独立执行的语义结果一致。完整DSDT离线加载有3条既有BAUD buffer-field初始化错误，保留原日志且明确列入结果；BWQR/PMCL返回成功，不把这称为整个ACPI无错误。STA/INI禁用，无设备访问。
本轮是目标带宽依据的进展，未改KGSL补丁/内核/DT、未部署重启，不新建v76候选。v75仍缺已校准的GPU带宽与CNOC输入；首次KGSL执行、生命周期收束、KGSL+DPU、下游SDE/Surface eDP及SQ1/SQ2验收继续待完成。

## 2026-11-11：小米 KGSL v75 接入原生OPP带宽输入并补齐SH2

第46项native-bandwidth补丁让原生A680.1的DDR表按实际GPU频率精确查询OPP peak kBps，不再读取手机DDR表。保留OFF项、合并相同带宽、校验频率/带宽顺序、u32/HFI容量与gfx-mem索引；缺带宽返回ENODATA，显式average floor尚未接入governor则返回EOPNOTSUPP，不静默忽略。
在ICC路径成功获取后才发布各GPU频点的bus索引，默认/最大档取该OPP峰值、最低档取表中最小非零值。OPP引用逐项归还，查找/分配/ICC失败不改变原频点bus索引。
沿匹配内核真实ICC图核对GPU→GEM_NOC_SNOC→SNOC_CNOC→GPUSS_CFG，修复原CNOC表遗漏SH2；现在含SH2/SN15/SN3/CN0，地址/unit/width/VCD继续读运行时cmd-db，不复制手机地址。
99实际bus场景、51 RPMh＋18 DDR解析场景、7编译后运行反例和9准备器通过。46补丁/109源重复，2源改变；60对象/3模块-Werror零错误警告，489模块depmod无输出。
**这只接通带宽输入与修正拓扑。捕获目标的10个GPU OPP均无peak带宽，CNOC输入仍要求当前DT没有的qcom,bus-table-cnoc；原生probe仍不能通过。测试带宽值为合成输入，cmd-db为真实捕获，未证明硬件投票/性能。** 未部署/重启；真实带宽依据、固件/执行、根/回放回收、KGSL+DPU、SDE/eDP和双机验收仍待完成。

## 2026-11-11：小米 KGSL v74 修复空闲重试与解锁后的关机竞态

第45项idle-retry补丁让普通GMU idle worker检查关机返回值：仅仍启动且无power_error/transition_error的GPU重新安排空闲定时器；已有致命错误时不再运行调频或重新排定时器。
adreno_dispatcher_idle等待期间会释放device->mutex。a6xx_power_off在重新持锁后、OOB请求前重查新增故障、GPU_STARTED和active_cnt；其他线程已关机则直接结束，出现新活跃引用则返回EBUSY，不继续断电。
PM suspend遇到仍运行且无锁存错误的早期空闲失败，恢复dispatcher提交门；低层关机/默认根/记录失败继续保留关闭的门与引用，不发布PM_SUSPEND。未新增自动清除错误或未经停机证明的回收。
84实际关机/idle worker/PM/恢复场景、10编译后运行反例、21硬重置回归和9准备器通过。45补丁/109源重复，只有GMU源改变；60对象/3模块-Werror编译及489模块depmod通过。首次全编译有2条WSL时间偏移警告，日志保留，最终增量复核无错误警告。
**解锁交错/OOB/定时器/引用和硬件为模型，并非内核并发或物理退休证明。soft/HWSCHED/fatal恢复、submitted页表/回放回收、正常SMMU runtime-PM和safe unbind未完成。** 未部署/重启，B0m保持；原生带宽/固件/执行、KGSL+DPU、SDE/eDP与SQ1/SQ2验收继续推进。

## 2026-11-11：小米 KGSL v73 接通正常休眠后的context引用回收

第44项slumber-retire补丁让普通A6xx正常休眠与硬重置共用回收顺序：checked完整停机后清GPU_STARTED，恢复默认页表、清抢占记录，再回收switch/active引用。默认根或记录恢复失败保留引用，锁存首个power_error/transition_error，阻止误唤醒。
正常关机开始检查adreno_suspend_context返回值，空闲失败不继续OOB/断电；原OOB失败但底层停机成功的返回语义保留。回收失败不发布PM_SUSPEND，不继续dispatcher/ringbuffer/LLCC清理。
63实际关机/PM/恢复场景、8编译后运行反例、21硬重置/30 switch/36默认根/47 dispatcher回归与9准备器通过。44补丁/109源重复准备一致，2源改变、107源保持；60对象/3模块-Werror零错误警告，489模块depmod无输出。
**停机/provider/CP/引用/锁仍为模型，未证明硬件退休或缓存可见性。idle worker尚未针对早期空闲失败重排定时器；soft reset/HWSCHED/fatal恢复、submitted页表和回放回收、安全unbind仍待完成。** 未部署或重启；B0m保持，原生执行、KGSL+DPU、SDE/Surface eDP及双机验收未完成。

## 2026-11-11：小米 KGSL v72 接通硬重置后的context切换引用回收

第43项hard-reset-retire补丁在普通A6xx GMU restart中串联checked GMU/GPU shutdown、checked默认根恢复、CPU抢占记录清理，然后回收旧switch引用/active引用，最后重启。任一步失败停止后续阶段，未解除submitted/started页表保留，也不清除既有transition_error。
记录清理先校验所有CPU映射/范围，非secure路径清除用户保存地址scratch、整个非secure CP保存记录和ringbuffer页表记录，重建默认TTBR0/contextidr及固定CP头，清wptr/next/prev，停止preemption timer。禁止把未实现的secure记录重写当成功。
回收区分尚有callback与callback已完成/注册失败：前者标记stopped、取消事件后由callback最终释放；后者摘到局部列表解锁后put。callback在锁内决定是否释放，解锁后不重新读取可能被另一条回收路径释放的record。
21实际硬重置/记录场景、30实际switch/event归属场景、12编译后运行反例、36默认根/47 dispatcher/47电源/55关机恢复回归及9准备器通过；43补丁/109源重复，5源改变、104源保持。60对象/3模块-Werror零错误/警告，489模块depmod无输出。
**这是普通A6xx硬重置路径的源码回收实现，硬件停机/provider/CP/引用仍为模型，未实机验证。普通休眠、soft reset、HWSCHED、已锁存transition_error后的恢复入口、保留回放队列及submitted页表回收仍未闭合，强制unbind/devres仍不安全。** 未访问/部署/重启设备；B0m保持，原生执行、KGSL+DPU、SDE/eDP和双机验收仍待完成。

## 2026-11-11：小米 KGSL v71 保留context切换取消/注册失败的引用

第42项context-switch补丁在提交切换命令前分配旧context引用记录；提交成功后先让新context成为active，再把旧引用交给ringbuffer的context_switches列表。只有KGSL_EVENT_RETIRED回调摘链并put，CANCELLED或未知结果保留记录和引用，不冒充GPU完成。
事件注册失败发生在提交之后，因此保留新旧引用并锁存transition_error，返回原errno；后续切换先检查该错误。提交前的分配/引用/命令失败正常回收新增临时资源，旧active保持。ringbuffer重新初始化拒绝覆盖非空引用列表。
23实际switch/set_pt/event/ringbuffer-init场景、10编译后运行反例、36默认根/16 fence/47 dispatcher回归和9准备器通过；42补丁/109源重复一致，3源改变、106源保持。60对象/3模块-Werror零错误/警告，489模块depmod无输出。
**取消/注册失败现在有可追踪的引用归属，但尚无安全回收流程。CP提交、锁、引用、workqueue为模型；新列表仍须接入真实GPU/CP/preemption停止后的回收，强制close/devres和started/submitted根退休未完成。** 未访问/部署/重启设备，B0m保持；原生执行、KGSL+DPU、SDE/Surface eDP和双机验收继续待完成。

## 2026-11-11：小米 KGSL v70 修正默认根恢复与旧 context 释放顺序

第41项reset-context补丁让普通启动、GMU/RGMU启动在kgsl_mmu_start成功恢复默认根之后，再释放旧active context和更新ringbuffer全局状态；soft reset同样在checked默认根恢复之后释放。失败保留旧引用，避免恢复失败时先丢归属。
_soft_reset改为返回SP/TP regulator_enable的真实错误，普通启动和soft-reset两个调用者均检查并停止后续启动；共享context清理先清软件指针和页表元数据，再put引用，避免析构时仍暴露旧指针。
36实际默认根/reset/context/boot场景、9编译后断言反例、47 dispatcher/47电源回归和9准备器通过。41补丁/109源重复一致，3源改变、106源保持；60对象/3模块-Werror零错误/警告，489模块depmod无输出。
**这是启动/恢复失败的引用顺序修复，不是GPU退休证明。reset寄存器、SMMU/PM和引用叶节点仍为模型；未解除started/submitted根保留，未修复preemption record、context-switch事件取消/注册失败、强制close/devres或完整回收协议。** 未部署/重启，B0m保持；原生提交、KGSL+DPU、SDE/Surface eDP及双机验收继续待完成。

## 2026-11-11：小米 KGSL v69 保留普通dispatcher回放失败归属

第40项dispatcher-replay补丁将未重新提交的命令保存在队列retained_replay中；成功sendcmd后由在途队列接管并清空对应replay槽，失败保留其余槽。检查head/tail、槽指针和计数；分配失败保持原队列，不再失效/销毁context。已有保留owner拒绝覆盖。
恢复主流程收到错误立即停止其余队列回放，恢复原halt计数，锁存错误并停止定时器；已发布命令继续由在途队列持有，故障不会伪装成完成。adreno_stop也检查clear_pending_transactions返回值，失败时不继续清理ringbuffer、active context或STARTED状态。
47实际dispatcher/replay/send/stop/wait场景、9运行反例、47电源/20发布回归和9准备器通过；40补丁/109源重复一致，3源改变、106源保持。完整60对象/3模块-Werror零错误/警告，489模块depmod无输出。
**这是失败归属保留，不是完整恢复/回收。普通dispatcher的ENOSPC等回放失败目前同样锁存，保留数组尚无安全回收协议；强制close/component/devres解绑仍不安全。真实GPU/CP/preemption/SMMU退休、正常PM、原生提交及SDE/eDP未验证。** 本轮未访问/部署/重启设备，B0m保持，完整目标继续进行。

## 2026-11-11：小米 KGSL v68 修复HWSCHED重启与重放归属

第39项hwsched-recovery补丁删除reset前force_retire_timestamp，加入RESETTING状态；restart成功且未被新fault覆盖才初始化重放。inactive/未启动GPU不能当作已完成重启；失败保留旧命令、fault引用及计数并锁存错误。
故障策略要求的context失效移到成功重启之后、任何重放之前。提交成功才设置REPLAY位和增加inflight；已重放的命令不能仅因后来detach/invalid就销毁，必须等实际EOP。旧队列故障命令取消对应事件，不制造完成时间戳。
ENOSPC且有已发布在途命令时保留待提交项，靠完成通知继续重放；无进展来源或其他错误保留全部不确定owner并阻止继续提交。重复fault通知只取一次额外引用，退休时归还；profiling映射失败也不再漏掉已退休命令的释放。
26实际函数场景、11编译后运行反例、16 fence/20发布回归、9准备器通过；39补丁/109源重复，3源改变、106源保持。完整60对象/3模块-Werror零错误/警告，489模块depmod无输出。
**这些是恢复控制流与归属的源码/主机证明，硬件reset/HFI/时间戳/引用/锁仍为模型。真实GPU/CP/preemption/SMMU退休、context unregister ACK、halt引用、错误后恢复和safe unbind/PM未完成；普通dispatcher的回放失败/强制close仍待修复。** 未访问/部署/重启设备，B0m保持；原生提交、KGSL+DPU、SDE/Surface eDP及SQ1/SQ2验收继续待完成。

## 2026-11-11：小米 KGSL v67 区分 fence 取消与真实完成

第38项fence-result补丁让事件持有具体fence引用，仅回传该事件的完成/取消结果；取消先设置-ECANCELED再signal，未知结果为-EIO，不再推进整条timeline或提前完成其他fence。错误ioctl释放与异步回调之间的引用也已接通。
移除context invalidate和wait_rb中的合成SOP/EOP写入；真实已退休事件可成功，未退休事件取消。事件注册在group锁下检查terminal context，避免与flush交错后挂入永不处理的事件。
16实际函数场景、8编译后运行反例、20发布/IB回归及9准备器检查通过；38补丁/109源重复一致，5源改变、104源保持。60对象/3模块-Werror链接成功，489模块depmod无输出。
补齐TZ governor模块描述；**更正历史v64“零警告”：原compile.log存在一条大写WARNING的MODULE_DESCRIPTION警告，旧结果统计漏记。** v67无编译器/MODPOST警告，首次完整构建有WSL时钟偏移警告，保留原日志；随后增量复核零错误/警告。
**错误结果不等于已停止GPU。HWSCHED仍有reset前force_retire_timestamp，强制close/回放失败/context teardown/内存释放回调、嵌套IB/数据缓冲、根退休/回收及safe unbind/PM继续未闭合。** 本轮未访问/部署/重启设备，B0m保持；KGSL硬件提交和SDE/eDP目标仍进行中。

## 2026-11-11：小米 KGSL v64 接通映射发布与顶层IB引用

第37项map-publication补丁增加独立published_gpuaddr：attach/SVM映射完整成功后release发布，unmap前撤销；地址查找、两种info和profiling读取已发布状态，ID查找仍允许未映射SVM对象进入mmap。
dispatcher/HWSCHED均检查IB大小和地址dword对齐、进程范围及实际已映射对象边界；顶层IB持有entry引用，重复验证不重复取引用，命令对象销毁通过现有deferred put归还，两个mem-node分配点清零新指针。
20实际函数场景（含3个pthread阻塞map交错）、8编译后运行反例、20释放/39 SVM回归及9准备器通过；37补丁/109源重复一致，7源改变、102源保持。
完整60对象/3模块-Werror零错误警告，489模块depmod无输出；msm_kgsl SHA 71b0be98cf0bec07441e54f529f90953d449ec491b00950eb32c645040badc5e，内核和两个governor保持。
**这是映射发布及顶层IB到命令对象的引用归属，不是GPU退休证明。嵌套IB/其他数据缓冲、强制/错误清队列和context teardown是否已停GPU、页表退休/保留对象回收、正常PM与safe unbind仍未闭合。主机交错不是弱内存模型或实机压力验证。**
本轮未访问/部署/重启设备，B0m保持；实际带宽/固件/首次提交、SDE/Surface eDP和完整SQ1/SQ2验收继续待完成。

## 2026-11-11：小米 KGSL v63 将SVM绑定推迟到实际mmap，收紧失败归属

第36项svm-mmap补丁使get_unmapped_area只选择地址，不保留GPU VA或建立映射；CPU/GPU候选地址必须精确一致，修正VMA exclusive end并限制搜索到当前TASK_SIZE，避免减1下溢。
GPU绑定改在实际mmap回调的最后一个可失败步骤，排在核心准入和所有CPU页插入之后；检查vm_insert_page错误及页数组边界，成功后才发布VMA私有数据/回调和映射计数。
实际mmap重新lookup后再检查release_error、安全内存和SVM对齐，防止地址选择后对象状态变化绕过检查。多VMA仅第一次增加mapped字节；失败由核心撤CPU PTE，清洁GPU失败释放VA，不确定回滚保留entry/页表/后备内存并撤ID。
39个实际KGSL/核心mmap包装场景、10个编译后运行反例、19释放与25模块接口回归及9准备器检查通过；36补丁/109源可重复准备，仅kgsl.c改变、108源保持。
匹配native-kernel-v1下完整60对象和3模块-Werror零错误/警告，489模块depmod无输出；新msm_kgsl SHA为777cc28e59d123df3e5b627280e2f03bae4a8eaa137219334c428f9c7b19d9b6。两个governor/内核保持v61产物。
**主机模型不证明实际MMU/PTE、并发ioctl/提交或compat/fork/split行为。现有_gpu_set_svm_region仍在map完成前发布gpuaddr；后续须审计map-ready发布与跨调用串行化、根退休、保留对象回收、安全解绑及正常PM。**
未部署/访问/重启设备，B0m保持。DDR/CNOC、首次KGSL提交、下游SDE/Surface eDP及双机/长期验收仍未完成，完整目标保持进行中。

## 2026-11-11：小米 KGSL v61 首次完整模块链接，原生提供者全内核构建通过

第35项module-api补丁导入DMA_BUF命名空间；memstore使用已导出的mm_get_unmapped_area，保留外层mmap地址/对齐/LSM检查。
工作线程用sched_setattr_nocheck保留FIFO优先级2，设置成功后才唤醒；创建/调度错误返回原errno，失败停止线程，core退出先drain/stop并清指针。**仍要求设备先停止所有生产者，不证明安全外层解绑。**
修复三个格式警告及TZ governor缺少的显式OF头；35补丁/109源可重复准备，六源改变、103源保持。25实际helper/核心mmap包装场景、8编译后运行反例、9准备器检查通过。
独立native-kernel-v1整合SMMU-v3、SCM-DCVS-v1、clock-v1：完整Image及486模块成功，含回退MSM；版本6.18.60-sq2-kgsl-dev1。91,245个普通源文件比对，仅10个固定提供者文件改变。
KGSL使用真实新内核头及Module.symvers，无强制提供者头：全部60对象（含链接/模块元数据）及msm_kgsl、两个governor模块以-Werror零错误/警告链接。489模块depmod无缺失符号/警告。
初次内核全编译保留37条已有KernelSU/ashmem等警告及WSL时间戳警告；后续最终增量复核零错误/警告，**不称整个内核首次严格零警告或二进制可重复构建**。
状态为source-linked-not-device-ready：仍缺实际GPU启动/提交、SVM精确地址和失败回收、根退休/保留对象回收、安全解绑、正常runtime-PM交接及DDR/CNOC带宽。SDE/Surface eDP和SQ1/SQ2验收未完成。
本轮未部署/重启或访问设备；运行B0m保持。禁止把新KGSL/私有SMMU接口与旧内核/MSM二进制混装。

## 2026-11-11：小米 KGSL v59 接入原生父域，完整源码进入MODPOST

第34项parent-domain补丁将paging domain附加到实际GPU `pdev->dev`，而非KGSL字符设备；从GPU iommus引用找到真实SMMU，持有其device/runtime-PM引用。
使用提供者返回的TTBR1配置与真实context bank，建立独立空默认TTBR0并保持其寿命；进程表仍是独立根。移除手机子节点/domain属性/私有SMMU MMIO和重复时钟控制。
secure/LPAC未支持则明确拒绝；目标捕获DT不存在的手机qtimer/QDSS属性保持不可用，不伪造寄存器地址。原生IOMMU完整对象首次编译通过。
全局map返回值接到三个分配入口，失败保留不确定映射及物理内存；检查完整SG长度、guard只读、父域unmap与checked TLB drain。全局位图覆盖完整footprint/padding并修复满区随机取模边界。
默认根安装/禁用/销毁或detach失败保留归属；初始化失败不再无条件释放外层PT，retained_dev保存准确设备引用。子表或启动暴露的根阻止父域关闭，且拒绝发生在全局unmap之前。
41父域/probe/global、39后端＋31进程/父映射、28获取检查、14运行反例和9准备器检查通过；18默认根及28故障检查的实际函数与v58逐个哈希保持。
34补丁/109源重复准备一致；7个ARM64对象-Werror零警告。完整56对象/0源码编译错误，首次进入MODPOST后仍有15项错误、3项既有格式警告，无.ko。
**目前按绑定寿命持有SMMU PM，启动暴露根尚无GPU/CP/preemption退休回收；正常runtime-PM交接、保留对象回收及外层void组件/devres安全解绑未完成。主机模型不证明硬件隔离/运行。**
下一步解决DMA_BUF命名空间、未导出的scheduler/VA接口及真实SCM/clock/SMMU提供者全内核链接，继续完成生命周期与SDE/eDP。不能部署；本轮未访问或重启设备，B0m保持。

## 2026-11-11：小米 KGSL v56 接通进程页表与默认根恢复

第32项process-table补丁把进程创建/物理SG/guard/unmap接到独立原生io-pgtable后端；不再分配手机dynamic子域。
失败回滚不确定时保留memdesc归属；销毁回调改int，失败保留外层页表、父域、device/module引用，close拒绝仍有子表引用的父域。
实际CP命令生成入口先标记根已导出；已导出根和非空VA树拒绝销毁。**尚无GPU/CP/preemption退休回收协议，已提交根暂时持续保留，不能用于长期运行。**
第33项default-root补丁取消split模式下CPU恢复直接成功的旧行为，改调原生provider reset_ttbr0；仅允许已附加的默认根，失败锁存错误。
soft-reset检查AWARE与默认根恢复返回值，成功后才发布ringbuffer默认状态并重启GPU；移除该路径旧私有SMMU寄存器写入/轮询。
39后端＋23实际进程调用场景、18默认根/soft-reset场景、两组各7运行反例、32 MMU回归和9准备器检查通过；5个完整ARM64对象-Werror零警告。
33补丁/109源重复准备一致；全量54对象/18错误/3警告，无MODPOST/.ko。**全局父域/空默认TTBR0安装仍是旧接口，kgsl_iommu整对象未编译通过，候选不能部署。**
主机模型不证明硬件页表隔离/恢复；外层void组件解绑仍可能释放devres，get_device不能代替安全解绑。SDE/eDP和双机验收尚未完成。
本轮源码阶段未部署/重启；之前已实机复核B0m调试设备不锁屏/接电常亮。

## 2026-11-11：小米 KGSL v53 接入原生SMMU故障保留与恢复接口

匹配6.18核心对EBUSY仍清FSR并终止事务，与KGSL“保留停滞等待恢复”的语义不符。独立SMMU provider v3新增显式opt-in策略、checked terminate/rearm与Adreno IRQ处理。
提供者在调用GPU fault handler前屏蔽CFIE、发布pending并持有PM引用；EBUSY保持停滞，终止成功仅清pending，复位后的rearm才恢复中断/策略并释放持有引用。
第31项KGSL smmu-fault补丁通过provider读取真实FSR/TTBR0/CONTEXTIDR，移除四个不存在的下游IOMMU fault flags；setup/策略/终止/rearm改用原生回调。
MMU两个恢复回调改int；dispatcher终止失败保留队列/故障/暂停引用并停止reset/replay，四条启动路径检查rearm错误，不继续ringbuffer/CP启动。
28实际provider＋KGSL IRQ/helper host场景、8运行反例、29 dispatcher场景及1反例、31 provider回归与18准备器测试通过；provider和五个KGSL对象-Werror零警告。
31补丁/109源与3个provider源重复准备一致；整体54对象/25错误/3警告，无MODPOST/.ko。原生父域/进程页表和safe detach仍未完成，不能部署。
**PM/MMIO/调度是主机模型，嵌套回调不是并发压力或实机故障恢复证明。终止错误保留PM引用，尚无终止失败后的最终回收；HWSCHED独立故障恢复也未闭环。**
本轮未访问/部署/重启设备；B0m与常亮偏好保持。provider私有接口扩展须重建所有消费者，包括回退MSM，禁止只替换提供者。

## 2026-11-11：小米 KGSL v52 接入6.18 SCM aperture接口

第30项scm-api补丁将IOMMU/A5xx切到原生SCM头；aperture保留实际context bank，检查8位范围、SCM就绪和调用能力，EBUSY仅重试一次并返回实际错误。
核对donor与6.18：MP服务0x0c/命令0x1b/四参数布局一致；A5xx ZAP仍为BOOT状态0/PAS13，保留transport及固件结果语义。没有新建SCM替代实现。
实际KGSL＋匹配SCM描述符函数覆盖256个合法bank与17个边界/能力/重试/ZAP检查，5运行断言反例、47电源回调回归及9准备器检查通过。
A5xx完整ARM64对象-Werror零警告，30补丁/109源重复准备一致；相对v50仅2源改变、107源保持。
完整构建54对象/29错误/3警告，无MODPOST/.ko；旧SCM头解除后暴露的错误全部在kgsl_iommu的旧domain属性/故障接口，必须接原生父域与进程页表。
**固件transport仍为主机模型，IOMMU整对象未通过；不是实际SCM调用、页表隔离或GPU运行证明。** 原生BW/生命周期和SDE/eDP尚缺，候选不能部署。
本轮未部署/重启；B0m和调试设备不锁屏/接电常亮已重新核验。v51为带A5xx两项警告的中间候选。

## 2026-11-11：小米 KGSL v50 保留复位失败队列，移除伪完成

第29项dispatcher-recovery补丁删除reset失败BUG_ON和reset前强制写ringbuffer完成timestamp；失败保留未退休队列/count/halt/fault，锁存transition_error。
已知失败状态下sendcmd/worker/issue/timer停止新的提交与硬件检查；成功reset后仅取消旧内部ringbuffer等待事件，等待方重新检查真实timestamp，未退休返回EAGAIN。
新recovery_active保护reset内启动函数的fault清除，保留恢复期间到达的新故障；该情况拒绝重放。SMMU待IRQ处理的EBUSY分支也归还取出的fault位。
删除4处仅写OEM游戏PID事件缓冲的msm_performance旁路，保留所有KGSL trace调用/帧计数；补strscpy和sysfs default_groups，未用空接口代替。
27实际dispatcher/ringbuffer-wait host场景、9运行反例、47电源回调回归及9准备器检查通过；dispatcher/Adreno/GMU/RGMU/HWSCHED五对象-Werror零警告。
29补丁/109源可重复准备，3源改变/106源保持；整体53对象/2错误/3警告，无MODPOST/.ko。剩IOMMU/A5xx旧SCM头阻断。
**这是复位结果与队列保留的源码接线；reset/MMIO/replay/events仍为host模型。后续重放失败、异步竞态、故障回收/解绑与原生SMMU尚未闭环。**
完整KGSL及SDE/eDP、双机运行验证未完成；本轮未部署/重启，设备B0m/常亮保持。v48/v49仅中间检查点。

## 2026-11-11：小米 KGSL v47 接通时钟保持/供电关闭错误

第28项power-callbacks补丁将两层时钟保持与regulator_disable回调改为int，失败不继续写下一保持位、不清GPU供电标志。
A5xx实际轮询/状态失败及Holi SPTPRAC超时返回错误；A6xx两处GMU SPTPRAC关闭回调类型匹配，不作强转。GX CPR局部helper改static。
通用INIT/WAKE/SLUMBER检查enable/context/stop/clock/regulator返回值，失败停止后续关钟/断电或状态发布；已不确定的切换记transition_error。
该错误阻止后续状态请求、first-open、active-count和reset入口；失败suspend不重新开放提交。没有未经验证的自动清除/重试。
47实际函数host场景/7运行反例、27 clock-owner＋33 reset＋52 power-off回归及9准备器检查通过；五个ARM64对象-Werror零警告。
28补丁/109源重复准备一致；整模块52对象/3错误/3警告，无MODPOST/.ko。A5xx完整对象仍被旧SCM头阻断，其新函数只有host证据。
**通用dispatcher仍有reset失败BUG和reset前强制timestamp退休；完整故障队列、直接MMIO、regulator_disable_poll与恢复/解绑未闭环。**
原生BW/SMMU/固件和SDE/eDP继续推进；禁止把本候选部署或称完整PM通过。本轮未部署/重启，当前B0m/常亮保持。

## 2026-11-11：原生时钟保持提供者与小米 KGSL v46 编译检查

独立clock-provider-v1为6.18实现真实branch2保持位接口；只接受已注册的受支持分支时钟，使用实际halt_reg和BIT12/13/14，保留写后屏障/1us间隔。
提供者注册/移除与操作加锁，devres先移除查询项再注销时钟；PM恢复失败不写寄存器，写入/PM释放错误返回，不把未知时钟或缺失指针当成功。
27实际函数host场景、6运行反例、9准备器检查通过；clk-branch/clk-regmap两个ARM64对象-Werror通过，四个提供者源重复准备一致。
v46沿用v45的27补丁/109源，增加提供者头后A3xx对象-Werror通过；全量51对象/5错误/4警告，无MODPOST/.ko。
**KGSL旧void时钟回调仍忽略返回错误，尚未完成调用方接线；新编译暴露两处SPTPRAC int关闭函数绑定到void regulator_disable槽。**
下轮必须修复真实调用链，不能用强转或空实现。SMMU/原生带宽/固件/SDE/eDP仍缺；未部署/重启，不热替换活动时钟提供者。

## 2026-11-11：小米 KGSL v45 明确原生GPU时钟/供电归属

第27项clock-owner补丁让A680.1电源初始化先确认GMU组件绑定完成和dev_ops，再跳过手机CPU侧时钟获取/舍入/设频及regulator获取。
目标频率表原值保留，GMU继续拥有自身时钟和cx/gx域；公共约束、waitqueue、runtime-PM和timer初始化照常执行。0/超限档位返回EINVAL。
27实际初始化函数host场景、5编译后运行反例、9准备器检查通过；kgsl_pwrctrl.o ARM64 -Werror零警告，27补丁/109源重复准备一致。
另修正同文件2处格式类型和13处显式fallthrough标记；不是修复旧电源状态机的错误传播。整体仍50对象/5错误，警告14降至3，无MODPOST/.ko。
原生带宽、运行时clock/PM错误传播、SMMU/固件/生命周期和SDE/eDP未完成；不可部署。本轮未部署/重启，B0m/调试设备常亮保持。

## 2026-11-11：小米 KGSL v43 接入原生GPU OPP频率/电压

第26项gpu-opp补丁让原生A680.1直接注册并读取OPP-v2，按KGSL降序生成频率/电压等级；错误不回退手机档位。
检查完整枚举、u32 Hz/u16电压等级、kHz无损转换和HFI预留OFF项容量；逐项put OPP引用，全部验证后才发布档位数组/数量。
初始档位保持多档默认index1，单档改为合法index0。总线索引保持未设置，现有validator明确拒绝，未伪造DDR/CNOC映射。
25原生OPP/实际ARC/HFI host场景＋28 DT回归、8运行反例、9准备器检查通过；adreno.o ARM64 -Werror零警告，26补丁/109源重复准备一致。
目标10档177–700MHz及实际GFX/MX cmd-db转换通过主机核对，不是硬件调频/HFI执行证明。整体仍50对象/5错误/14警告，无MODPOST/.ko。
带宽、原生时钟所有权、SMMU/固件/生命周期及SDE/eDP未完成；当前不可部署，本轮未部署/重启，B0m/常亮保持。

## 2026-11-11：小米 KGSL v42 修正 DT 引用与早期 probe 清理

第25项dt-ownership补丁将5个手机节点全局查询改为当前GPU直接子节点查询，保留借用parent引用，不会选中其他GPU的同名节点。
型号匹配释放返回的model或显式持有的fallback节点；powerlevel bins在成功/失败/不匹配路径释放父节点，L3正常/异常路径释放迭代与表引用。
speed-bin与电源解析失败接入已有common err，保留原始errno并清理IDR/bus/pdev/driver data。
28个实际函数/早期probe片段host场景、6运行反例、9准备器检查通过；adreno.o真实ARM64 -Werror零警告，25补丁/109源重复准备一致。
整模块仍50对象/5错误/14警告，无MODPOST/.ko；原生GPU OPP/BW、SMMU/固件、完整生命周期与SDE/eDP仍未完成，不可部署。
目标OPP缺带宽属性，主线A680没有动态BCM表；未引入手机固定BW表作为目标校准。未部署/重启，B0m/常亮保持。

## 2026-11-11：小米 KGSL v41 接通原生 GPU 身份、IRQ 与组件注册

第24项gpu-probe补丁增加原生qcom,adreno-680.1平台匹配，按该精确compatible识别0x06080001；若同时给出旧chipid必须一致，解析错误原样返回。
GPU IRQ从目标第0项取得Linux virq，申请即NO_AUTOEN；不依赖目标未提供的kgsl_3d0_irq名称，也不把SPI300当Linux IRQ。
GMU组件按成功绑定状态配对解绑，移除device probe/error/unbind里会消耗借用GPU节点的全局搜索；失败清理先unbind再清pdev/driver data。
GMU/RGMU使用标准platform_register_drivers成组注册，传播失败并自动反向回滚；GPU驱动注册失败也撤销GMU组，unregister可用于init错误路径。
27实际函数host场景、9运行断言反例、9准备器检查通过；adreno/kgsl/GMU core三个ARM64对象-Werror零警告，24补丁/109源重复准备一致。
整模块仍50对象/5错误/14警告，无MODPOST/.ko。原生GPU OPP/带宽、余下DT引用与早期失败清理、SMMU/固件和SDE/eDP未完成，不能部署。
本轮未部署/重启；B0m和调试设备常亮保持。组件状态跟踪不是运行中quiesce/unsafe unbind解决方案。

## 2026-11-11：小米 KGSL v40 接入目标 GMU MMIO/IRQ 与组件引用

第23项gmu-resources补丁使用A680目标gmu/gmu_pdc/gmu_pdc_seq资源及hfi/gmu IRQ名称，校验映射范围、对齐与GPU相对偏移；映射成功后才发布指针/长度。
GMU与两套HFI申请IRQ时使用IRQF_NO_AUTOEN，删除随后多余的disable，避免上电前短暂开启IRQ；保留实际申请/延迟错误。
三处GMU匹配加入qcom,adreno-gmu-680.1；GPU组件按qcom,gmu引用绑定，原生A680缺失/无效/禁用引用返回错误，不搜索其他GMU替代。
36实际函数host场景、8编译后运行断言反例、9准备器检查通过，4个ARM64对象-Werror零警告；23补丁/109源重复准备一致。
整模块仍50对象/5错误/14警告，无MODPOST/.ko；原生GPU兼容/芯片识别/OPP、SMMU接线、固件和SDE/eDP尚未完成，不可部署。
匹配6.18组件框架已有bind失败devres group清理；运行中unsafe unbind/失败资源最终回收仍未闭环。测试模拟IRQ/MMIO，不是硬件启动证明。
本轮未部署/重启；当前B0m与用户要求的常亮设置保持。

## 2026-11-11：小米 KGSL v39 接入A680原生cx/gx电源域

第22项genpd补丁把A680供电probe从手机vddcx/vdd regulator改为目标cx/gx命名域；必需域缺失/延迟错误原样返回。
上电顺序CX→GX，GX引用仅记账、实际上电仍由GMU固件负责；跟踪两域引用与时钟持有，失败回滚不重复put。
关闭先释放GX并确认provider OFF/可读GX状态，再停时钟和释放CX；每次put前请求dev_pm_genpd_synced_poweroff，强制votable CX执行硬件poll。
同步PM返回0后仍检查genpd OFF，失败记入power_error；probe后续失败释放空闲域，unsafe remove保留GMU资源。
34 genpd＋33 reset＋52 power-off＋53 bootstrap＋36 OPP host场景、8运行反例及9准备器检查通过；GMU/HWSCHED对象-Werror通过。
真实对象引用六项匹配内核导出的PM符号，CONFIG_PM/PM_GENERIC_DOMAINS均启用；22补丁/109源可重复准备，4源改变/105源相对v37未变。
整模块仍50对象/5错误/14警告，无MODPOST/.ko。v38缺少votable CX同步关闭请求，保留作中间快照，由v39替代。
**源码已接原生genpd，尚未在目标运行；domain OFF仅是瞬时状态，共享域必须协调，不能当完整电源周期证明。**
外层void组件/master解绑及失败资源owner回收仍未闭环，禁止热卸载；SMMU/IRQ/寄存器/固件、完整KGSL及SDE/eDP尚未完成。
本轮未部署/重启，设备B0m和常亮设置保持。

## 2026-11-11：小米 KGSL v37 接通硬复位与CX关闭错误

第21项reset-errors补丁将SPTPRAC关闭、GMU硬复位和CX关闭改为返回实际错误，修复RPMh errno按位合并与GBIF第一次halt错误被覆盖。
GX regulator enable失败不再继续disable；缺GX句柄且硬件仍开、关闭后GX仍开均返回错误；A660临时CPR位仍配对恢复。
硬复位失败不继续关时钟/发布NONE，restart不清GPU_STARTED或继续boot；HWSCHED解除上下文/唤醒unregister等待移到复位成功后。
启动清理中的reset/CX失败保留原始启动errno、记录power_error并保留带宽floor；CX模式/disable/实际状态轮询错误向上传递。
33个实际底层函数＋52调用方＋53 bootstrap host场景、7运行反例与9准备器检查通过；GMU/HWSCHED两个ARM64对象-Werror通过。
21补丁/109源重新准备hash一致，5源改变、104源相对v36不变；整模块仍50对象/5错误/14警告，无MODPOST/.ko。
**cx/gx genpd尚未接入，MMIO/regulator/时序仍只做host注入；不能称物理断电或恢复已验收。**
修改的adreno_a6xx.c内CX helper未获完整ARM64对象证明（仍被旧clock头阻断）；非GMU void调用方也尚不能向上传递错误。
失败状态仍需重启；原生SMMU、完整生命周期/故障退休、SDE/eDP和双机验收未完成。本轮未部署/重启。

## 2026-11-11：小米 KGSL v36 保留关闭错误，禁止失败后的休眠/重放

第20项power-off-errors补丁修复GMU/HWSCHED的RSCC错误被后续GBIF结果覆盖、已有gmu_fault返回成功，以及上层忽略power_off返回值。
报告的关闭失败记录power_error；上层保留GPU_STARTED与资源/上下文清理状态，PM不置SUSPEND、不重新打开提交。
后续boot/active-count/power-off/PM/restart拒绝已记录失败；HWSCHED重启失败返回errno而非BUG_ON，恢复路径不再执行replay。
48个实际函数host场景及5组运行断言反例通过；GMU/HWSCHED/调度三个ARM64对象-Werror通过。
20补丁/109源重新准备hash一致；5源改变、104源相对v35不变，整模块仍50对象/5错误/14警告，无MODPOST/.ko。
**这是接入genpd前的错误链修复，尚未绑定cx/gx；底层硬复位/CX关闭仍为void，不能据此证明物理断电。**
失败状态目前保持到重启，没有自动恢复/资源回收；既有HWSCHED强制退休也未证明真实fence完成。当前不可部署。
本轮未部署/重启；原生SMMU、完整KGSL运行、SDE/eDP与双机验收仍未完成。

## 2026-11-11：小米 KGSL v35 接入目标GMU时钟与OPP表

第19项gmu-opp补丁绑定A680目标的ahb/gmu/cxo/axi/memnoc时钟，并从GMU设备树读取真实OPP。
启动200MHz必须存在于已构建的表；速率设置/实际速率/时钟启用失败返回错误，成功后才更新RDPM和AWARE状态。
GMU电压等级通过既有ARC算法转换；HFI v1发送全部GMU条目，修复固定复制两项会漏掉500MHz档的问题。
36个实际函数host场景使用真实HFI结构/消息头与捕获SQ2 cmd-db，5组反例均编译后断言失败；时钟/OPP/固件调用仍模拟。
三个改变对象GMU/RPMh/HFI匹配ARM64 -Werror通过；相关51 RPMh＋18 DDR＋49 bootstrap＋58 GMU回归、9准备器检查通过。
19补丁/109源重新准备逐hash一致，5源改变、104源相对v34不变；整模块仍50对象/5错误/14警告，无MODPOST/.ko。
**仍未接入目标cx/gx电源域：probe在新时钟绑定前仍请求旧regulator，不能称资源启动完成。**
匹配6.18 genpd suspend忽略物理power_off返回，PM成功不能当断电证明；GX上电由GMU固件控制，需通知/真实握手与完整失败清理。
本轮未部署/重启；原生SMMU、完整KGSL提交/退休/PM、SDE/eDP与双机验收保持未完成。

## 2026-11-11：小米 KGSL v34 接入目标ZAP加载与显式非安全路径

第18项zap-loader补丁替换subsystem_get：使用设备树firmware-name/预留内存、request_firmware_direct、qcom_mdt_load及GPU PAS13认证。
禁止从手机core名推导签名镜像。缺节点/文件、MDT错误及认证失败原样返回；失败后shutdown失败记录zap_load_error，拒绝再次覆盖。
仅“节点显式disabled且mmu.secured为false”允许未加载ZAP返回，实际A6xx调用方随后执行SECVID_TRUST_CNTL切换。
29个实际loader＋A6xx启动host场景、5运行断言反例和9准备器检查通过；adreno.o匹配ARM64 -Werror通过。
同boot只读确认当前SQ2节点disabled和Surface专用固件路径；没有运行新KGSL或写设备。该事实不是签名固件认证证明。
整模块现50对象/5错误/14警告，无MODPOST/.ko；3源改变、106源相对v31不变。v32/v33保留为中间失败检查点。
**成功PAS生命周期/退出与恢复尚未完成；不能热卸载GPU。** 原生SMMU、时钟/性能接口、SDE/eDP和完整实机验收仍未完成。

## 2026-11-11：小米 KGSL v31 接入6.18 SCM DCVS调频

第17项tz-dcvs补丁适配governor，独立三文件SCM provider补丁提供真实DCVS v2 init/update/reset/CA调用。
provider使用现有SCM共享qcom_tzmem池，检查精确v2能力与ARM64 SCM约定；不把GPU物理地址冒充DMA映射。
调频返回值拆为transport errno与有符号delta；governor在SCM失败时返回错误、保留采样和原频率，不把负errno当升频增量。
初始化只发送已填充频率表；拒绝不支持版本/请求的CA失败；启动资源错误清理，suspend reset失败不伪报成功。
35 provider＋25 governor实际源码host场景、5组运行断言反例与14准备器测试通过；两侧对象匹配ARM64 -Werror通过。
整模块现49对象/6错误/14警告，无MODPOST/.ko。v31仅1个KGSL源改变，108源与v29一致；v30为中间编译检查点。
**未验证实机固件DCVS能力、响应与电源行为；必须重建SCM及完整内核/消费者，现有symvers尚无新增导出。**
原生SMMU生命周期、SCM aperture/安全固件、时钟/性能接口、SDE/eDP与完整验收仍未完成。本轮未部署/重启。

## 2026-11-11：小米 KGSL v29 保留无法确认回滚的映射

第16项map-retention补丁新增MAP_UNCERTAIN状态，覆盖payload、guard与SVM失败清理，5源改变、104源相对v28未变。
核对匹配6.18的iommu_map_sg/map_nosync/map：内核尝试回滚但忽略unmap返回值，负errno不能证明无残留。
旧domain API调用失败保留原errno并标记不确定；短映射/guard失败的外部回滚也检查返回值。
MMU拒绝重map/unmap/释放不确定VA，backing析构也拒绝释放；SVM清理失败撤下ID并接入v28对象保留。
32映射/guard、17核心释放、19外层/SVM场景及相关导入/缓存/userptr/fence回归通过；5运行断言反例、9准备器测试通过。
kgsl/MMU/sharedmem三个对象匹配ARM64 -Werror通过；整模块仍48对象/7错误/14警告，无MODPOST/.ko。
**旧API无法区分干净失败，因此部分实际已回滚的对象也会保留；尚无退休后的回收。** 不是长期可部署的内存策略。
原生io-pgtable接线、全局域/对象退出、设备解绑、fault/PM和SDE/eDP仍未完成。本轮未部署/重启，设备B0m/常亮保持。

## 2026-11-11：小米 KGSL v28 接住上层释放错误并登记保留对象

第15项entry-retention补丁改kgsl.c/发起对象结构/debugfs，105源相对v27未变。
普通mem_entry销毁先移除ID/解除映射，失败时保留entry和process引用、增加pagetable与模块引用，统计不提前扣减。
attach的ID/映射失败出口及三处已分配backing的ioctl/allocate清理接入同一保留机制，登记后不盲目重放unmap。
新增只读root debugfs retained_memory列出表名、地址、大小和errno。14实际函数host场景/4运行断言反例及9准备器测试通过。
kgsl.o匹配ARM64 -Werror通过；整模块仍48对象/7错误/14警告，无MODPOST/.ko。
**尚无恢复后回收实现，错误对象持续占用内存/进程/页表/模块引用；global close、设备解绑和失败map回滚归属仍未解决。**
本轮未部署/重启，B0m/常亮保持；不能称完整生命周期、native KGSL或SDE已完成。

## 2026-11-11：小米 KGSL v27 保留解除映射失败的地址归属

第14项release-errors补丁修改MMU/sharedmem释放函数和声明（4源，105源相对v26不变）。
put_gpuaddr返回int，unmap失败不清pagetable/gpuaddr、不释放VA；缺少地址释放或unmap回调返回ENODEV。
sharedmem_free传播错误，不调用backing free。11项实际函数host场景及3组编译后断言失败反例通过。
MMU/sharedmem对象ARM64 -Werror通过，整模块仍48对象/7错误/14警告，无MODPOST/.ko。
**上层entry/process/global拥有者仍忽略返回值，生命周期安全尚未完成。** 失败映射回滚与异常对象归属/退休回收仍缺。
本轮未部署/重启，B0m及常亮保持，完整KGSL＋SDE目标未完成。

## 2026-11-11：小米 KGSL v26 修正GPU drvdata与SMMU私有接口布局

第13项固定smmu-layout补丁在kgsl_device首字段放置真实adreno_smmu_priv，并static_assert偏移0。
6.18 provider通过dev_get_drvdata直接写接口；旧首字段是device指针，若直接native attach会覆盖KGSL对象。
现有adreno_device_probe仍发布同一个kgsl_device指针，KGSL消费者强转保持；没有把drvdata改成另一个独立分配对象。
完整48对象重编，仍7错误/14警告、无MODPOST/.ko；MMU核心与原生页表对象使用provider v2头以-Werror重编通过。
这是域绑定的必要布局修正，尚未接入native attach/默认TTBR0/进程表及退出/fault生命周期；不能称SMMU接线完成。
本轮未操作设备，B0m/常亮偏好保持；完整KGSL＋SDE目标仍未完成。

## 2026-11-11：小米 KGSL v25 快照源码适配6.18

第12项固定snapshot补丁仅改kgsl_snapshot.c，109准备源中108份相对v21不变。
移除目标没有的OEM msm_minidump注册/撤销，保留普通sysfs快照、原子采集和panic notifier；未移植固件minidump通道。
时间/字符串/通知头及const sysfs回调换为6.18接口。快照对象ARM64 -Werror通过，准备器9项检查通过。
整模块48个.o、7错误/14警告、退出2，无MODPOST/.ko；时钟、SCM/ZAP、性能接口和原生页表生命周期仍缺。
本轮未部署或重启，现机B0m/常亮保持。对象编译不是硬件快照或原生提交验证，完整KGSL＋SDE目标未完成。

## 2026-11-11：小米 KGSL v21 禁止 MMU 失败降为物理地址或共享进程页表

第11项固定补丁 `kgsl-xiaomi-618-mmu-required.patch` 已进入准备链，109份源中仅3份相对v20改变。
IOMMU probe原始错误直接返回，删除NOMMU实现；进程页表创建失败不再返回defaultpagetable或切换global位。
缺少init/get回调返回ENODEV，失败对象不发布；qcom,global_pt配置明确拒绝，内核global/secure/LPAC表名称仍保留。
32项实际函数主机测试通过，恢复5种旧行为的反例均编译成功后触发断言；准备器9项测试通过。
匹配6.18 ARM64的kgsl_mmu.o以-Werror通过。整模块仍失败、47个.o、8项缺失旧接口头错误，无MODPOST/.ko。
原生页表后端仍未接入旧生命周期，不能称运行时进程隔离完成；本轮未部署或重启，现机B0m/常亮偏好保持。

## 2026-11-11：小米 KGSL v20 加入原生 io-pgtable 后端，尚未切换旧调用链

第10项固定补丁新增`kgsl_iommu_table.c/h`和Kbuild接线，共109 prepared源。后端使用真实alloc_io_pgtable_ops、
独立root、父pagetable/device引用、物理SG和4K叶子映射；处理部分映射错误、精确前缀回滚及只读重复guard页。
每表mutex保护映射计数和异常状态；未清空映射或回滚不完整的表拒绝销毁。调用方仍须保留对应buffer backing并负责GPU退休。
SMMU provider v2新增返回错误的ASID flush，后端unmap/rollback/destroy已调用；该操作自身不写TTBR0/CONTEXTIDR。
39个后端＋31个provider实际源码主机场景、9＋6组反例通过；9项准备器测试通过，新增源支持也有输入/输出hash。
两个新/改对象均通过匹配6.18 ARM64的-Werror编译；整KGSL仍退出2、47个.o、8错误/14警告，无MODPOST/.ko。
旧`kgsl_iommu.c`初始化、分配与释放调用链尚未切到后端，不能称已实现运行时进程隔离或提交；当前候选仍不可部署。
发现旧unmap错误路径会丢失外层owner：底层MAPPED检查已有，但mem_entry/process/pagetable引用仍可能被释放；必须配套修正。
本轮未访问/部署/重启设备，B0m与常亮偏好保持。完整KGSL＋SDE目标不变；证据和接入条件见HANDOFF/BUILD。

## 2026-11-11：KGSL 的 SMMU provider 前置接口已实现，调用方未接入

新增独立 `kgsl-618-smmu-provider.patch`，以匹配的Linux6.18主线SMMU为输入；不是第10项小米donor补丁。
修正TTBR0 setup在runtime PM失败前污染恢复缓存的问题，原errno返回；新接口读取真实bank/ASID，
在GPU已静止的前提下恢复长期保留的默认TTBR0并清CONTEXTIDR、按真实ASID失效TLB，明确返回同步超时。
28个实际源码ASan/UBSan场景通过，原版缓存缺陷及4项故障变体均触发预期断言；3项准备器拒绝检查通过。
匹配ARM64配置下provider对象以-Werror编译通过，仅为单对象，没有完整内核链接、KGSL接线或硬件证明。
小米v17的107源码与9补丁保持，之前整KGSL构建仍失败；原生提交、每进程隔离、SDE/eDP与完整目标未完成。
新增私有接口改变结构体大小，后续必须成对重建SMMU与所有使用该头的GPU消费者；禁止只换provider配旧msm模块。
默认页表必须长期存活；停GPU、禁用TTBR0并成功drain后才可回收，错误时不能提前释放。不得复制MSM最后页表忽略返回值的释放路径。
本轮无设备部署/重启，B0m与常亮设置保持；源码、失败取证与下一步见HANDOFF，准备命令见BUILD。

## 2026-11-11：小米 KGSL v17 修正首次启动带宽生命周期

新增第9项固定bootstrap补丁，候选仍不可部署。GPU ICC请求统一经过真实helper，跟踪已成功的AB/CPU峰值和启动峰值下限；
HFI设置正常AB时保留启动下限，初始化成功后仅释放启动下限，修复原first_boot直接清零正常AB的问题。
普通GMU/HWSCHED均传播初始/释放投票错误；CM3启动尝试后失败走HFI stop＋既有GMU suspend，再清理ICC，保留首个错误。
ICC失败后即使回到旧缓存值也重新请求，不能假设provider回滚成功。49启动场景、6组反例、80 bus＋39 ICC回归通过。
58 GMU、69 RPMh/DDR与10项unittest本轮通过；未改内存函数176场景及最终源依赖hash复核，证据见HANDOFF。
严格6.18仍46个.o、8错误/14警告、退出2，无MODPOST/.ko。未访问设备或部署/重启；B0m与常亮偏好保持。
**校正v15范围：** bus测试mock了DCVS provider；实际GMU零档由slumber处理，软件OFF/缓存清理不证明硬件投票已撤销。
底层GMU suspend/rail helper仍有void/吞错路径，新增测试只证明调用顺序；完整PM、OPP/SMMU/SCM和原生KGSL＋SDE仍未完成。

## 2026-11-11：小米 KGSL v15 接上目标 ICC 契约并修正带宽失败状态

新增第8项固定bus补丁，候选v15仍不可部署。只读取得当前B0m的有效DT：GPU为gfx-mem路径，
177–700MHz十档OPP有opp-level但无带宽属性；没有手机qcom,devfreq-qoslat节点。
核对donor后移除其仅用于AOP DDR perfmode的dummy devfreq发现/状态，不声称实现该手机策略或等价DDR频率控制。
目标带宽走真实ICC/RPMh；本批修正空/错误ICC句柄、全部powerlevel边界、64位单位换算、失败状态保留及AXI/调速回调重试。
80个实际源码bus场景、4组反例通过；176内存/MM/fence、58 GMU、39 ICC、69 RPMh/DDR及10项unittest通过或输入hash复核。
严格6.18构建46个.o、8错误/14警告、退出2，未到MODPOST，无.ko；不能据错误减少宣称可启动。
实际OPP/DDR/CNOC档位生成、启动投票清理、上层时钟/PM错误传播及每进程SMMU/SCM/clock仍待完成。
现机仅只读取证，B0m/常亮与发布pins保持，未部署/重启/提交/推送，完整KGSL＋SDE目标继续。

## 2026-11-11：小米 KGSL v12 接入 SC8180X RPMh 表生成

本轮新增第7项固定RPMh/DDR补丁，候选v12仍不可部署。按目标ICC拓扑选择MC0/SH0/ACV和SN15/SN3/CN0，
地址/unit/width/VCD从cmd-db运行时读取；禁止用A660配置或把本机采集值固化成通用运行条件。
已只读采集同一B0m boot的cmd-db，使用私有临时mount namespace挂载只读debugfs后清理，没有投票/重启/部署。
修正cmd-db错误指针/短数据/除零、分配回滚、HFI表越界、CNOC错误指针释放及DDR节点/属性读取错误。
51 RPMh＋18 DDR场景与3组反例通过；176内存/MM/fence、58 GMU/QMP、39 ICC及10项unittest回归通过。
严格6.18构建45个.o、12错误/14警告、退出2、无MODPOST/.ko；RPMh与kgsl.c新通过编译。
仍缺实际OPP/DDR/CNOC级别构建、legacy dummy devfreq QoS替代、每进程SMMU/SCM/clock及运行验证。
SC8180X以外的RPMh拓扑本候选明确拒绝；不是通用手机KGSL移植。现机B0m/常亮、发布pins与完整SDE目标保持。

## 2026-11-11：小米 KGSL v10 完成 GMU 内存与 QMP 一批适配

本轮源码候选推进至v10，新增固定GMU memory/domain与QMP两项补丁（共6项），仍不可部署。
GMU使用DMA API取得物理SG，不把DMA handle当PA；映射负errno、长度不符与临时SG清理已覆盖，
成功后才发布地址/推进VMA，拒绝secure降为普通GMU映射。分配/释放使用相同页对齐大小。
GMU域改用6.18 paging-domain API，要求已有identity默认域；移除强制set_dma_ops。参考SC8180X DTS
及qcom arm-smmu有该配置，但有效目标绑定/资源仍须实测，GPU每进程SMMU未实现。
QMP改为qmp_get/send/put，传播初始化/首次启动/ACD切换错误，发送失败不提前更改ACD状态。
58新增主机场景、3组v9反例、既有176内存/MM/fence＋39ICC场景与10项unittest通过。
真实匹配6.18构建：43个.o、15错误/14警告、退出2，未到MODPOST，无.ko；GMU/HWSCHED HFI两源编译无诊断。
未访问/部署/重启设备，B0m与常亮偏好保持。继续SMMU/DDR/devfreq/SCM/clock及资源接线，完整目标不缩减。

## 2026-11-10：调试设备按用户要求不自动锁屏、接电常亮

用户本轮明确要求“调试设备改为不锁屏常亮”，覆盖历史30分钟熄屏偏好，仅作用于当前调试设备。
重新核对仍为B0m boot f7f2a4f5… / Linux6.18.60，动态发现当前Android用户0、原本无安全凭据且lockscreen disabled。
已持久设置当前用户screen_off_timeout=2147483647（电池模式约24.9天）、sleep_timeout=-1，
global stay_on_while_plugged_in=15，确认lockscreen disabled并唤醒屏幕。
运行时mWakefulness=Awake、mIsPowered=true、mPlugType=1、mStayOn=true；接电不会因空闲熄屏。
没有清除凭据、重启、写EFI或替换图形载荷；电池模式不是无限时长保证。后续不得恢复旧30分钟值，
也不把此次单机偏好强加于通用镜像/其他用户。取证仅ignored out/kgsl-sde-port-20261110/display-awake-settings。

## 2026-11-10：小米 KGSL v6 继续源码适配，尚无可部署模块

小米四项有序补丁已覆盖ICC、DMA-BUF生命周期/完整buffer cache、核心API、长期用户页pin/unpin和shrinker。
176个实际函数主机场景通过，三组旧行为反例成立；软件fence测试包含真实6.18比较函数，但非GPU/显示completion。
未知exporter/部分cache范围仍未实现，不能扩范围invalidate或伪报成功。每进程SMMU/GMU仍须真实接入。
当前Kbuild虽生成41个.o，仍17错误/19警告、未到MODPOST；新增int-conversion错误检查，禁止把生成.o当可运行证明。
设备、B0m用户验收和发布pins保持；本轮无设备操作/提交/推送。完整目标与所有full_*仍未完成。
下一阶段转DDR/ICC/OPP及SMMU/GMU资源接口，不能靠旧核心头/恒成功stub满足编译。详见HANDOFF/BUILD首节。

## 2026-11-10：小米 SM8350 KGSL 参考已接回，下一阶段优先适配

用户指明小米骁龙888并提供旧Quest2会话；已核对同一LineageOS kernel提交7ede20c8…为5.4.302，
旧Oculus为4.19.315，目标仍6.18.60/SC8180X A680.1。mapper5＋原厂gralloc＋源码QTI HWC保持。
小米完整参考固定426文件，ARM64共有62ioctl编码与92结构体876布局项一致；尚非UMD运行/32位证明。
小米作为下一阶段优先适配候选，Oculus保留UMD语义对照；两份源码配置均不可构建，不切换设备。
小米ICC错误传播补丁实际函数39场景及旧版反例成立；Oculus v3内存/cache共90＋42场景通过。
cache仅固定system/WC完整buffer，范围同步/通用属性契约尚缺；不能推广名字判断或伪造成功。
两份严格6.18编译均未到MODPOST，仍须DMA-BUF/SMMU/GMU/ICC/OPP和核心API实质适配。
33项主机unittest、发布verify通过；本轮无设备访问/部署/重启，现机B0m与发布pins保持。
真实提交、DPU/SDE/eDP、fence、性能/PM/长时/SQ1及完整目标继续未完成。详情见HANDOFF/BUILD首节。

## 2026-11-10：KGSL/SDE 开始实施，独立 WC 内存候选已正常重启

用户已要求按 `docs/QUALCOMM-KGSL-SDE-PORT.md` 推进完整目标。完成第一批独立内存实现与当前 SQ2 分项回归：
6.18 `system-uncached` exporter + 成对图形 ION/视频桥，原 cached heap 保持；uncached 不再借 MSM GEM。
实际 v5 父包 `94b3f6bf…` → 内存候选 `180b1900…`，正常启动 `f7f2a4f5…`；Image/DT/MSM/DPU/OMX/用户配置保持。
mapper5 AHB/GLES/BufferQueue/YUV/P010、Vulkan 三轮 AHB 和 AVC30/HEVC6 精确帧通过；全部临时客户端已取回删除。
这只是 B0m 内存阶段，原生 KGSL、SDE、性能/PM/完整显示 fence 及 SQ1 尚未完成；`full_*` 保持 false。
发布 pins 仍 v21-release-4k，当前设备是独立实验候选，不把 main 旧包覆盖现机 v5/用户修复。
完整 donor 已固定，406 个关键源码文件校验；SDE donor 仅候选，SC8180X catalog/eDP 配对仍待完成。
详见 HANDOFF/BUILD 最新节；原回退与既有菜单保持，后续不得热卸活动 GPU/显示提供者。
用户已确认本次B0m候选B站HD播放/清晰度/页面/声音正常。KGSL源码导入/SG回滚87个主机场景通过，
真实6.18编译仍因旧ION/MSM bus/devfreq/SCM等接口失败；尚无可部署KGSL模块，不能把mock验收当硬件通过。

## 2026-11-10：KGSL＋DRM/SDE 专项移植方案已编写

用户要求结合当前工程及鸣潮 v5 实测编写下游 KGSL＋DRM/SDE 方案，见
[专项方案](docs/QUALCOMM-KGSL-SDE-PORT.md)。本轮是方案整理，尚未开始实现或部署。
后续技术顺序为独立共享内存后端＋原生 KGSL → 现有 DPU 上系统/性能闭环 → 下游 SDE/eDP → 联合验收。
保留 mapper5、原厂 gralloc、源码 QTI HWC/SDM 和当前视频/音频修复；SDE 全目标保留，默认晋升独立验收。
当前 main 发布仍 v21-release-4k；已拉取 v5 不等于合并或标准构建已启用。v5 实测热点为 GLES 提交/对象管理，
没有 KGSL/SDE 提升百分比。既存记录与私有证据保持，不恢复用户迁移或历史故障压力脚本。

## 2026-11-10：用户报告手动修复，停止迁移与救援操作

用户已报告自行修复，当前任务改为归档主线100b6d7/v21-release-4k的私有镜像资源；不再继续迁移或启动诊断候选。
手动修复步骤和设备最终状态未由代理重新采集，不把此前379文件恢复记为黑屏修复验收。
禁止继续未部署的user0迁移v2或全局重定向/data/data。迁移失败与已执行恢复的事实见HANDOFF首节。
私有归档不包含临时救援、用户数据或新Wi-Fi配置；验收启动包原有私有身份仍需保留并明确标注。

## 2026-11-10：4K 软解回退修复与独立 debug 入口

用户在 v21-release 下报告 Bilibili 4K 严重卡顿，并明确要求恢复一个带原 debug 信息的 GRUB 入口。
已新增 `sq2-qcom-618-debug` / `EFI/SQ2Dbg`，名称为 `SQ2 Linux 6.18 LTS (Qualcomm graphics debug)`，
载荷为清理前完整 v21 `71db2317…`，保留 SoundWire 和黑闪修复；未启用 `sq2.debug=1` 或更改网络策略。
默认仍为 `sq2-qcom-618-native` / `EFI/SQ2Nat`；原 `SQ2QDbg` 应急载荷不变。
这是本轮明确授权的诊断例外，覆盖下方历史“仅两个入口/不新增入口”；debug 入口尚未实启动验收。

根因证据：清理时重编桥接源含 v2-only 布局回调依赖，原已验收服务只导出 v1，
PresetANWBuffer 配置返回 -1010，Bili 在解码启动前 hard→soft；4K60 软解约87–90ms/帧。
这推翻了历史“清理仅改变诊断、不影响功能”的范围判断；此前六帧 ByteBuffer 测试没有覆盖此接口。
桥接现在优先 v2、兼容 v1，旧接口必须满足真实 V4L2 最小分配大小，错误/所有权/队列约束保留。
新增实际源码旧红/新绿 ASan/UBSan 测试；native/release 打包均检查成对服务导出，拒绝旧失配。

此前用户确认清晰度切换恢复是在临时 CSD 候选下，修复并未持久化。本轮一并整合源码 OMX 的
独立 Annex-B HEVC CSD + 完整 AU 输入适配，不伪造 SEQ/EOS/QBUF，不改变 PTS/flags；
保留真实 flush/释放时清理，完整上游碎片组帧、加密输入及 Main10 广告均不在本轮范围。
正常源码补丁没有候选诊断宏或存储/合并信息日志，保留真实错误日志，发布库剥除 debug 符号。

RAM实测：先只换桥接，原 OMX 的完整 AU Surface 六帧通过；再加入原 CSD 候选，
Bili 720p→4K→1080p→4K 四次硬解配置/格式输出，无 PresetANWBuffer 失败或 hard→soft，
真实映射 hash 和 Venus 活跃会话核对，用户明确回复“正常了”（画面及声音）。
DynamicANWBuffer 仍不支持，但正常回落到 PresetANWBuffer，不是这次错误。
应用启动统计的 avgDecTime 仍包含较大值，未据此声称稳定60fps性能基准通过。
去日志正式 OMX 又完成五组30帧，逐字节对照已独立审计参考完全一致，EOS/stop/delete及资源归零；
两轮 runner/cleanup均0，临时文件已取回，默认包将保留干净运行环境。

新唯一发布基线为 `v21-release-4k`，initramfs `0ada8ee0db271abc7a25f5c6ee63b0d484b03bed75c5352ae8f5d65f0902beb5`；
相较清理基线仅变 OMX、桥接和HIDL校验清单，其余有效记录/metadata一致；内核、音频、显示未改。
桥接 `7a7c4f57…`，源码 OMX `29f5d1b0…`；原服务 `7a6aa750…` 保持。
34项相关主机unittest、30项实际CSD源码场景、标准 verify/package通过。
已写入默认包并正常重启，boot6fd60796…，等待162秒后首次SSH成功；实际两个库hash核对。
启动后HEVC独立CSD Surface六帧与AVC Surface三十帧逐字节匹配参考，EOS/stop/delete与资源归零，
七服务running；临时客户端已删除。默认启动后再捕获4K硬解及清晰度切换，用户明确回复“完成 正常”，
确认4K、切换、声音及页面均正常；此次验收属于新默认包，不再依赖临时库。
证据仅 ignored `out/qualcomm-full-stack-20261108/v21-debug-entry-4k`，私有库/凭据/原始日志不上传。
内屏仅SDR，Main10过滤、PM/长时/SQ1及全部 full_* 未完成状态保持。

## 2026-11-10：唯一主线基线 v21-release（验收版清理）

清理版正常重启已验证：boot398dd33e…，第146秒首次SSH检查成功，7服务running；
HEVC硬解6帧可见像素与已独立审计父版逐字节一致，PTS/EOS/stop/delete成功，
Venus错误/实例/缓冲归零。50项相关主机测试与标准verify/package通过，临时复核客户端已删除。
未新增听音、长时、休眠或SQ1验证；沿用用户对功能父版的听音/页面验收。

用户确认 v21 听音、Bilibili 播放页/主页切换均正常，授权固定为唯一版本并提交主线，
随后要求设备及提交源码一并清理开发工具、日志和探针。本节覆盖下方历史“未提交”、
“当前 v20”及 Mesa 默认说明；旧日期的实验记录仅用于追溯，不是并列支持版本。

唯一受支持发布基线为 **v21-release / Linux 6.18.60-sq2-android1**，默认 `EFI/SQ2Nat`，
路线为 mapper5 + 原厂 gralloc + 源码 QTI HWC/SDM；initramfs SHA256 `e826df919c358a61ff023f6021ba9dae43d166b1d781a3b107ed8320286456a0`。
原 v21 功能父包为 `71db2317…`；清理版保留 SoundWire 容量/AHB 串行修复、DPU EBUSY
重提修复、原音频 HAL及原 OMX。HEVC CSD/HDR 实验候选未因基线整理被暗中提升。
清理只移除诊断输出/入口：桥接逐缓冲日志及弱观测回调、Venus 启动/阶段/PM 诊断开关、
启动持久日志采集器、随包测试工具；重编成对 Venus 模块与视频桥接，不热换提供者。

源码同时删除废弃采集器、临时探针及对应入口；正式显示/mapper builder 不再顺带编译探针。
保留真实错误处理、驱动安全边界、主机回归测试与构建记录。正常系统错误日志不是持续开发采集。
本机 `/data/local/tmp` 历史工具/向量/采集已清理（17166388→30388 KiB），移除三个测试 APK，
清除 ESP `googlebook-native-*` 日志；应用缓存、用户数据、校准和凭据保留。
旧 `EFI/SQ2QDbg` 仅为现有应急恢复载荷，不是受支持的第二发布版本；未新增备份或启动项。

`configs/kernel-baseline.json` 是唯一发布 pins，标准 `build.py verify/package` 默认选 SQ2Nat；
`compile` 仍产生独立未验收候选，不能凭版本号相同覆盖发布包。私有库、固件、凭据和原始日志
不提交。内屏仅 SDR，Main10 过滤保持，整栈长期目标尚未全部完成；
`full_stack_complete/full_hwc_verified/full_fence_lifecycle_verified` 仍 false。
清理、构建、完整性和本次启动记录位于 ignored `out/qualcomm-full-stack-20261108/v21-release-cleanup`。

## 2026-11-10：无声与 Bili 整屏黑闪修复已持久部署并通过用户验收

用户确认 4K→1080p→4K 无卡顿；独立 CSD RAM 候选下确有三个重建的硬解会话和非空输出，
不是同会话 DRC。验收功能父版为 v21/原 OMX，CSD 候选尚未持久化；旧 EBD tag0 歧义保留。
本轮用户另行授权排查无声、修复播放页/主页整屏黑闪，不自动恢复其他整栈开发。

黑闪原采集三次均为 DRM EBUSY→HWC 错误 Flush→唯一全屏 plane 被撤→下一帧恢复；
没有 eDP/CRTC 重启、欠载或超时。修复仅在 kernel-managed DPU 的真实非阻塞 EBUSY 时，
同一请求改为阻塞重提一次；实际结果/fence 完成一次，其他失败照常传播，真实空画面仍清理。
实际代码 12 场景 ASan/UBSan、旧版红例通过；42TU 配套构建。RAM v2 真机 fence/30 帧所有权/
真实 empty flush/协议恢复通过，页面采集 1601 次提交、零 plane disable/显示超时，用户确认“不会了”。
信息日志受 QTI tag 开关限制，本轮不能从无重提日志断言重提次数。恢复原库后启动期再见 EBUSY。
v1 因 runner 的 set-e/pidof 空返回提前退出，fence 通过但未进入页面验收；v2 已修采集器且收尾0。

无声重启音频服务无效；完整停 HAL 后 WSA 成对 runtime 周期也无效，未重复该路线。
原声道/音量正常，PCM SETUP，SoundWire prepare -61。被动跟踪 40833 次读证明 FIFO 空、读取成功，
但缓存 wr_fifo_depth=0。匹配 ABI 的只读能力探针 16 次均读到 params016840c6、实际 wr/rd16，
缓存仍0；探针仅读 COMP_PARAMS，未重置主端/ADSP或改驱动内存，返回 ECANCELED 后无驻留。
源码 AHB 地址/数据两步缺整体互斥，IRQ 已注册后才缓存容量；竞态可造成错误能力值是待实机验证的解释。
候选补 AHB mutex，并在 IRQ 注册前读取/校验真实容量，恢复时不覆盖固定容量，不填假16、不吞错误。
实际 read/write 并发旧红例、新绿例、四错误解锁路径通过；匹配6.18严格MODPOST完成。
已通过正常重启应用模块，禁止热卸提供者、裸写容量或复位 ADSP。新候选正常重启后用户已确认听音恢复。

证据仅 ignored out/qualcomm-full-stack-20261108：bili-page-blackout-v2、dpu-busy-commit-v1/v2、
audio-blackout-20261109-v1/fifo-read-capture-v2、cap-read-module、soundwire-ahb-capacity-v1。
音频 HAL774cd7e7…及原停流静音/增益/配对端口保持；不恢复旧 HAL pacing/MonoPipe 改动。
v21 默认71db2317…已部署并完成一次正常重启，boot364dc453…；唯一回退5b9f7012…保持。
匹配 SoundWire4671cc9d…/note f9dd9313…、显示e4f0e12a…实读核对；原音频HAL和OMX保持。
等待120秒后首次SSH成功，Android/user10/原服务正常；缓存wr/rd16与16次真实能力读一致，
本次启动无旧FIFO溢出/prepare -61；用户对新默认包Bili听音/页面切换反馈“全部正常”。
无新备份、无提交推送。
两次4秒/各192000帧 AAudio 标准媒体流均成功排空；间隔10秒重开通过，实际PCM RUNNING
各29个样本且hw_ptr持续前进，AAudio XRUN0，无原FIFO溢出/prepare -61。重启后只读显示采集
234提交、零撤层/欠载/超时，收尾0/无丢失；本段是受控输出和空闲UI观察，不能冒充Bili听音/页面复测。
用户随后确认持久v21“全部正常”：内置扬声器恢复、Bili播放页/主页无整屏黑闪。反馈晚于采集收尾，
不伪造精确时间对齐；受控PCM与RAM页面证据分别保留。所有临时trace/挂载已清理。
本轮两项修复完成；整体显示栈目标继续暂停，未新增长时/休眠/跨设备声学验收。
full_stack_complete/full_hwc_verified/full_fence_lifecycle_verified 仍 false。

## 2026-11-09：HEVC 独立 CSD 已通过普通 MediaCodec 基本组，继续实用场景验收

执行路线继续 mapper5 + 原厂 gralloc + 源码 QTI HWC/SDM + Linux 6.18。
当前仍是正常 v20（boot 1566c477…、默认 a65968ee…、唯一回退 5b9f7012…）；未写 EFI、未重启。
源码 SM8250 OMX 候选 d8699271…在有界 RAM 单库绑定中，将独立参数头复制到组件私有存储，
再放入首个完整 Annex-B AU 的既有 DMA 输入槽；保留图片 PTS/flags，不合成 SEQ/FLUSH/END。
普通 MediaCodec 五阶段全部独立通过：原完整 AU Surface、候选完整 AU Surface、剥头 CSD
ByteBuffer、剥头 CSD Surface、独立 CSD + 重复头 ByteBuffer；合计 30 帧像素/PTS 精确，
EOS/stop/delete 均成功。真实 HFI 首包分别为 5161/5244 字节，没有孤立 83 字节参数包；
全部实际 END、传输归还及最终错误/实例/vb2_bytes=0，原五文件与日志属性已恢复，七服务 running。
Surface 是 AImageReader，不是本次物理屏像素验收；仍需真实 Bili 4K→1080p→4K、HEVC DRC、
重复 flush/reset、五组件 HDR 联合与更宽输入契约验证。Main10 继续过滤，候选未持久部署，full_* false。

原始 v1 因 map_files 在能力探测 dlclose 时消失而中止；前三组 18 帧实际精确，但 runner1，
后两组未运行，不能记全通过。v2 只对同 PID 下已消失映射作有界重试，错误 SHA/存活 I/O 错误仍拒绝；
27 项采集器故障注入通过，候选、向量与客户端字节未变。独立像素/HFI审计保留 v1 反例。
此前直接 V4L2 重组 AU 对照也真实通过 6+6 帧；early-DPB 阴性结论不变，未作为正常修复引入。
证据：`out/qualcomm-full-stack-20261108/omx-hevc-csd-prefix-v1/independent-basic-v2-pixels.json`、
`independent-basic-v2-hfi.json`；直接对照见 `hevc-csd-assembled-au-control-v1/independent-control-audit.json`。
私有产物/原始日志仍仅 ignored out；不提交推送。音频 HAL 774cd7e7…保持，原听感修复不回退。

## 2026-11-09 当前状态：首 SEQ 对照阴性，已正常返回 v20

当前 boot 为 1566c477-1442-4ac5-91d5-5d3ead54575f，实际 v20 core/dec note、原五组件和
音频 HAL 774cd7e7…均核对；错误/实例/vb2_bytes 为 0，early-DPB 诊断参数已不存在。
默认 SQ2Nat 为原 v20 a65968ee…，唯一 SQ2QDbg 回退 5b9f7012…保持，无新增启动项/备份。
恢复期间 SSH 观察曾超时，但实际 EFI 恢复脚本成功，核对后才正常重启，未重复写入/重启。
两次启动后的入站网络均在设备反向 ping 电脑后恢复；路由/ARP sysctl/SSH 监听和网络载荷
未发现错误，不能把该关联当根因或修改正常网络策略。详见 network-arp-after-reverse-ping-v1。

真实四组 first-SEQ 对照 runner=1、cleanup=0，测量可用但不是功能通过：关闭/开启 early-DPB
时完整 AU 均产生有效 SEQ，原顺序 90B 独立参数头均等 12 秒超时。开启时各 18 个真实 DPB
确实在 START 成功后、首 ETB 前成功提交，仍不足以修复 CSD。split 的 raw SEQ 只在收尾期
出现，SID 属于本会话但回调已关闭；不得计为等待期首序列成功。全部真实 END/最终资源归零。
两条 EBD 的未用范围字段异常原样保留，整体协议审计 false；实际参数 ABI/同步 packet 生命周期
已核对，INPUT 回调不使用这些范围字段，不能据此宣称越界 DMA 或固件根因。
证据：`hevc-csd-early-dpb-v1/{independent-first-sequence-audit.json,INDEPENDENT_CAPTURE_REVIEW.md}`。

独立修复了原生成器 DPB extradata-size 错误分支重复 kfree：共同 fail 标签负责一次释放。
已补精确增量 --dpb-cleanup-only，实际旧红例/新 ASan/UBSan 绿例和 4 项测试通过；只修主机源码，
正常候选 376ae4e0…已严格构建/打包，独立解析核对仅两份 core 和清单变化，其余 1299 记录保持；
尚未部署，不是 CSD 修复，也不包含 early-DPB 诊断模式。
下一步核对原厂源码现有 HEVC arbitrary_bytes 组帧路径：正常下游 ETB 并不发送 ENDOFFRAME，
不能盲加该标志；评估真实参数头与首 VCL 组合及输入所有权/flush/DRC/EOS契约。
Bili 真实 4K 硬解切换仍必修、Main10 继续过滤，完整整栈目标及 full_* false 保持。

## 2026-11-09：v20 五组件 HDR 联合回归完成，继续 HEVC 首序列定位

默认仍为 v20-capture-lifecycle（a65968ee…），唯一 SQ2QDbg 回退保持。完整五组件 RAM 候选
在同一 boot 上完成：源码 OMX、原生 P010 service/bridge、stagefright 与 renderer 联合回归。
六项独立审计通过：同流 HDR10→SDR→HLG 30 帧、独立 HDR10/HLG/AVC 42 帧、
正常组 150 帧及 BT709 60 帧，共 282 帧像素/PTS 精确；同流后 20 帧静态 HDR 正确清空。
AVC SDR 320×240→1920×1080→320×240 完成 30 帧；物理窗口合计 150 输出/回调，
多平面、缩放与 UI 遮挡通过。这不是严格 DPU 像素、HDR 分辨率切换或 4K 硬解验收。
旧 bridge mixed 日志未记录，不能沿用其日志交叠结论；独立 HFI 另证最大并发 2。
HFI 4269 条事件覆盖 80 个真实成功 END 世代（64 能力探测、16 解码）和 432 非空输出，
全部传输及内部缓冲归还闭合，旧 DPB 在下一 CONTINUE 前归还；同步 flush 0.169–1.112ms。
45 个 HEVC EBD 的未用 filled 字段异常、7 次 EOS 后空 OPB 入队/flush 归还单列保留；
不据这些未用字段推断越界 DMA，也不把 quarantine 函数入口当真实隔离。

两组件 DRC 对照也已独立通过：各 30 精确帧、真实 FTB/FBD 全归还，flush 0.163–0.329ms，
无原 1 秒锁阻塞。历史 joint-v1 固件 fatal 和 v19 DMA 残留证据保留，当前通过不能反证其根因。
完整联合 runner/cleanup 均 0，原五文件 SHA 已恢复、七服务 running，错误/实例/vb2_bytes 全 0；
24 个 trace 丢失计数全 0，全局 tracing 设置未变。未持久部署五组件，Main10 仍过滤。

用户复测“没有卡顿”记录保持；该次 Bili 采集显示 hard→soft，真实硬解清晰度切换仍为必修项。
HEVC 独立 CSD 尚无首 SEQ。匹配下游源码显示 START_DONE 后先排内部 DPB、再交首 ETB；
当前等 SEQ 才分配 DPB。下一有界实验只对照是否产生真实首 SEQ，并以真实 END 收尾，
不伪造 SEQ、不把参数开关或首序列通过称为解码修复；诊断候选尚在本地准备。
路线及 KGSL/msm_vidc/QTI C2/PM/性能/SQ1-SQ2 目标保持，全部 full_* 仍 false。
证据：`source-omx-hdr-joint-v2/independent-summary.json`、`native-p010-drc-trace-v1/independent-hfi-summary.json`，
均位于 ignored `out/qualcomm-full-stack-20261108`。源码手册尚未提交推送，私有资料不上传。

## 2026-11-09：v20 正常启动与 capture 生命周期修复已验收

当前默认qti-system-display-v20-capture-lifecycle a65968ee…，boot1faec9eb-96e7-4885-8e51-5b01569fceec。
匹配corecf3d061d…/dece02fc528…，唯一SQ2QDbg回退5b9f7012…保持；原Image/固件/音频/OMX/桥/显示未改。
120s等待后SSH成功，模块note和原显示/音频组件核对通过。路线仍mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18。

修复SQ2 decoder的capture QBUF早于实际启动而提交固件：新队列先保留，真实capture开始时提交。
独立原v18对照有18次preSTART FTB/18次FW state错误，v20实际同流两者均0，6帧像素/PTS精确。
只加gate的v19曾在整组结束残留73211904字节，失败保留；v20配套回收证明未曾提交的VB2 lease，
静态SET_BUFFERS发布前补scope绑定，已发布DMA仍等真实END/PAS，不改通用隔离规则。
实际源码ASan/UBSan、旧版红例、14启动/6静态注册场景及五地址发布点审计通过，严格MODPOST完成。

v20正常联合16客户端/150精确帧/120物理输出回调与额外BT70960精确帧/30物理回调，四独立审计0；
各关闭阶段及最终Venus错误/实例/vb2_bytes均0。不是严格DPU像素或整栈完成证明。
正常与仅P010候选service/bridge的DRC对照各30帧返回0，详细像素/真实HFI独立审计及源码OMX/HDR完整联合继续。
HEVC独立CSD仍未修：直接7槽、无early capture、原顺序90B参数包也0SEQ；完整AU通过。
用户“没有卡顿”仍对应已观察到的软解回退，不能关闭真实硬解清晰度切换或历史DRC fatal问题。
内屏SDR，Main10继续过滤，full_*false；KGSL/下游msm_vidc/QTI C2/PM/性能/跨SQ1-SQ2目标不缩减。
证据native-capture-start-v2、qti-system-display-v20-acceptance-v1、native-p010-drc-trace-v1；复建见BUILD。
本轮源码手册未提交推送，私有库/凭据/固件/原始记录只在ignored out。

## 2026-11-09 当前任务：必须解决动态分辨率／清晰度切换故障

用户在默认系统实际执行4K→1080p→4K后确认播放卡顿。动态分辨率／清晰度切换故障
恢复到之前的优先级，是当前必须解决的执行项；修复和验收必须包含此项，不能只修HDR元数据后结束。
以实际Bilibili清晰度切换为主线；先核对当前boot/实际加载组件、VPU/flush/重配置与音视频时间线，
区分正常v18卡顿和新P010候选固件错误，禁止把二者未经证据合并为同一根因。

本轮源码SM8250 OMX及SurfaceUtils两层缓存修复在RAM候选中已通过同流HDR10→SDR→HLG30帧
和独立HDR10/HLG12帧+AVC30帧像素/PTS/metadata审计；同流用例固定320×240，不证明HDR分辨率切换。
完整联合随后在AVC SDR的320×240→1920×1080→320×240过程中输出13帧后失败，
固件报video_decoder_utils.c:4394 Err_Fatal。7图形和前4解码客户端返回0，未到物理窗口/BT709阶段。
候选五文件已撤销绑定，未持久部署，Main10仍过滤。此前一次正常重启134s读回boot34d0c43a…；
用户随后恢复连接，本次读回boot2fe924fb-3878-4806-ae14-7b6c3f6ca6ea、6.18.60-sq2-android1。
正常五组件/音频HAL/内核note均匹配，七服务running，无候选绑定，Venus错误/实例/缓冲0。
用户本轮反馈没有卡顿；真实采集却显示HEVC硬解无非空输出后hard→soft，不能作为硬解验收。
固定六帧完整AU通过，独立HEVC参数头分包零帧（重复头/去CODECCONFIG也未解决）；
当前主线同时解决HEVC首帧与候选DRC错误，详细对照及恢复核验见HANDOFF最新节和hevc-csd-v18-v1。
证据out/qualcomm-full-stack-20261108/{source-omx-hdr-sequence-v2,source-omx-hdr-joint-v1,bilibilihd-quality-switch-user-v1}。

## 2026-11-09：v18 修复 Bili 切换时的 FLUSH/SEQ_CHANGED 锁阻塞

默认已正常启动qti-system-display-v18-flush-lock caeb6a4b…，boot659812ff…；
唯一SQ2QDbg回退保持，音频HAL774cd7e7…/OMX/桥/mapper/HWC/Image/固件未改。
Bili真实trace三次FLUSH_ALL在SEQ_CHANGED后阻塞1秒，成功回包只能在超时后处理。
同一直接V4L2码流复现两次，vdec_event_change入口/返回证明IRQ回调卡住；
v18同一客户端五组均0，真实成功回包先于return，耗时0.235–0.550ms，
两组在flush等待期间完成sequence回调（10/11us）。这只修已证实的私有OMX flush锁依赖。

私有flush持有ctx_q_lock串行队列，等待时释放inst.lock，回调可更新真实sequence；
DRC与显式输出/ALL flush不重复提交共享completion，输入-only则延后发送真实输出flush。
无成功回包、负回包、session/core错误不返还软件队列，END/隔离保护保持。
struct新增字段必须重编完整Venus模块组，不热换resident core；源码适配在queue-lock之后执行。
Bili另捕获一次物理输出skip确认4096帧却未传输、一次PCM XRUN，二者不同时；
用户随后明确“听音也确认修复了”，记录为本轮听感恢复。HAL并未修改，不能把旧pacing/
MonoPipe实验重新套回或宣称音频skip根因消除。无新复现先完成v18整组回归并继续原整栈目标。
正常联合16客户端/150精确帧/120物理输出回调及额外BT70960帧/30物理已通过，四独立审计0。
最终七服务健康、Venus错误/实例/缓冲0，无试验探针/实例/进程；用户wake lock1保留。Main10仍过滤，full_*false。

## 2026-11-09：v17 正常启动，修正真实成功 END 被旧会话错误否决

当前默认已是qti-system-display-v17-session-end ae39c924…，boot f5a7bd80…；
唯一SQ2QDbg回退5b9f7012…保持，没有新启动项/备份。核心模块709e2d42…实际note匹配。
本组只改Venus成功END确认条件；原OMX7a6aa750…/桥2a120b31…/mapper/HWC/Image/固件不变。
下方v16仍是历史；正常联合已通过：16客户端/150精确解码帧/120物理输出回调，额外BT70960精确帧/30物理输出回调；见HANDOFF。

真实损坏AVC负例在原v16捕获：prior session_error1/core_error0，实际16字节
HFI SESSION_END reply_error0，但deinit返回-EIO/confirmed0，随后quarantine。
源码不再用旧session_error否决独立成功END，保留旧错误给客户端；超时/错误回包/
核心错误仍拒绝确认，不放宽隔离、不清错误、不重试PAS。

新v17同一负例已捕获prior1/reply_error0/return0/confirmed1，core_error0且DMA归零。
不过负例客户端仍timeout124，OEM逐buffer free等待使10秒收尾观察失败；失败保留。
之后同一OMX PID5571自然回到insts0/vb2_bytes0，没有服务重启/provider重载。
不能称完整错误恢复/历史DRC或PM超时根因已修；同boot正常16客户端/BT709联合回归已完成，四独立审计通过。

原v16另有180帧（5个DRC+普通）像素/PTS精确、30个真实成功END回包基线。
20个实际源码END场景ASan/UBSan、12个实际close场景、严格MODPOST与depmod通过；
新增trace审计及反例覆盖真实回包配对，不能把quarantine函数入口当作实际隔离。
全部full_*仍false。Main10仍过滤；内屏SDR/真实HDR→SDR、同流HDR缓存、
严格DPU像素及KGSL/msm_vidc/QTI C2/PM/性能/SQ1-SQ2原目标继续。
源码手册尚未提交推送，私有产物/原始日志仅ignored out。

## 2026-11-09：物理 P010/灰阶通过，候选联合 DRC 失败已正常重启恢复

路线保持mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18。用户确认内屏无HDR；
内屏验收SDR输出和HDR→SDR映射，外接HDR按实际sink/link，不要求内屏HDR直显。
正常v16-bounds c967ade0…和唯一SQ2QDbg回退5b9f7012…保持，Main10仍过滤，候选未持久部署。
物理HDR10/HLG/BT2020 SDR Main10及AVC各30输出/定时release/回调（120合计）通过；
P010最终CLIENT→SDR RGBA_8888_UBWC/0x08810000/KMS AB24，AVC仍DEVICE/NV12。
另90帧lossless中性灰按实际10bit码值对照709、HLG和PQ≤名义300nit标准函数，误差<1个8bit级，
灰阶单调、RGB差/平坦抖动最大1。500nit仅SF软件fallback，非面板实测/HDR能力。
四组--require-dpu全部exit1（含AVC）；证据为色彩保持PNG/SF/HWC/KMS/CRC，
严格DPU像素、面板色准、PQ高光精确曲线、性能/时延仍未验。不能把回调当物理present。
最新桥修复10→8→10的隐式格式重协商：G_FMT规范化后与上一layout格式不同时，
先真实unpadded S_FMT，再padding；fixture区分reported/applied格式并保留busy/失败恢复。
同流30帧像素/PTS、真实AHB P010→NV12→P010和PQ→SDR→HLG VUI/dataspace精确；
但第10..29帧继承首段mastering/CLL，严格metadata审计exit1，整体不通过。
raw native记录证明首帧有0x15(40B)/0x16(8B)/VUI(56B)，之后29帧只有VUI，epoch1/2/3正确。
匹配OEM源码HEVC优先internal缓存，SEI handler只在有record时更新；空SetConfig只改client，
不能清旧internal。不能按“每帧无SEI”清空合法CVS持久状态、合成空FW record或只改mapper掩盖。
联合候选joint-v1的7图形/5解码阶段0，诊断脚本尺寸初始化误插入cleanup，物理阶段未启动；
修正并用真实SF fixture检验后，joint-v2的7图形/4解码阶段0，1080p DRC九帧后timeout124。
Venus报release-end/close-power/close-end END unconfirmed并quarantine，
sys_error1、insts0、vb2_bytes17698560；未到物理阶段，不能记新联合成功。
已取回全部失败/dmesg，撤销绑定，正常service7a6aa750…/bridge2a120b31…恢复；
坏固件准入未恢复时没有继续解码，没有热卸provider/清隔离/重复PAS。
已一次正常重启，新boot fe5f1239-2837-4059-9df2-f45a9772e9b4，内核6.18.60-sq2-android1，
七服务running，原service/bridge哈希正确，Venus错误/实例/vb2_bytes全0。
首次SSH观测在请求后274秒（此前做本地工作，不是启动耗时）；未写EFI/部署新包。
下一步相同DRC输入的正常基线/最新候选对照，记录在native-p010-drc-compare-v1。
Main10能力声明/持久部署须等DRC和同流HDR状态修复、完整联合验收后再推进。
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete仍false；
继续严格DPU像素/fence实际故障、KGSL/下游msm_vidc/QTI C2、PM/性能与SQ1/SQ2目标。
源码手册尚未提交推送，私有库/固件/凭据/原图/原始日志仅ignored out。

## 2026-11-09：原生 P010/HDR10/HLG Surface 候选已实测，正常包仍 v16

主线保持mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18。内屏仅SDR，
下一步必须验证物理HDR→SDR；不能把ImageReader通过当成面板tone-map或整栈完成。

新增成对OMX/native-layout/bridge候选已经在真实init服务RAM绑定验证：
HDR10和HLG各6帧为原生P010 AHB，直接来自VPU，像素/PTS对当场FFmpeg参考逐字精确；
实际AHB dataspace为0x11c60000/0x12060000，HDR10真实mastering/CLL/FALL完整，
HLG与随后30帧AVC的静态HDR均为空。AVC30帧像素/PTS及EOS/stop/delete也通过。
这是同一服务生命周期的不同decoder节点，不是同流HDR/SDR切换或物理显示验收。
Main10 profiles仍过滤，未广告/持久部署该候选；正常v16与唯一回退保持，无重启/EFI改动。

修复P010字节stride与ANW像素stride的区别；格式变化先真实S_FMT选格式，再协商padding，
按实际kernel minimum分配，不再复用NV12布局。v2回调保留原ABI；packer按桥实际v2引用
验证成对服务，旧包仍可重建。读回复用实际mapper5布局及同一AHB锁，不在产品路径做像素转换。
65项相关主机测试、实际布局ASan/UBSan和两项独立审计负例通过，失败试验保留。

同尺寸SPS变色另已捕获HFI事件固定头：两组仅各1个初始化SEQ_CHANGED，真实DRC为3个，
与driver回调逐一对应；69帧像素/PTS正确。尚未捕获全HFI属性payload，不泛化固件能力。
证据native-p010-surface-v9/与vui-hfi-event-v1/，详见HANDOFF/BUILD。
最终七服务正常，OMX2308、正常桥恢复，无试验绑定/进程/kprobe/trace实例，
Venus错误/insts/vb2_bytes为0；无密码用户锁屏已跳过，最后Awake、总wake locks=0。
full_*仍false。继续物理Main10/HDR→SDR、同流切换、正常联合回归/持久部署，
以及严格DPU像素/fence故障、KGSL/msm_vidc/QTI C2、PM/性能和SQ1/SQ2。
源码手册尚未提交推送；私有产物和原始记录仅ignored out。

## 2026-11-09：内屏固定 SDR，流内 VUI 的重配置边界已区分

用户确认内屏没有HDR能力：按SDR输出和真实HDR→SDR tone-map验收，不要求内屏HDR直显；
外接HDR仅按实际sink/link能力。路线仍为mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18。

同尺寸601→709→601在SPS/PPS id复用或分离、OMX或直接V4L2四种组合中，
真实native VUI均保持首段601；额外提交三组真实CODEC_CONFIG也不改变结果。
同尺寸OMX两组及额外配置两组各30帧像素/PTS正确。直接V4L2两组各30帧CRC/PTS、
EOS/关闭通过；原首轮诊断完成mask硬编码63的ret1保留，已修工具，不算硬件失败。

真实320×240→640×480→320×240同时变色时，9帧native VUI与SDK正确601→709→601，
逐字像素及PTS通过。随后同一服务生命周期的独立function trace对照69帧再次通过：
同尺寸两组各1次vdec_event_change（仅初始化），DRC组3次。无trace丢失，全局设置保持。
这是driver回调证据，不是raw HFI包捕获；不能断言固件普遍不支持，不能在桥合成VUI冒充修复。
DRC-v1在创建codec前因错误pacing参数exit64的反例保留，v2修正实验输入后通过。

下一步比较颜色变化时实际HFI事件/属性和匹配下游配置；不再盲试SPS编号或重复CSD。
其他能力目标仍包括Main10/HDR→SDR、严格DPU像素、fence故障、KGSL/msm_vidc/QTI C2、
PM/性能和SQ1/SQ2；不要求不支持HDR的内屏直出HDR。full_*完成标志保持false。
正常v16-bounds与唯一回退保持，未部署/重启/修改EFI；正常服务/桥已恢复，七服务健康，
无试验进程/绑定/trace实例，Venus错误/insts/vb2_bytes为0，用户wake lock保留。
证据和复跑入口见HANDOFF/BUILD；源码手册尚未提交推送，私有原始资料仅ignored out。

## 2026-11-09：v16 已正常启动，修复 FMQ fence 类型与短命令边界

用户已确认内屏不支持HDR：内屏按SDR验收，HDR内容验证解码/元数据和HDR→SDR映射；HDR直出仅按外接sink实际能力启用与验收。

执行主线继续mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18。
默认SQ2Nat为v16-bounds（c967ade0…），boot1ad3305b-af45-407b-bf17-96e14c3ad2b5；
唯一SQ2QDbg回退5b9f7012…保持，无新增备份/入口。首次SSH观测195.5秒，不是启动耗时。
完整46TU composer-v22配原42TU display-v20，服务b4a30a34…；mapper/OMX/桥/msm保持。

新patch sm8350-composer-acquire-fence.patch在共享FMQ reader验证真实sync_file类型，
原非sync FD被接受的反例已保留。三入口错误前不改cache/HAL，保留合法负状态、
EMPTY/0FD兼容与资源错误，移除dup失败的无界同步等待；补client-target length0拒绝。
66个实际源码FD/短命令主机场景修复0不匹配，相关34项检查通过。
实机受控负例使用标准kernel sw_sync，明确不是GPU/VPU生产者故障；
四情形/30缓冲归属帧/12FMQ异常及3短命令恢复通过，详情HANDOFF最新节。
诊断provider未热卸、未打包，已由正常重启清除，当前实际不存在。

正式GPU30帧/27缓存复用、29个pending GPU在DPU wait内signal及继承平面交接通过。
正常16客户端/150精确解码帧、4物理窗口120输出/回调及自动BT709另3客户端通过。
两路60个非空硬件输出完成配对完整、交叠778ms/17切换；零字节尾记录配对缺失保留，
全trace完整仍false，不把完成区间交叠当吞吐或同时硬件执行。

七服务健康，无试验进程/挂载/模块，Venus错误/insts/vb2_bytes=0，无密码用户锁屏已跳过，
用户wake lock保留。所有full_*完成标志仍false。继续SPS/HFI、真实GPU/VPU错误/超时/
多点fence、严格DPU像素、HDR/secure/videoUBWC、KGSL/下游msm_vidc/C2、PM及SQ1/SQ2。
源码手册未提交推送；私有二进制/固件/凭据/原始记录仅ignored out。

## 2026-11-09：v15 自动 BT709 VUI 已正常启动验收，继续全栈目标

主线固定mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18，不重新比较路线。
默认SQ2Nat v15 SHA029a1a8a…，boot f40274da-2b07-491e-bc0c-4002c9065b47；
正常重启首次SSH151.2秒，唯一SQ2QDbg回退5b9f7012…保持，无新备份/入口。
正常bridge启用已有native-extra1，SHA2a120b31…；service仍7a6aa750…。
QTI composer init rc关闭实际SF treat_170m_as_sRGB覆盖，未更改OMX颜色结果、
mapper转换或HWC转换表；无效的现代dataspace指导候选未采用，仅out保存反例。

正式启动后未放宽的BT709审计通过：SDK/像素/PTS、物理HWC0x10c10000 DEVICE和
KMS709 limited一致；normal-v8的16客户端/150精确解码帧、4窗口120输出/回调、
多NV12缩放/UI遮挡/回收三个独立审计及52项相关主机检查通过。细节见HANDOFF/BUILD。
七服务健康，无试验进程/挂载，Venus错误/insts/vb2_bytes=0；原无密码用户锁屏已跳过。

SPS-only流内颜色切换仍未验收：输入601→709→601各10帧，native extradata保持首段；
未隔离HFI/驱动配置与固件，禁止桥合成VUI冒充修复。v7-vui的退窗CRC失败仍保留，
v15单分钟时钟对照重验通过，不删旧失败或放宽审计。
strict DPU pixels、fence错误、HDR/secure/videoUBWC、KGSL/下游msm_vidc/QTI C2、
PM/性能和跨SQ1/SQ2待推进；所有full_*能力完成标志保持false。
源码手册尚未提交推送，私有库/固件/凭据/原始记录仅ignored out。

## 2026-11-09：v14 内核 SDR 色彩状态与真实 VPU/HWC 归属

继续 mapper5 + 源码 QTI HWC/SDM + Linux6.18 + 原厂 gralloc 主线。
vpu-hwc-v2的60帧BT601及新v14的60帧显式BT709均已验证真实DMA-BUF/AHB、
QBUF/DQBUF→HWC前一帧release→refill/free，含物理1080p DRC；不是假acquire fence。
源码/审计入口和证据详见docs/HANDOFF.md最新节。

v14-color已部署并正常重启，包6e75d8c8…、boot8e0e0b08-549f-42b0-aa6d-9878e6ef898a，
首次SSH在请求后145.3秒；实际msm build-id与f3ba9cf7…模块配套，display/composer-v20
为42/46TU。新增标准颜色属性仅真实YUV格式平面可用，601/709 limited实际选择CSC，
默认601保持。唯一SQ2QDbg回退、凭据与其他组件保持，没有新备份/启动项。

必须区分已验的显式BT709直显与未解决的OMX自动VUI：真实BT709码流仍被原厂
native OMX/ACodec报告为BT601，ByteBuffer未报color元数据。不得硬编码709冒充传播修复。
旧失败sdr-color-v1/v2保留。正常联合回归状态以最新HANDOFF为准。
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete仍false，继续全栈目标。

v14正常normal-v6的16客户端/150精确解码帧、4物理窗口120输出/回调以及多层缩放/
UI遮挡和撤窗恢复均已由三独立审计通过。最终七服务running，无实验进程/挂载，
Venus错误/insts/vb2_bytes=0，用户无密码锁屏已跳过，其他用户设置/凭据保持。
源码/手册尚未提交推送，原始日志/私有库仅ignored out。

## 2026-11-09：真实 GPU acquire/UBWC 缓冲归属通过，修复继承平面交接

以已部署正常v12及真实NV12 release/FMQ修复为基线，整体目标仍进行中。
继续既定mapper5 + 源码QTI HWC/SDM + Linux6.18 + 原厂gralloc，不重新比较mapper路线。

新增GPU生产者直接HIDL诊断：3个真实AHB RGBA UBWC target，按运行时物理分辨率
2880×1920各渲染10次，共30帧、27次缓存复用。GPU直接写入用于client target的同一AHB，
EGL导出真实native sync_file后立即交HWC，不先在CPU等待该acquire；GPU读回逐像素
验证放在物理present fence完成后。合计165,888,000个RGBA像素精确，是GPU内存读回，
不作为DPU严格像素或面板端到端时延证明。

每帧日志记录AHB ID/slot与导出、信号状态；独立tracefs实例记录同一生产者的render/
export/present标记。以实际display commit wait的context/seqno，关联该帧render区间
创建的唯一对应GPU scheduler fence，避免只按FD、anon-inode或相近时间猜身份。
两轮均30个acquire导出时pending，30个内核display wait；实际在wait期间GPU发信号
分别29/27次，其余为已完成后进入等待，不强称每次都阻塞。
调用栈含drm_atomic_helper_wait_for_fences→commit_tail→commit_work。
随后逐帧关联DPU release到下一target替换，重用前已完成；末帧由empty flush撤下。
两轮全局tracing设置保持、所有CPU overrun/commit overrun/dropped events为0，
原始trace统计分别1787与1786项。未注入fence错误。

首轮正常v12实际KMS仍多出一个未由当前SDM提交的XR24/fbcon平面，虽然被不透明
全屏GPU target覆盖，不能写成干净交接。根因是DRMPlane::InitAndParse将assigned CRTC
置0，丢掉上个master/fbcon的实际绑定，既有UnsetUnusedResources无法找到它。
新sm8350-dpu-inherited-planes.patch仅对无下游CAPABILITIES的标准DPU路径，采用
drmModeGetPlane提供的真实CRTC作为初始assigned状态，复用原有TEST_ONLY/commit/
失败重试/按CRTC清理。未按plane号、fbcon名字、样机或CRTC编号写特判；
主机实际源码方法验证解析失败、legacy不变、其他CRTC保留、TEST_ONLY不改归属、
失败commit重试、未请求平面撤下及被请求平面保留。硬件调用为注入，不冒充实机失败注入。

匹配display-v19/composer-v19（42/46TU）在原init服务RAM成对绑定实测：
30帧GPU/acquire/复用继续通过，目标CRTC仅预期AB24 UBWC client target；
原继承平面已撤下，末帧flush和原服务/库/用户恢复完成。
独立GPU审计与负例、实际源码和打包相关39项测试通过。
证据：out/qualcomm-full-stack-20261108/composer-gpu-acquire-v1与v2/
independent-gpu-audit.json；v1按显式baseline保留inherited_plane_handoff=false，
v2严格验收且核对实际service/成对库hash，未用允许遗留选项放宽修复验收。

qti-system-display-v13已部署为默认SQ2Nat并完成正常重启，SHA256
c9e2eedae75c80b7e7d2662e5e685da23d2f94989432f64fedb788981a62a34f；
boot d0af5276-5ab5-420e-8e66-efe44b5daf29，请求后第140秒首次SSH观测成功，
仍为6.18.60-sq2-android1。62显示文件/13链接/17HIDL核验通过；
实际composer进程67ed8868…、libsdedrm7ab1e17a…、libsdmutilsc33e0d21…匹配成对构建。
在任何视频测试占用VIG前，独立boot-drm审计仅有预期AB24 UBWC client target，
证实正常启动也完成继承平面交接。唯一SQ2QDbg回退5b9f7012…保持；
没有新增备份或入口，5显示ELF+2校验清单变更、1295其他有效记录和凭据尾保持。

正常v13的qti-system-display-normal-v5一次连续16客户端单项及runner均exit0：
7图形、5解码、4物理窗口。150帧可见解码像素/PTS对新生成FFmpeg参考精确，
含320×240→1920×1080→320×240真实DRC；paced mixed实际输出交叠793ms、
17次fd切换，不作为吞吐或端到端时延。原生NV12 DEVICE、多视频2x/0.5x、
UI遮挡A/B/A及撤窗平面/CRC恢复由三个独立审计通过，4物理窗口120输出/回调。
最终同boot、OMX PID5288贯穿、六服务running、Awake、用户secure=false、
disabled=true/showing=false，试验holder释放，总wake locks=1（用户应用保留）。
无实验进程/挂载/图层，Venus error/sys_error/hfi_error/insts/vb2_bytes全0，
ESP可用1,243,372KiB，/logs4,180KiB。证据：qti-system-display-deployment-v7/
及qti-system-display-normal-v5/独立审计和final-health.txt，均在本地out。

复跑主机检查：python3 -m unittest tests.test_qti_dpu_inherited_planes
tests.test_sq2_composer_gpu_acquire_audit -v；GPU构建入口
scripts/build-sq2-composer-fence-probe.py，服务端运行run-sq2-composer-fence.sh
TRIAL --gpu-targets。独立审计入口scripts/audit-sq2-composer-gpu-acquire.py，
修复验收必须传成对--composer-build/--display-build，不使用--allow-inherited-plane；
后者仅用于保留首轮反例，不是正常放行条件。完整复跑参数/哈希在各BUILD.json。


逐缓冲VPU/decoder acquire归属、真实fence错误处理、严格DPU像素、物理高分辨率视频/
DRC/更多颜色格式、HDR/secure/videoUBWC、性能/PM、KGSL/下游msm_vidc、
真实QTI C2与跨SQ1/SQ2仍待推进。full_fence_lifecycle_verified/full_hwc_verified/
full_stack_complete保持false；本轮源码/手册尚未提交推送。私有库、截图/trace/raw仅out。

## 2026-11-09：真实 Composer HAL 缓冲所有权与 FMQ 异常契约已闭合一条受控路径

沿用 mapper5 + 源码 QTI HWC/SDM + Linux 6.18 + 原厂 gralloc 主线。
新增标准 HIDL2.4/FMQ 诊断客户端，连接正常 init composer/allocator/Power HAL；
不接管 DRM master，不使用 eventfd 模拟 sync_file。动态 hotplug 发现显示，
先以真实透明 RGB client target 完成 CLIENT 启动帧，再转 NV12 DEVICE。
ClearClientTarget 请求按已清零的目标处理；不放宽合成策略或硬件校验。

HIDL SET_RELEASE_FENCES 属于前一帧，不能把返回 FD 关联到本帧。
原正常服务与修复候选各完成30帧、3个不同 AHB ID、30个逐缓冲 release关联、
27次HAL槽缓存复用；CPU重写前poll及SYNC_IOC_FILE_INFO均确认release已signal，
各点无负error status。末帧先用CLIENT替换取回最后DEVICE的release，再destroy/empty flush。
观察到真实msm-dpu/dpu-video-buffer-release点；FD号和anon-inode不作为唯一fence身份。

实际负例发现并修复 QtiComposerClient.cpp 的两个错误：2.1/2.2/2.3三个execute接口
固定回报NONE，掩盖parse/writeQueue失败；无效长度在parse失败后仍endCommand，
触发底层未消费payload断言并终止服务。新patch在错误处直接返回BAD_PARAMETER，
正常命令再endCommand，三接口回传实际err并保留reset。
原末条无效命令也不再因isEmpty误报成功。主机运行实际parser/三接口方法的24场景，
旧版15个契约不匹配、修复0；FMQ存储/写失败为注入，未称主机模拟完整Binder。
实机12个错误矩阵（3接口×未知末条/未知前缀/截断/错误payload长度）
全部BAD_PARAMETER，且每个之后正常validate/present及真实present fence通过。
未等待异步present的早期恢复诊断曾触发真实EBUSY；保留失败，现按fence等待后继续。

证据：composer-hal-ownership-v5与v8/independent-ownership-audit.json；
v6/v7失败记录保留，v11包未部署并标记superseded。源码patch可重放：
patches/sm8350-composer-command-errors.patch，匹配构建sm8350-composer-v18（46TU）/
sm8350-display-v18（42TU），未直接修改ignored源码冒充保存。
诊断/审计/打包相关26项测试通过。原始记录和私有产物仅out，不上传。

qti-system-display-v12已替换默认SQ2Nat并完成正常重启：SHA256
b435e962b5460dce3ba22b952f7f99567ff9e22e7a0e0f27f78815b81f25555d；
boot ef82f1f3-61f1-46e3-ae78-7a934b13d85b，首次SSH在请求后140秒，内核仍6.18.60-sq2-android1。
实际composer进程SHA c2429eeb49bfa18011155eaa94c1cb4f0e74e45b432e6c35c019ed46c113f42c，
62显示文件/13链接/17HIDL核验通过。仅3显示ELF+2校验清单变更，1297其他有效
记录保持，凭据尾逐字相同；唯一SQ2QDbg回退5b9f7012…保持，没有新增备份或启动项。

正常启动后qti-system-display-normal-v4一次连续联合回归16个客户端单项及外层exit0：
7图形、5解码、4物理窗口客户端。150解码帧可见像素/PTS对新生成FFmpeg参考精确，
DRC320×240→1920×1080→320×240；paced mixed真实输出区间交叠825ms、19次fd切换，
不作为吞吐/时延或硬件同时执行证明。物理NV12 DEVICE、多视频2x/0.5x缩放、
UI遮挡A/B/A和撤窗硬件平面/CRC回收由三个独立审计通过，4窗口共120输出/回调。
最终同boot、OMX PID5476贯穿、六服务running，实验holder释放，无试验进程/挂载/图层，
Awake、总wake locks=1（用户应用保留），Venus error/sys_error/hfi_error/insts/vb2_bytes全0。
动态用户经cmd lock_settings核对secure=false，disabled=true/showing=false。
早期helper误用am is-user-secure的平台差异已修正并实机核验，没有清除凭据。
证据：qti-system-display-deployment-v6/、qti-system-display-normal-v4/三独立审计及final-health/
lockscreen-check.txt，全部位于out/qualcomm-full-stack-20261108。


该受控证明限CPU写入linear NV12 320×240单DEVICE层。逐缓冲GPU/解码acquire归属、
fence错误注入、严格视频DPU像素、更广物理分辨率/DRC/格式、HDR/secure/videoUBWC、
性能/PM、KGSL/下游msm_vidc、真实QTI C2和跨SQ1/SQ2验收仍待完成。
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete仍false。
本轮源码/手册尚未提交推送。

## 2026-11-09：v10 退窗时钟 CRC 对照与真实内核 fence 顺序/等待路径通过

当前仍是正常v10、boot f36892b1-b79f-4562-bf8c-62fa96ec64a0、默认SHA5a753828…；
本组没有更新系统包、EFI或重启。三轮各30帧全屏原生NV12继续DEVICE-only，
无RGB client target，1080连续硬件CRC与撤窗回收通过；原始日志/截图/trace仅本地out。

自然跨分钟对照显示，前后桌面仅1642像素变化，完整差异bbox[110,24,291,49]，
全部在实际StatusBar frame[0,0,2880,68]内；裁剪目视确认时钟26→27及相邻日期排版变化，
其他桌面像素逐字相同。随后用临时RGB图层遮住运行时发现的68高状态栏、
置于全屏视频下方再跨分钟：前后5,529,600桌面RGB像素完全一致；
双LM CRC在视频前保持192扫描，撤窗后原pair精确恢复179扫描，随后移除遮挡回正常桌面。
遮挡期间视频仍为唯一HWC DEVICE/SDE/KMS NV12，未借新增client target规避全设备路径。

这组受控复现将当前现场的内容变化与显示恢复路径区分开。历史最初v10全屏轮缺少
before PNG，保留其左LM变化原始记录，不据新实验追溯证明所有历史差异原因；
严格视频DPU像素仍未验收。临时遮挡、进程及图层均已移除，没有修改状态栏/时间设置。

三轮使用独立tracefs实例，global tracing设置前后一致，所有CPU overrun/
commit overrun/dropped events=0。新 scripts/audit-sq2-dpu-fence-trace.py 按context/seqno
跨driver名称变化关联信号/销毁，而非依赖FD号或render callback；6个解析/负例测试通过。
实际分别观测36/39/35组同任务相邻retire/release创建，所有可比较release信号均处于
下一已观测commit的init→present区间；每轮最后一个standing-scanout release在trace末仍pending。
销毁有限观测分别66/72/64，不把仍持有的signaled引用或末帧pending当成已证泄漏/错误。

第三轮wait_start栈含dma_fence_wait_timeout→drm_atomic_helper_wait_for_fences→
commit_tail→commit_work，4个真实GPU scheduler fence在显示commit等待期间发信号后结束等待。
这证明实际kernel acquire等待路径；仍不提供每个HAL帧/缓冲身份映射或fence error status，
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete保持false。
下一步需用真实composer HAL ownership/reuse与release/present接口闭合逐缓冲契约，
并继续高分辨率/物理DRC、HDR/secure/videoUBWC、性能/PM、KGSL/msm_vidc/C2与跨设备目标。

最终同boot、六正常服务running，唤醒holder释放，总wake locks=1（用户应用保留），
Venus error/sys_error/hfi_error/insts/vb2_bytes全0；无试验进程、挂载、图层或trace实例残留。
证据：out/qualcomm-full-stack-20261108/display-sync-discovery-v1/result.json；
display-sync-clock-v1及display-sync-clock-mask-v1/independent-clock-audit.json；
三轮各自independent-fence-audit.json、independent-fullscreen-audit.json和final-health.txt。
本组新增诊断源码与手册尚未提交推送。

## 2026-11-09：v10 正常默认已实测多 NV12、缩放、UI 遮挡和全设备扫描

qti-system-display-v10已部署并正常启动，SHA256
5a753828ea820a55a29b1480a890bfd08a17e8479f2938d2b1488b5d27b1d110；
boot f36892b1-b79f-4562-bf8c-62fa96ec64a0，144秒后首次SSH成功。
62显示文件/13链接/17HIDL核验通过，唯一SQ2QDbg回退5b9f7012…保持。
无密码用户disabled=true/showing=false，没有新增备份或清用户数据。

display-v18/composer-v16分别42/46TU构建通过。源码按真实VIG、blend stage和缩放能力
选择多个opaque NV12层，保留视频顺序并复用框架清洞；全设备合成时省略未重渲染的
client target，资源/真实atomic拒绝仍有GPU回退。新71个ASan/UBSan源码场景通过；
C数组类型与诊断SDK接口的实际编译失败已修正并保留失败记录。主机硬件边界为注入，
最终直显/缩放/清洞以下列实机证据验收，完整fence仍未据此完成。

主runner的16个图形/客户端单项exit及外层均0，含7图形、5解码、4物理窗口客户端。
150帧ByteBuffer/native ImageReader/mixed/DRC像素与PTS对FFmpeg参考逐帧精确；
paced mixed非空DQ输出交叠836ms、29次fd切换，不作吞吐/硬件同时执行或延迟证明。

两个独立视频进程各30帧，实际同时为DEVICE：NV12 plane41/FB135为320×240→160×120，
plane49/FB136为320×240→640×480，另有RGBA UBWC target plane89/FB113。
两NV12真实FB512×512、pitch512/512、offset0/262144；编号仅本轮观测。
左右LM各有六内容×五轮，前29帧各2扫描，撤窗后两侧原桌面均稳定656扫描。

RGB UI覆盖层遮挡中心视频左半部，再撤下、再覆盖：同一NV12末帧仍DEVICE，
硬件CRC出现A/B/A稳定87/90/90扫描，只有对应左LM变化，右LM保持。
这是该部分UI遮挡/清洞功能证据，不能推广为所有UI效果或严格DPU像素验收。

同boot另做全屏30帧：320×240原生NV12按9x/8x缩放到2880×1920，
HWC仅一个DEVICE视频、SDM仅SDE，实际KMS无RGB client target，已实测全设备省略路径。
1080连续CRC含六内容×五轮，前29帧各2扫描；撤窗后NV12释放，
右LM匹配原桌面，两LM随后稳定659扫描。左LM与开始时不同，原因未确定；
不写成完整CRC pair恢复，也不把这项小源缩放当成高分辨率解码验收。

合计5个物理窗口客户端150个output/release/callback；原有150帧原始像素检查另计。
最终仍同boot、六正常服务running，无试验进程/挂载/图层；holder均释放，
最终总wake locks=1（用户应用保留），Venus error/sys_error/hfi_error/insts/vb2_bytes全0。
完整逐帧fence/严格DPU像素、更广分辨率/物理DRC/格式、HDR/secure/videoUBWC、
性能/PM、KGSL/下游msm_vidc、真实QTI C2解码及跨设备仍未完成，fullHWC/fullstack=false。

证据：out/qualcomm-full-stack-20261108/qti-overlay-composition-v1/result.json、
independent-normal-audit.json、independent-composition-audit.json，
qti-overlay-fullscreen-v1/independent-fullscreen-audit.json及final-health.txt。
部署/启动：qti-system-display-deployment-v5。源码/文档尚未提交推送。

## 2026-11-09：v9 正常默认已验收首条原生 NV12 DEVICE 直显

默认SQ2Nat的qti-system-display-v9（SHA1125875d…）已正常启动，新boot
ce7df37a-84e1-4c94-b025-5e274d7af6e5；等待138秒后首次SSH成功。
62显示文件/13链接/17HIDL记录与运行服务hash匹配，唯一SQ2QDbg回退5b9f7012…保持。
无密码用户锁屏disabled=true/showing=false；没有新增备份或清理用户数据。

display-v17/composer-v15成对42/46TU构建，通过正确矩阵metadata及源码策略/资源分配，
将一个边缘opaque linear NV12 BT.601 limited 1:1层交给VIG；其他组合/资源失败/
TEST_ONLY失败保留GPU回退。71个ASan/UBSan场景覆盖实际源码方法及注入硬件边界；
主机注入不是实机TEST_ONLY。v8→v9仅5显示ELF+2校验文件变化，其余1295有效记录保持。

qti-overlay-v1直接使用正常启动服务，连续13阶段和外层exit0。7项图形回归保持；
五个解码阶段150帧像素/PTS对FFmpeg参考逐帧精确，mixed实际非空DQ输出区间交叠826ms、
19次fd切换，DRC320×240→1920×1080→320×240通过；不作吞吐/时延证明。

实际视频层已为DEVICE，SDM为SDE/VIG；同一msm/card0/crtc0上NV12 plane41/FB127，
真实FB512×512、pitch512/512、offset0/262144，裁剪320×240→[1280,840,1600,1080]，
BT.601 limited；RGBA UBWC client target为另一plane89/FB115。编号仅本次观测。
这是原生NV12物理平面，已区别于此前SF CLIENT合成视频；不靠更改Java窗口层级或关闭SF优化。
1080连续双LM CRC包含六内容×五轮；前29帧中index8仅1扫描，其余2扫描，末帧保持178扫描。
sample299撤窗后完整桌面CRC pair恢复并稳定781扫描；NV12 plane和视频层均已释放。

截图不作为DPU像素读回，render callback也不作为物理present；逐帧fence信号/复用、
严格像素、UI遮挡全面覆盖、缩放/多层、视频UBWC、HDR/secure及性能/PM仍未验收。
最终同boot、六正常服务running、实验holder释放且无残留试验进程/挂载；
总wake locks=1（保留用户应用），Venus error/sys_error/hfi_error/insts/vb2_bytes全0。
full_hwc_verified/full_stack_complete仍false，KGSL/下游msm_vidc与跨设备目标保持。
证据：out/qualcomm-full-stack-20261108/qti-overlay-v1/result.json、
independent-normal-audit.json、independent-overlay-audit.json和final-health.txt；
部署/启动见qti-system-display-deployment-v4。源码和文档尚未提交推送。

## 2026-11-09：v8 已成为正常默认，13阶段连续回归通过

**qti-system-display-v8 已部署并正常重启，逐节点 native OMX 已在默认服务生效。**
包SHA f627e683…，boot f1a784c2-080a-4528-ba3c-388c62777ff3，6.18.60-sq2-android1；
实际service/bridge为已验7a6aa750…/282834a0…，原init UID/GID/域保持。HIDL17项、
显示62文件/13链接核验通过；唯一SQ2QDbg回退5b9f7012…保持，首次SSH观测为131秒。

normal-v3为一次连续13phase及外层exit0。七项图形回归通过；五解码phase各30帧，
共150帧可见像素/PTS对新生成FFmpeg精确，三路native ImageReader时间戳精确，
EOS/stop/delete通过。真实DRC可见320×240→1920×1080→320×240各10帧。
混合ByteBuffer/native节点在同PID5590下输出DQBUF交错、区间重叠808ms、19次fd切换；
这是有输入节奏的有界功能并发，不是硬件同时执行、吞吐或时延量测。

正常native物理窗口30 output/release/callback和严格harness通过；1080连续CRC含六内容
五轮，前29帧各2扫描、末帧177。撤窗sample300恢复原桌面pair并稳定24扫描；
sample324后LM0变化，LM1持续稳定780扫描，**不能写成完整pair稳定780扫描**。
NV12原生ANW移除该SoftwareRenderer输出拷贝，经SF CLIENT→RGB UBWC→QTI HWC/SDM/DPU；
不是YUV overlay、压缩视频或端到端零拷贝。callback提前49.109–66.312ms而非物理present。
截图模型最大通道差2、MAE0.077921，量化契约未独立确定，严格物理像素仍false。

最终同boot/OMX PID5590，六正常服务running，用户动态10 disabled=true/showing=false、
Awake、wake0，实验holder释放，无试验进程/挂载或视频层；Venus sys_error/insts/vb2_bytes=0。
full_hwc_verified/full_stack_complete仍false。继续处理source strategy/resource fallback仅支持
GPU_TARGET的后续显示能力；HDR/secure、完整fence生命周期、KGSL、下游msm_vidc、PM、
性能/功耗、普通应用与跨设备保持未完成。证据见HANDOFF最新节，私有库/原始日志不上传。

## 历史阶段（2026-11-09，v8部署前）：逐节点 native OMX 在正常服务环境完成 RAM 联合验收

native-omx-node-v3 使用原 init 的 mediacodec 身份，仅临时 bind 替换 OMX service/bridge；
正常 v7 的 manager、SF、QTI HWC/allocator 与内核保持。五个解码阶段各30帧（合计150）：
ByteBuffer、native ImageReader、并发混合两路和 native DRC，逐帧可见像素/PTS均对新生成
FFmpeg参考精确，三路ImageReader时间戳精确，EOS/stop/delete通过；外层exit0。
DRC实际可见320×240→1920×1080→320×240各10帧，真实分配尺寸与crop分开核验。
并发两节点同服务、不同session/token且ioctl交错，证明模式隔离，不等于并发性能验收。

同一配对二进制的v4追加33333µs输入节奏混合试验，两路各30帧像素/PTS及native
ImageReader时间戳精确，EOS/stop/delete/exit0。真实非零CAPTURE DQBUF双fd交错，
输出区间重叠802ms（日志精度1ms），证明输出活动重叠，不作同时硬件执行或吞吐验收。

物理native窗口30 output/release/callback与严格harness通过；1080连续CRC含六内容×五轮，
撤窗后781扫描恢复桌面。index6的视频帧只有一次扫描，不称完美cadence；callback早于target，
不作物理present/延迟证明。NV12由SF CLIENT合成至RGB UBWC，未证明YUV直显或压缩视频。
末帧模型最大通道差2、MAE0.077921，量化契约仍待核，严格物理像素/full HWC/full stack仍false。

本轮已恢复原service/bridge，**默认仍 qti-system-display-v7 / mapper5-v36 的软件输出**；
未写EFI或重启，boot仍bcd26c8c…。实验wake holder已释放，最终Awake、wake locks=1、
Venus sys_error/insts/vb2_bytes=0；不为追求wake0终止用户应用。逐节点usage/ANW/mixed/DRC
已有上述实测；v8配对包已构建但未部署，下一步为部署与重启验收，HDR/secure、PM、KGSL、
下游msm_vidc、性能与跨机目标保持。证据见HANDOFF最新节；私有库/原始日志仅本地。

## 历史阶段（2026-11-09，native v3前）：36帧正常视频尾部通知区分

默认仍qti-system-display-v7/mapper5-v36，未再部署或重启。追加真实六帧GOP后，
原前30帧callback全部出现，新缺口移到32–35（32/36、严格harness仍exit1）。
1080连续CRC含六内容×六轮及撤窗后123扫描桌面；一帧占3扫描，不称完美cadence。
结果支持SoftwareRenderer后续dequeue驱动通知机制；NDK允许信息性callback缺失，
不是已证API违规或显示丢帧，亦非“回调已修”。源码/输入与实机证据见HANDOFF最新节。
本节记录当时正常服务、wake0/Awake及Venus健康；之后逐节点native usage/ANW/mixed/DRC
已完成RAM联合验收，见本文最新节。完整HWC、HDR/secure、PM及整栈目标仍未完成。

## 2026-11-09：正常显示包 v7 已启动，默认 AVC 回归通过，窗口回调缺口仍在

**qti-system-display-v7 已部署 SQ2Nat 并正常重启生效**，initramfs SHA4fb449db…；
唯一 SQ2QDbg 回退5b9f7012…保持。新boot `bcd26c8c-cc5d-4858-8237-45ea3657c184`，
内核仍6.18.60-sq2-android1，62个安装文件hash匹配。正常QTI composer、AIDL allocator、
HIDL manager、SF/zygote、原Power HAL、OMX和C2服务运行/注册；当前用户动态发现为10，
锁屏disabled/showing=false，仅记本轮观测，不作为正常启动条件。

normal-v2九个独立phase最终均exit0：27AHB/exec、45EGL与54BufferQueue帧、GLES1
pbuffer8帧/265,224像素/8fence、Vulkan计算/光栅/3AHB及ImageReader WSI24帧，
135个YUV缓冲/GPU/exec（27YV12+108NV12/P010）、126次标准lockPlanes与真实QTI
extension原AHB/共享数据/metadata/独立所有权通过。**原runner在extension启动环境错误处
exit1**；只修诊断命令为system优先库路径后，extension及两视频phase续跑通过，
不能写成一次连续九阶段外层exit0，也没有修改全局linker配置。

默认原厂OMX AVC的ByteBuffer和Surface/ImageReader各30帧，经实际stride/crop与
I420→NV12归一化后，逐帧像素/PTS对重新生成的FFmpeg参考精确，EOS/stop/delete通过。
Surface/ImageReader使用SoftwareRenderer；这不证明默认native OMX零拷贝。
物理SoftwareRenderer窗口两轮均未通过现有harness的严格回调计数：分别26/30和27/30；
各660个连续硬件CRC样本中，右LM六内容依序五轮、全部30段出现，包括缺callback尾部。
SF实测YV12 linear usage0x920 CLIENT合成到RGB UBWC，不能把回调缺失直接当显示丢帧，
也不能据CRC代替完整回调/fence或严格物理像素验收。
官方NDK r28c `media/NdkMediaCodec.h:510–512` 明确OnFrameRendered仅用于信息通知，
可显著延迟/批量送达，已render的帧也可能没有callback。因此26/30、27/30仍是现有harness
额外逐帧计数判据的失败，未被修复；单此结果不证明API违规或显示丢帧，也不构成继续推进
native OMX前必须重写Framework的理由。实际呈现、fence及性能仍按独立证据验收。
第二轮截图源模型比对76,800像素：最大通道差1、MAE0.055026；尚无独立量化容限，full_frame_pixel_pass=false。两轮CRC均未覆盖窗口退出，不能据其证明退出后回到桌面。

收尾另取final-desktop事后样本：正常launcher截图无视频窗口，60个连续CRC样本的59个
frame delta均1、CRC pair恒定，观察器恢复source=none。Wake Locks size=0，旧wake.pid进程
已退出，当前用户disabled=true/showing=false、Awake；Venus sys_error/insts/vb2_bytes均0，
boot仍bcd26c8c…。这份独立事后样本确认收尾桌面和资源状态，不证明退出瞬间的切换时序。
证据：`qti-system-display-normal-v2/final-desktop/{device.txt,crc.txt,sf.txt,desktop.png}`。

C2兼容修复恢复其服务注册及全局codec资源枚举，实际视频回归仍为OMX，不是QTI C2解码验收。
完整HWC/full_stack仍false；KGSL、下游msm_vidc、HDR/secure、PM、性能、完整fence生命周期、
普通应用和跨机目标保持。原开发permissive域策略未改变，不作enforcing验收。
证据为 `qti-system-display-normal-v2/normal-independent-audit.json`、`independent-window-v*/`
及 `qti-system-display-deployment-v2/`；原始日志/私有库仅本地，本轮修改尚未提交推送。


## 历史阶段（2026-11-09，v5）：正常显示包启动及五阶段回归

**qti-system-display-v5 已部署SQ2Nat并正常重启生效**，initramfs SHA2664c067…，
唯一SQ2QDbg回退5b9f7012…保持。新boot `7bb4e664-cd35-4fe1-89f5-bef4350f0b8c`，
内核仍6.18.60-sq2-android1，sys.boot_completed=1；真实hwservicemanager、QTI composer、
AIDL allocator、SF/zygote和原Power HAL均running，标准allocator/SF/input/window与display.qservice均found。
原composer3服务未运行；60个已安装文件hash与v5全匹配，composer/allocator/SF身份和实际加载闭包已核。
这些进程未加载已退役的mapper/句柄桥。正常显示HIDL manager已独立于视频实验开关。

`qti-system-display-normal-v1` 五阶段及外层均exit0：27 AHB与独立exec像素精确，
45 EGL缓冲和54 BufferQueue帧，GLES1 pbuffer 8帧/265,224像素/8native fence，
Vulkan计算/光栅、3AHB精确读回/重新导出及ImageReader WSI24帧/24native fence均通过。
生产/vendor路径的Vulkan与原GLES1现已实测；GLES1 pbuffer和Vulkan ImageReader不冒充物理应用窗口。
当前用户动态发现为10、showing=false、屏幕ON/AWAKE；用户编号仅本轮观测，不成为启动条件。

本轮正常桌面物理KMS审阅及默认视频回归仍在采集，尚不提前计入本节。
原permissive-domain开发策略保持，服务域正确和注册成功不等于enforcing权限验收。
full_hwc_verified/full_stack_complete仍false；完整fence/power生命周期、HDR/secure、性能/PM、
KGSL、下游msm_vidc与跨机目标不变。证据为`qti-system-display-deployment-v1/first-boot-state.txt`及
`qti-system-display-normal-v1/`，原始记录/私有库仅本地，不上传。

## 历史阶段（2026-11-09，system-v16）：联合通过与正常启动候选准备

沿用 **display-v16/composer-v14、mapper5/v33、native-video-window-v2** 和已部署的
caps-fence-ubwc-v1 内核，新增 native-vulkan-v6 的真实原生句柄 Vulkan 验证。
在真实 SF/HWC/Power HAL 的隔离服务环境中，计算/光栅、3轮AHB精确读回及重新导出通过；
ImageReader WSI两次创建/销毁共24帧、795,672像素bad=0、24个native fence、192次getter均通过。
这不是Vulkan可见Surface的物理扫描或性能验收。缓存压力覆盖type0/4的150×4KiB与16×16MiB；
type4双向CPU/GPU一致性通过，type2/3仅CPU/cache API覆盖，不宣称全部内存类型GPU验证。

Vulkan只保留内存策略HAL/TLS：AHB/FD/host导入及export不改缓存模式，原生句柄和pNext原样传递。
WSI先前卡住源于私有SF缺失；v15的lock错误源于探针把GPU_COLOR_OUTPUT传作ImageReader consumer
usage。v16仅改consumer为CPU_READ_OFTEN，实际AHB仍含GPU生产者usage，不放宽mapper5的CPU锁契约。
图案120CRC两种各60/每15交替、30帧视频内容顺序及两次30黑场CRC回归通过；视频一帧仅1扫描，
其余前29帧各2扫描，不宣称完美cadence。30个ImageReader输出/PTS与已验基线相符，截图与v14同SHA。

正常启动候选 **qti-system-display-v5** SHA256 `2664c067b80c67629d7678d2b6acb9a3924e53861f46e6ef00b18bb66be8f8ac`
配套native-vulkan-system-v5；独立真实HIDL manager不依赖视频实验开关，保留开启时的OMX原服务。
安装原GLES1实体/alias，精确清除3旧mapper实现和5旧句柄适配器，保留接口库；60文件/13链接/4支持文件
的生成stage及视频开/关×重入验证通过。正常init/生产路径和重启验证仍pending，不能据构建提升默认状态。
开发SELinux策略仍沿用原permissive domains，QService系统Binder权限未作enforcing验收。

system-v16外层exit0，原默认DRM HWC/minigbm、Power HAL及桌面已恢复，屏幕ON/AWAKE，wakelock释放，
Venus sys_error/insts/vb2_bytes=0。正常包已部署待重启验证；330728ef…为先前已验基线，唯一回退5b9f7012…保持。
2026-11-08 19:29:33.969 UTC已将v5写入SQ2Nat，远端hash/gzip/SHA256SUMS一致；
已发出正常sys.powerctl reboot。唯一SQ2QDbg回退5b9f7012…保持，**当前为已部署、等待正常启动验收**；
不提前写成新用户态已生效。证据`qti-system-display-deployment-v1/deploy.txt`及`reboot-request.json`。

full_hwc_verified/full_stack_complete仍false；完整HWC生命周期、GLES1实绘、HDR/secure、性能/PM、KGSL/
下游msm_vidc与跨机目标保持。原始证据/私有库仅本地，详见HANDOFF/BUILD和system-v16/result.json。

## 历史阶段（2026-11-09，system-v14）：真实空栈清屏与视频窗口切换已通过

最新已验证配套为 **display-v16/composer-v14 + native-video-window-v2**，沿用 mapper5/v33
和 caps-fence-ubwc-v1 内核。video-mode 空栈现在通过真实 NullCommit(retain_planes=false)
解除旧 plane，并返回同次 atomic 的真实 retire/present fence；不申请虚假的新 buffer release。
HWC Flush 失败传播错误并保留重试状态，不把失败当空帧成功。

system-v14 实测6次 DPU_FLUSH result=0、retire_fd=13；图案和视频撤窗后，8个实际plane均
FB=0/crtc=null，CRTC保持active，两次各30个连续双LM CRC均0。前轮末帧残留已在本轮消除。
资源编号与fd仅本轮观测。图案四个中央区域共1,382,400个RGB像素与源图精确一致；
边缘BootAnimation缩放/滤波未源匹配，不声称全屏逐像素验收。120个图案CRC两种各60/每15采样交替；视频420个连续样本为
38黑→六种视频内容依序重复五遍→147黑，前29帧各2采样，末帧保持177采样至窗口移除。
新共享解码函数的ImageReader30帧NV12/PTS精确回归通过；实际Surface30 output/release/
API callback/EOS及关闭通过，仍是NV12由SF CLIENT GPU合成到RGB UBWC，不是YUV直显/视频UBWC。

callback payload等于target，实测callback到达早于target约49.5–66.4ms，明确不能作物理present。
末帧截图与v13同hash，AOSP crop/transform独立模型76800像素MAE0.077921/最大差2；
厂商滤波/量化容限仍待核。完整HWC fence信号/复用/disable、HDR/secure、性能/PM及跨机未验收。
本轮仅提升物理空栈blank与窗口切换，full_hwc_firstframe=true，full_hwc_verified/
full_stack_complete/default user-space replacement仍false。
默认DRM HWC/minigbm、原Power HAL、SF/zygote/input/window恢复，屏幕ON、wake holder释放，
Venus sys_error/insts/vb2_bytes=0。持久用户态/EFI不变，默认initramfs330728ef…、唯一回退5b9f7012…保持。
完整证据见HANDOFF/BUILD与本地system-v14/result.json；原始日志/私有库仅本地，不上传。

## 历史阶段（2026-11-09，system-v13）：控制图案/原生视频扫描与退窗残留

当前已验证配套为 display-v15/composer-v13，沿用 mapper5/v33 与 caps-fence-ubwc-v1。
真实 SF 的双图案窗口通过 QTI HWC/SDM → 6.18 DPU：120 个连续 CRC 样本为两种值各 60，
每 15 个扫描帧交替；截图四象限采样 RGB 与独立图案精确匹配。
原厂 OMX/HIDL → ACodec → 实际 Surface 的 AVC 30 帧按 PTS 定时释放，
30 output/release/callback 与 EOS/关闭均通过；不等待消费者回调节拍。
实际线性 NV12（usage 0x20400900、stride/scan 512、393216 字节）由 SF CLIENT GPU 合成到
RGBA8888 UBWC target 后上屏，不是 YUV 直接 plane 或压缩视频验收。
视频硬件 CRC 为六种内容顺序重复五遍，每帧对应两个 60 Hz 扫描样本，物理扫描链已贯通。

callback 的 actual_ns 均等于请求 target_ns，只能记为 API 通知，不能当硬件 present 或延迟。
同轮独立 ImageReader 基线 30 帧归一化 NV12/PTS 精确。末帧 320×240 截图按 AOSP crop/transform
独立模型对照 MAE 0.077921、最大通道差 2；量化与厂商滤波契约仍待核，不提前标为严格像素通过。
早期未建模 YUV crop 收缩的 ffmpeg max210 参考不代表真实颜色失败。

空层输出归零修复后本轮无 invalid layer 0，但退窗后 KMS plane89/FB115/CRTC active，
CRC 最后帧持续到结束；旧图案也在视频前残留 39 个样本。编号仅本轮观测。
因此真实空栈 blank、完整 HWC fence/复用/关闭生命周期仍未通过；system-v14 正在验证
新 Flush/retire fence 修复，结果尚未计入。full_hwc_firstframe=true，full_hwc_verified/
full_stack_complete/default user-space replacement 仍为 false。
默认 DRM HWC/minigbm、Power HAL 与 SF/zygote 已恢复，屏幕 ON、wake holder 已释放，Venus 健康。
持久用户态和 EFI 不变；仅此前 UBWC 内核已部署。证据见 HANDOFF/BUILD 与本地 system-v13/result.json。

## 历史阶段（2026-11-09，system-v12）：完整 HWC/SF 真实首帧提交

最新配套为 display-v15/composer-v12。system-v12 已由原版 SurfaceFlinger 产生真实
mapper5/UBWC client target，经源码 QTI HWC/SDM 提交到 6.18 DPU：
首个 target id=15/fd=26/format=12，AtomicCommit=0，release fd=29、retire/present fd=28；
本轮 plane89/FB118、QCOM_COMPRESSED modifier、2880×1920@60、CRTC105 active=1。
这些编号仅为本轮观测，不得成为正常运行条件。full_hwc_firstframe=true，
整体 HWC/显示视频栈验收及默认用户态替换仍为 false。

本轮补齐私有 Binder 中已声明的真实 Pixel libperfmgr Power HAL，使用原二进制/配置/身份；
先停止原 owner、实验结束先退出私有 owner 再恢复原服务，不用假 HAL 或隐藏声明解围。
修复 GetClientTargetProperty 无格式更新时未初始化输出：RGBA_8888/UNKNOWN 表示无请求；
真实格式请求仍转换并传播错误。Linux 不支持的 histogram/DPPS 不再被初始化或广告，
入口返回 NotSupported；不再将每帧无条件 HIST IRQ 请求加入 atomic，也不放宽 DRM contract。

硬件 CRC 观察得到 120 个样本，119 次 frame delta 均为 1，CRC 变化 27 次；
原 bootanimation 约 5 秒结束，CRC 尾部为 0。尚无独立预期像素对照，
不能据此宣称完整 fence 生命周期、持续内容、原生视频、HDR/secure、性能/PM或跨机验收。
动画退出后空层栈曾报 invalid layer 0，需单独处理；后续控制图案/视频结果尚未计入状态。

caps-fence-ubwc-v1 已更新 SQ2Nat 并正常重启，实际模块 build-id 为
1dacaf73eac94ebfacbe76f45a9429bc48e1a8f1；默认 initramfs 330728ef…，
SQ2QDbg 唯一回退 5b9f7012…保持。当前持久显示改动仅为此前已验收的 UBWC 显示内核；
默认用户态仍是原 DRM HWC/minigbm，vendor.minigbm.debug=nocompression。
独立 LINEAR/UBWC 八帧双 LM CRC 与真实 fence/disable 回收基线保持，但不替代整栈验收。
system-v12 外层及 CRC helper 均 exit 0，默认服务、input/window、唤醒与 Venus 健康已恢复。
构建/hash 与原始证据路径见 docs/HANDOFF.md、docs/BUILD.md；原始日志/私有库仅本地，未提交推送。

用户允许实验恢复时跳过已确认无密码的锁屏。动态发现当前用户并核对 passwordless/disabled，
再 wake/dismiss-keyguard，不硬编码用户 10、不清除密码或更改持久 PM 策略。
停止 zygote 前由正常 system Binder 下的有界 PARTIAL holder 保活，实验结束释放；
恢复等待 input/window 就绪及 system_server 稳定，单个动作失败不得阻断其他清理。
此措施不表示 PM 缺陷已修复。

## 2026-11-09：正常重启后的 SSH 恢复窗口

用户确认设备正常重启后可等待 120 秒再尝试 SSH；期间继续本地构建/分析，
不要因过早的超时或 No route to host 就请求人工唤醒。超过窗口仍不可达时结合屏幕/网络现场诊断。

## 2026-11-08：用户确认后续执行方案与较大开发步幅（当前规则）

采用 **mapper5 统一框架接口 + 源码 QTI HWC/SDM + Linux 6.18 显示适配**。
保留已验证的原厂 gralloc 后端作为集成基线，吸收小米 11 的源码显示实现；
不再以整套切换 mapper3/4 为当前主线，也不把 mapper5 仅列为对照。
先补真实 capabilities/输出 fence/缩放 CSC/UBWC 的 DPU 衔接和物理显示，
再接全 HWC 内部 allocator/metadata/生命周期与原生视频；缓冲缺口按实际依赖同步补齐。
**每轮合并推进有依赖的一组改动和完整链路验证，遇到实际失败再 debug；
不因单个接口、库加载或探针通过就结束本轮，修复后回到阶段链路继续推进。**
减少无实际依赖的前置检查与反复微小实验，保留必要回归、资源/ABI/数据和恢复保护。
KGSL、下游 msm_vidc、统一内存、HDR/安全能力/性能/PM及跨设备目标不缩减。
mapper4 只在配套后端实测、同等回归及具体工程收益成立时评估切换；禁止伪报 API/能力。
完整顺序、验收和切换条件以 [开发方案](docs/QUALCOMM-FULL-STACK-PORT.md) 的
“当前执行方案（2026-11-08 用户确认）”为准。用户已恢复移植目标为 active，后续依此执行；
本次文档保存不代表候选已部署或验收。

## 历史路线评估（2026-11-08，已由当前确认方案取代）：参考SM8350高版本Android

用户明确允许参考一加9 Pro/小米11高版本Android做法，不必坚持mapper5。
主目标仍完整高通显示栈与Linux6.18，不把HAL版本升级当完成条件。
优先评估公开SM8350 QTI HWC/SDM源码、mapper3/4共用原生gralloc后端的配套路线；
已验证mapper5保留作对照，不因此部署旧的只读mapper4包装器或伪报设备API。
最新31份参考/完整304源码blob已固定commit与hash，见configs/sm8350-display-reference.json。
一加公共设备树保留原厂composer/SDM/DRM二进制，小米公共树构建composer，不能混称相同。
两者仍依赖旧SDE契约；当前DPU缺capabilities/fence等不能因换mapper消失。
sm8350-importer-v3只验证源码composer导入类接mapper5：66缓冲/132导入/18保留像素通过，
不是整HWC服务，内部allocator与SDE后端未迁移。默认服务/EFI保持，详见HANDOFF。

## 2026-11-08：RGB UBWC mapper5/GPU已通过，显示平面仍仅线性

mapper5/v33（核心二进制同v32）实际分配24个RGB UBWC AHB，72轮GPU图案/fence/采样及CPU锁定拒绝通过。
标准metadata必须如实区分压缩payload与metadata前缀；DRM压缩FB使用原厂GetBufferLayout的base0/pitch，
不能把标准payload offset直接拷给KMS或将压缩伪报线性。BGRA两种usage仍由原厂判定为线性。
24次AddFB2通过含18压缩/6线性BGRA，6次FP16真实EINVAL；不代表atomic/物理显示。
8个实机plane的284个IN_FORMATS组合全为linear；配套源dpu_plane.c保留SQ2_NO_UBWC_MODIFIER，
须处理旧限制和真实HWC消费者转换并验证硬件，不能仅删限制就宣布上屏。
27标准AHB/exec、45RGB GPU、45锁定同步、54离屏窗口、108YUV及线性DRM回归通过。
默认服务/EFI保持，临时服务退出、Venus健康；v12突发25/30等缺口不变。详见HANDOFF，目标active、未提交。

## 2026-11-08：ACodec原生Surface受控30帧与真实显示master

system-surface-v13在真实SF/HWC/HIDL/ACodec下完成30帧ImageReader像素/PTS/EOS及18缓冲复用；
按消费者收帧进度释放输出，不是吞吐验收。v12突发30输出仅25图，公开no-drop=0仍不足；
不得据v13宣称渲染回调/丢帧缺口修复。v11实际窗口请求UBWC被拒，黑图不算上屏通过。
VIDC桥只在显式native-layout的未使用CAPTURE队列上真实释放/S_FMT/恢复数量，保留错误保护。
实验SF须用实际system UID，root会令原厂libgsl选primary抢master；隔离Binder继承原节点权限。
当前真实DRM HWC取得master及LGD 2880×1920@60Hz，仍需UBWC/窗口/物理视频和正式init/SELinux。
默认服务已恢复、原持久库/EFI保持，详细证据见HANDOFF；目标active、未提交推送。

## 2026-11-08：mapper5 RGBX/BGRA及真实DRM framebuffer导入通过

mapper5/v30加入实际RGBX8888/BGRA8888布局；45缓冲metadata/exec及45 GPU+45 held-lock同步通过，
108 YUV与54离屏窗口帧回归通过。drm-v31以无CPU的0xb00 usage完成18次真实PRIME/AddFB2/
GETFB2/RMFB，未取得master或modeset，不是物理显示/HWC/ACodec Surface验收。
真实8个DRM plane均未广告FP16和ABGR2101010；FP16 AddFB2 EINVAL，RGBA10可建FB但无plane支持。
保留真实格式，不伪报可直显；后续合成到已支持目标或验证硬件格式扩展，不能只靠AddFB2通过提升默认。
系统服务/EFI未变、Venus健康，详见HANDOFF/result-v30.json；整栈目标仍active。

## 2026-11-08：原生远程HIDL真实30帧通过

hidl-decode-v8在surface-v3实验服务、mapper5/v27上完成真实远程AVC/NV12解码30帧，
逐帧像素及PTS精确；输出初始18缓冲，经首次SOURCE_CHANGE重配为6缓冲，各复用5次。
EOS、所有buffer归还、Idle→Loaded/freeNode通过，Venus健康；没有修改正常服务或EFI。
样本含B帧，输入PTS必须按显示顺序映射到码流AU；v5-v7探针误按解码顺序赋PTS，
不能将其误判为丢帧/回调乱序或为此修改驱动。证据见HANDOFF最新节。
ACodec native Surface、物理显示、流中尺寸切换及逐节点usage仍待完成；不取消正常软件输出。
目标仍active，改动未提交推送。

## 2026-11-08：原生HIDL缓冲导入通过，Surface仍待接入

surface-v3隔离候选通过真实远程HIDL注册4个ashmem输入及18个mapper5 AHB输出，
Loaded→Idle→Loaded和释放通过，但本次HIDL decoded_frames=0，不能套用v27私有30帧结果。
真实MediaCodec/ACodec隔离基线6帧通过；native Surface配置等待缺失的SurfaceFlingerAIDL。
不伪造compositor或修改libgui跳过查询。QUEST2_NATIVE_SURFACE_TEST仅用于显式实验构建，
正常服务保持RequiresSwRenderer=1/AVC-only；未带宏构建6帧像素/PTS回归通过。
native layout暂用固定CPU-readable NV12 usage，须改为逐节点协商并验证DRC后才能正式部署。
详细证据result-surface-v3.json见HANDOFF；目标仍active，未提交推送。

## 2026-11-08：mapper5真实OMX AHB解码已通过

mapper5/v27加入VIDEO_DECODER usage；未改libui标准AHB/AIDL分配、Stable-C布局元数据、
真实OMX HEVC/NV12共30帧320→1080→320 DRC/复用/SHA/PTS/GPU/fence/EOS全部通过。
不调用旧mapper实现；原厂接口库与EGL旧查询仍保留。108个YUV AHB/GPU/exec及RGB/窗口回归通过。
此路径仍为单实例私有诊断，系统OMX→HIDL→ACodec Surface、默认HAL、HWC/物理显示未接通；
不得取消RequiresSwRenderer冒充完成。flexible格式/其他usage/init/SELinux继续推进。
无重启/EFI改动，服务已清理、Venus健康，详细证据见HANDOFF；目标active，改动未提交推送。

## 2026-11-08：mapper5 NV12/P010标准AHB与GPU共享已通过

mapper5/v26支持非保护、线性、偶数尺寸progressive的vendor NV12(0x7fa30c04)、
标准P010(0x36)、Venus P010(0x7fa30c0a)。54个标准AHB跨三尺寸/三图案/cached与uncached，
原厂GPU采样容差内通过；生产者释放后54次独立exec导入/CPU像素全匹配，标准P010 lockPlanes36次通过。
原厂YUV公共几何API按实际aligned尺寸调用，P010 Y step与标准P010 UV行数错误仅在已核实格式内归一；
不改像素存储、不调用旧mapper实现。RGB/GPU/同步/54窗口帧回归保持，详见HANDOFF。
这不是OMX解码帧或系统Surface。flexible 0x23/implementation-defined、完整video usage及requested-format
共享契约仍待实现，不能把实际vendor格式冒充请求格式；uncached仍依赖MSM GEM WC。
默认/EFI未改、服务清理/Venus健康；整栈目标继续active，改动尚未提交推送。

## 2026-11-08：mapper5缓存同步与dump已实机验证

mapper5/v24新增flush/reread和21类metadata dump，27缓冲CPU/exec/dump重入及回调释放通过。
额外27轮保持原CPU映射锁定的flush→GPU采样、GPU写回→真实fence→reread像素精确；
原27轮AHB GPU和54帧离屏窗口回归通过。像素fd用DMA-BUF END/START RW，
同步期间不持全局mutex但保留pending生命周期引用；metadata限定已验证的coherent memfd/tmpfs。
这不是YUV/UBWC/物理显示或性能验收。全局服务/EFI未变、临时服务清理和Venus健康通过。
下一步YUV/P010与系统OMX Surface/SF/HWC，KGSL/msm_vidc/HDR/secure/PM目标不缩减，详见HANDOFF。

## 2026-11-08：mapper5原版libui/GPU与离屏BufferQueue已通过

mapper5/v20在私有Binder/挂载命名空间通过原版libui AHB分配及exec导入27缓冲、
三RGB格式GPU采样/写回/真实native fence共27轮，EGLWindow→ImageReader54帧像素/时间戳匹配。
maps确认无旧mapper实现/CPU上传桥；EGL仍会尝试旧HIDL接口查询，再用自身gralloc1回退。
原厂gralloc实体必须放在ODM hw目录内，libhardware会拒绝解析到hw目录外的别名。
允许原厂判定为线性的CPU-readable UBWC偏好请求，原usage不改；不是UBWC压缩支持完成。
mapper本体与v15逐字节相同；系统默认/EFI未变，测试服务退出，Venus健康，详见HANDOFF。
仍需flush/reread/dump、YUV/P010、系统OMX Surface、SF/HWC/物理显示及KGSL/msm_vidc/HDR/PM；
不得以离屏通过宣称整体完成或性能持平。改动尚未提交推送。

## 2026-11-08：mapper5共享字段及未改libui标准分配已通过

mapper5/v15的mapper/allocator与v8相同，21类metadata/27次exec共享NAME/blend/色彩通过；
私有Binder/default allocator + native mapper VINTF下，未修改libui的标准AHB27缓冲及独立exec导入通过。
不是全局默认、GPU或物理显示。完整元数据结构前缀校验和实验边界见HANDOFF。
Android17需声明native mapper @5.0/quest2；system Binder库优先，不能以vendor域远程探针替代框架集成。
v10-v14超时包装PID清理缺陷已按身份清理；v15记录真实子服务PID并确认无残留，原桌面/服务/EFI保持。
下一步flush/reread/dump及YUV、mapper5 GPU/BufferQueue/视频系统Surface；整栈目标继续active。

## 2026-11-08：mapper5共享色彩元数据与等待锁修正

mapper5/v6真实AIDL 27RGB缓冲及27次exec共享DATASPACE/SMPTE2086/CTA861_3通过，共16类metadata。
直接原厂QTI metadata API，无mapper3/4实现调用；HDR是元数据回传，不是物理显示验收。
pending fence不再持全局mutex，阻塞poll时metadata/第二导入可访问，pending free拒绝；
此并发测试使用eventfd，不能写成真实GPU fence通过。完整边界见HANDOFF。
NAME/blend、其他必需metadata、YUV/flush/reread与未改libui/GPU/系统显示仍待接入，不可默认替换。
临时服务已清理、桌面/OMX/libui/EFI保持，目标仍active；改动尚未提交推送。

## 2026-11-08：直接 mapper5/AIDL 候选已通过27缓冲远程调用

mapper5/v3直接调用原厂gralloc1/grallocutils，27个RGB缓冲的真实AIDL allocate2、
独立导入/12类metadata/CPU读回/释放通过；不再调用mapper3/4实现，但原厂旧接口库仍间接加载。
当前mapper5必需可写metadata、YUV、flush/reread等未完成，不可默认部署或称全栈完成。
临时/quest2服务及声明已清理，默认minigbm/桌面/libui/EFI不变，详见HANDOFF。
下一步补现代接口并接未改libui/GPU/显示消费者；旧v12 GPU证据不能替代mapper5实测。

## 2026-11-08：现代metadata契约候选，非完整mapper4部署

allocator/v15的mapper4导入/12类只读RGB metadata、27缓冲读写通过；它内部仍用mapper3，
不支持分配、可写/HDR metadata或完整mapper4契约，不可全局安装或加到默认VINTF。
QTI RGB subsampling实际指数0，要经真实布局验证转成标准factor1；逻辑尺寸与padding分开。
用户关于mapper5的架构问题不等于性能已验证：最终可直接mapper5/AIDL，但正常Surface路径仍需
mapper职责。优先用已验证契约推进现代接口，不能把临时mapper4当整栈完成。详见HANDOFF。

## 2026-11-08：标准 EGL/AHB 与离屏 Surface/BufferQueue 已贯通

allocator/v12经系统EGL前端和标准mapper HAL，27轮AHB GPU采样/写回/native fence通过；
三RGB格式三尺寸的真实EGL窗口→ImageReader/BufferQueue54帧像素与时间戳全匹配。
EGL子驱动已移除私有getService依赖，maps确认无旧CPU上传桥。详见HANDOFF。
这是私有命名空间中的离屏Surface，不能称为物理显示、SF/HWC或OMX原生Surface完成。
系统默认HAL/桌面/EFI保持，原OMX已恢复，后续必须共同处理系统/vendor显示消费者及metadata。

## 2026-11-08：标准 mapper HAL、AHardwareBuffer 与 Android17 选择限制

标准HIDL HAL入口v5已通过；v8标准AHB分配及独立exec导入27个RGB缓冲通过，详见HANDOFF。
目标Android17 libui在API>35时跳过mapper3/2；明确的私有libui候选只解除该后备分支，
不改设备API、不冒充mapper4/5、不取消RequiresSwRenderer。原厂getService私有拦截非此路径。
只在新挂载命名空间测试，系统libui/默认allocator/桌面/EFI未变，临时服务及VINTF已恢复。
这些AHB尚未通过GPU/Surface/显示消费者；不得套用旧私有GPU结果宣称系统整栈完成。

## 2026-11-08：原厂 allocator Binder 服务及 mapper RGB 单位

allocator/v4原厂服务真实HIDL IPC分配/mapper导入27缓冲通过，native-gpu-v24包含
RGB bytesPerStride单位修正并通过27轮GPU/6帧NV12回归。详见HANDOFF。
当前兼容mapper仍是进程私有getService入口，尚非系统HAL；不据此替换minigbm默认。
临时allocator/manifest已清理，hwservicemanager刷新后原OMX恢复注册；SF/boot未变。
P010旧metadata、正式SELinux服务身份、系统Surface/显示消费者仍需实现。

## 2026-11-08：原厂 OMX 原生缓冲帧循环已验证

native-gpu-v23进程私有HEVC/NV12共30帧、320→1080→320往返、buffer复用、
逐帧参考/GPU/native fence/EOS/释放通过，详见HANDOFF最新节。保持codec实际
PRIVATE_UNCACHED usage；不能读native header->pBuffer为像素。布局只在SOURCE_CHANGE
且CAPTURE释放后真实S_FMT，OMX crop可能滞后一代，按实际G_SELECTION绑定buffer代。
当前helper单实例/NV12/零crop原点，不能直接安装服务或取消RequiresSwRenderer。
本阶段未重启/写EFI，原默认候选与唯一回退保留；整体目标仍未完成。

## 2026-11-08：原生视频共享候选已部署，保留原运行包回退

当前默认EFI/SQ2Nat、initramfs f48e732a…，配套6.18 Venus capture-layout-v2；
原SQ2QDbg的5b9f7012…载荷及其余7项hash未变，继续作为唯一回退。
已完成NV12/P010各6帧VPU→原厂gralloc→GPU直接共享与CRC/fence/EOS/回收，
v16 RGB27轮及普通解码回归通过，详见HANDOFF最新节。
系统HAL和OMX Surface仍未接入原生路径，不取消RequiresSwRenderer冒充完成；
PM超时、渲染回调、KGSL/HWC/下游msm_vidc、HDR显示和secure仍未解决。
候选通过标准S_FMT/HFI协商真实几何；不能仅改metadata，也不能热卸载或清sys_error。
后续部署只更新候选，不覆盖已保留SQ2QDbg回退；不新建旧载荷备份。

## 2026-11-08：整栈移植继续，现机只保留当前 Qualcomm 回退

用户明确授权直接操作当前开发设备，包括关机、正常重启和修改EFI，并要求
直接清理当前运行入口以外的启动项，不做新备份。此要求覆盖旧的保留Mesa/6.3.8
入口规则：已清理8个旧EFI目录和旧菜单恢复文件，仅保留SQ2QDbg当前载荷作为回退，
保留共享BOOT、固件与SPX/LTE本机缓存。不得据此清用户数据或未知文件。
当前回退菜单ID为sq2-qcom-618-fallback，内核6.18.60、initramfs仍5b9f7012…；
一次正常重启通过，详见HANDOFF。后续新候选不得覆盖这个已保留的回退载荷。
整栈目标保持active，native gralloc私有分配/跨进程读回已通过，不是系统HAL替换。
共享内存后端仍依赖MSM GEM WC；移植KGSL前必须解决该依赖。
当前本地连接配置留out/private，IP不作为产品或通用构建准入。

## 2026-11-08：整体高通栈移植说明与进度保存

用户要求保存进度并推送，按整体移植方案写说明。后续架构以
docs/QUALCOMM-FULL-STACK-PORT.md为设计依据，覆盖匹配KGSL、QTI allocator/mapper/gralloc、
msm_vidc、OEM OMX/HIDL、HWC及显示内核接口，维持6.18唯一基线。
这是路线选择，不代表上述组件已落地；当前混合栈、AVC-only默认及验收边界保留。
不新增回退/备份入口，不删除旧EFI入口；源码保存不夹带私有库、固件、身份或原始日志。
不要只改RequiresSwRenderer标志冒充原生缓冲，也不要复制donor的SoC/DT/固件假设。

## 2026-11-07：移植全部硬件解码能力，包括HDR

用户明确要求继续移植硬件支持的全部解码能力，包括HDR。目标覆盖固件实际支持的各codec、
profile/level、10-bit、色彩及静态/动态HDR元数据、输出布局、流中重配置和显示处理。
沿用原厂OMX→HIDL→ACodec路线；不得重新启用已退休的项目C2实现。
按实际固件逐格式能力与当前可用路径声明，原始硬件能力、系统可用能力、实测范围分开记录。
HDR不是单纯添加profile标签：需真实10-bit像素和色彩/元数据传递；SDR面板需要正确色调映射。
保护内存/安全会话/DRM需要真实平台能力，不能把普通内存或非安全会话标成secure。
继续既有自动部署/正常重启授权、600秒SSH窗口和ESP/log空间管理，不新增回退或清应用数据。

## 2026-11-07：清理旧C2实验实现，能力声明对齐实际硬件

用户要求清理之前解码器，并明确能力声明应与硬件实际能力一致，不照抄原厂旧声明。
默认HIDL路径移除本项目c2.sq2.*.experimental的服务/注册/部署runtime；保留系统c2.android.*软件音视频组件。
C2名字不等于软件解码；不能按c2前缀删除全部系统库，libc2dcolorconvert也不是Codec2组件。
profile/level、尺寸、格式等须依据实际Venus/HFI能力及可用适配路径；不能把测试样本规格当硬件上限。
硬件能力、当前移植可用能力、实际测试范围分别记录；未接通的secure/HDR/UBWC等不能虚报可用。

## 2026-11-07：启动日志与 ESP 空间维护

用户实报Linux启动阶段No space left on device；清理logs释放约300MB后恢复启动及SSH。
开发期间及时检查ESP剩余空间和日志增长，部署前/重启后记录df及日志占用；不能只核对镜像hash。
仅清理已确认的开发启动日志，当前故障先取回必要的有界证据；不要把已取回日志无限留在ESP。
禁止把此授权扩展为删除EFI启动载荷、固件、LTE校准/本机缓存、用户数据、凭据或应用缓存。
正常启动不得因诊断日志目录创建/写入ENOSPC而中止；日志需要大小上限及剩余空间保护。
部署空间按实际新文件/临时写入方式计算，保留运行余量；不能靠删除未知文件腾空间。

## 2026-11-07：重启后 SSH 恢复等待时间

用户要求每次正常重启后，SSH整体重连窗口为10分钟（600秒）。连接拒绝、超时或暂时无路由
在窗口内只算暂时未就绪，继续重试；不能以单次连接失败提前宣布设备恢复失败。
使用scripts/wait-sq2-ssh.py记录重连；单次连接保持短超时，不将一次阻塞设为10分钟。
仍验证既有主机密钥，重启后核对boot_id变化；不因等待失败重复发起重启。

## 2026-11-07：高通 Direct 续接的设备部署授权

用户在本轮明确要求“允许自动部署 自动重启并设为默认”。此授权覆盖此前一次重启/手选第三项限制：
允许在当前6.18高通集成任务中自动部署、正常重启并将Qualcomm入口设为设备默认。
保留原6.18 Mesa入口、旧回退、凭据和用户数据；不清其他应用缓存，Chrome仍仅启动后按需处理。
先验证候选及回读，使用正常Android重启；不要把授权或设为默认写成实际设置/游戏/跨设备已验收。
此为设备图形默认选择，不篡改原验收父包的kernel-baseline完整性pins。

## 2026-11-07：启动后遇相关问题才按需清理 Chrome 图形缓存（当前规则）

用户最新要求：**先推进启动测试，启动后遇到相关问题时，再按需清理 Chrome 已确认的图形缓存；直接删除，不需要缓存备份。**
本节覆盖旧的全应用清理/备份要求及同日较早的“每次切换先清 Chrome”规则；取消切换必清及预启动清理/回执门槛，不得继续运行旧的全应用扫描、分类、备份或清理流程。

- 只动态发现各 Android 用户/工作资料中实际安装的 Chrome 及其真实数据/浏览器资料路径；不固定用户编号，不扩展到其他浏览器、WebView 或其他应用。
- 只删除确认可重建的 Chrome GPU、Shader、Vulkan pipeline、OpenGL program、ANGLE/Dawn/Graphite 图形缓存。例如已确认的 GPUPersistentCache、GPUCache、DawnGraphiteCache、DawnWebGPUCache 及 Chrome 所属的系统图形缓存；示例不是整个目录树的删除授权。
- 不清空 Chrome 应用数据、整个 cache/code_cache、网页普通缓存、账号、Cookie、历史、密码、凭据、下载、书签或设置；未知用途文件不删。其他应用的数据及图形缓存均不处理。
- 仅在启动后相关问题需要清理时，先停止 Chrome，完成后才重启 Chrome；驱动切换所必需的图形消费者停启另按实际流程执行，不以清缓存为由停止所有应用或桌面。
- 图形缓存直接删除，不复制、不打包、不改名移入缓存备份。仅记录处理路径、数量和结果，不留缓存内容副本；明确缓存删除本身不可回滚，后续由 Chrome 重建。
- Mesa↔高通双向切换及普通启动均不自动或强制清理缓存；启动前不以缓存清理完成为准入条件。启动后没有相关问题就不清理，出现相关问题再按需仅处理 Chrome。
- “不备份”仅指本次图形缓存：原驱动、6.18 Mesa 基线、启动回退和代码/配置恢复能力仍保留；不因此删除既有历史备份。
- 旧全应用或 Chrome-only 清理回执/控制器均不得作为启动前置条件或偷偷执行清理。按需清理的记录只证明实际处理结果，不伪造完成回执，也不把未清理当作不能试启。
- 规范修改不等于已清理真机或完成高通冷启动验收；记录实际后端、清理范围与失败项，不将 Chrome 结果推广为其他应用通过。

## 2026-11-07：唯一编译基线切换到已验收 Linux 6.18 LTS

用户明确要求保存全部进度、提交主线、清理6.3.8专用编译工具，并保持当前运行内容作为唯一编译基线。本节覆盖旧版本/档位/入口的默认规则。
- 唯一受支持内核系列6.18，当前精确版本6.18.60-sq2-android1。统一入口 scripts/build.py；其他兼容入口同一CLI，不允许旧6.3.8载荷覆盖新包。
- 当前验收包实际为Mesa、原生60Hz、GPU700/RPMh432、unrestricted suspend/nohibernate。按文件hash与有效kernel-options保持原样，不照历史文档切换高通闭源、661档或其他参数。保持已有热保护。
- configs/kernel-baseline.json固定发布载荷内容；configs/kernel-6.18-baseline.config固定实际编译配置。package精确保存已验收包；compile是真实源码编译，两者不可混称，新编译件不得自动替换已验收模块/载荷。
- 内核本体与外挂SPI/SSAM/Venus/IPA/温控等是配套基线；不得只编内核就声称保持现机全部功能。源归档、私有发布包与工具链保留在private/build-inputs/kernel-6.18，私有SSH/Wi-Fi/校准不进Git。
- 删除的是旧版本专用构建链，不删除设备已有6.3.8安全回退、私有恢复备份或仅作移植参考的历史源码。当前任务不部署、不重启。
- 用户确认现机其他功能已测，Scene Root软链接修正已确认；Scene GPU读数适配仍未完成。不得据此声称新6.18镜像已完成SQ1跨机、所有GPU应用或长时稳定性认证。

## 2026-11-05：图形缓存策略历史（已被 2026-11-07 规则替代）

当时要求全应用图形缓存清理，实际仅有 Chrome 五处缓存备份/恢复验证及用户反馈。
此处只保留历史证据边界，不再授权全应用扫描、清理或新建缓存备份；当前执行上方 Chrome-only 规则。

## 2026-11-05：GPU 默认回退 661/416，700/432 改为显式实验档

用户要求修复 out/sq2-black-screen-20261105 记录的 GPU/msm 问题。证据：两次复现首次事件
均为 `HFI_H2F_MSG_GX_BW_PERF_VOTE` 无应答→GMU watchdog→recover 死循环；coredump 的 HFI
队列显示卡住的是 perf_index 10（700MHz）。700/432 自 2026-10-30 起为默认，当时只验证
“能开机”。已把 `build-sq2-gpu-boot-payload.py:build()` 回退为固定 661/416 原样输出
（a5a25e43…），700/432（6db562c2…）仅保留 `build_700()` 与 `--gpu-profile experimental-700`。
菜单标签、configs/sq2-gpu-opp.json、README/BUILD/STANDARD-BUILD 同步。此改动覆盖下方
2026-11-02“默认 GPU700/RPMh432”的默认项，但保留该档为可显式开启的实验。
实际落地：设备恢复后只改了默认入口的 DTB（`/EFI/SQ2MesaDev/android-el2.dtb`
e9264ca6… → fadd4771…：删 opp-700000000、661 档 432→416，其余逐字节校验不变，原件留
`.gpu700.bak` 与主机备份）；回退入口仍保留 700 作为安全项。没有重启、没有改内核/initramfs/
Mesa。661 下需长时负载验证；不得把这次回退写成已证根因或已修复黑屏。

## 2026-11-04：接回81c5c67的本机LTE自动采集构建链

用户明确要求按81c5c673bb695a71a9017c2cf1bf6bd6ddf457aa修复标准通用构建。
此项覆盖下方“保留旧LTE兼容/FSG打包路径”的合并决策：默认EFI router在EL2前采集
本机FSG并使用本机UUID缓存，RAM seed核验来源/CRC后提供RMTFS，Radio为唯一bearer owner。
构建不读取或嵌入modem_fsg.bin、unit.json、单机APN/serial；使用源码AOSP APN表。
恢复上游退休的旧Java NetworkAgent删除与其旧基线测试删除。正常unrestricted和
screen_only=0、nohibernate、共享固件、双机档位、Mesa回退保持，不恢复must-Y门禁。
UUID只是动态缓存归属，不是样机白名单；实际缓存含私有校准，不能分发。
本次只构建、不部署/重启；LTE构建通用化不证明双机新整镜像/长睡稳定性。
标准组包仍使用指定模板和runtime中的原有SSH身份；本地产物不可当匿名公共镜像发布。
U4阶段候选仍是独立打包入口，不宣称标准镜像已自动包含全部U4诊断覆盖层。

## 2026-11-04 retired device-bound diagnostic tools

User requested removal of the old single-machine diagnostics after mainline audit.
Do not restore lte-fs12-seed, lte-fsg-seed, el2-preboot-init or their dedicated
packers/tests from historical merges. Keep normal LTE and U4 paths unchanged.
prepare-sq2-el2-preboot.py remains a shared EFI preparation tool; only its old
RAM-diagnostic packer was retired. Do not bypass NV/calibration or disk safety
checks to revive these tools. Historical documentation is not an active entry.

## 2026-11-04 user-directed mainline integration

On merge conflicts preserve the current unrestricted U4 capabilities and startup
flow. Retain the current LTE/runtime compatibility path, not the newly introduced
EFI LTE router as the default. Incoming touch v7 source changes are retained.
U4 persistent-default ZIP remains private/offline-only, pm_test=devices; do not
silently deploy it or call idle stability repaired. Candidate WSA bootstrap source
is not cold-boot verified. This merge does not authorize device writes or reboot.

# Working agreement

## 2026-11-04 user-directed unrestricted suspend environment

The user explicitly confirmed disabling `suspend.screen_only` and removing every
feature module's must-be-Y dependency. The new default build policy is
`--suspend-policy unrestricted`: boot with `suspend.screen_only=0` and
`suspend.screen_only_autosuspend=0`; no owner registration, trial counter,
admission deadline, diagnostic framework JAR or lab keep-awake hook. This supersedes
historical mandatory-screen-only and owner-only normal-policy requirements below.
Keep the original working Mesa fallback, original payloads and shared firmware.
Do not confuse the real display/screen-on state or KEEP_SCREEN_ON with this gate.
Kernel/module ABI, target-disk validation, firmware integrity, thermal protection,
credentials and nohibernate remain. Android's legitimate wake locks and user
settings remain authoritative. No claim that unrestricted suspend fixes black screen.

## 2026-11-04 user-directed normal power policy

The user explicitly requested normal Android suspend and lockscreen defaults, deployment
on the current device and a push of the development branch. This supersedes historical
screen-only-only/unattended-native prohibitions for the normal entry. Retain screen_only
as a boundary for unregistered callers; the verified Android suspend service gets the
persistent owner gate (01-framework-owner, NOT 02-native-single-request). Keep nohibernate,
matching SPI PM recovery, credentials and the original screen-only Mesa fallback. Remove
lab wakelocks and diagnostic SSAM delays; use stock framework policy. Restore timeouts
only on explicit request, never overwrite user choices on every boot. Do not call single
SQ2 results universal or claim battery efficiency/continuous stability without evidence.

Read docs/HANDOFF.md and docs/BUILD.md before changing the port.

## SPX-PORTABLE-SQ1-SQ2：通用开发与镜像交付（2026-11-03，最高优先级）

- **任意 SQ1/SQ2，不绑定调试样机。** 项目交付目标是同一份标准镜像可在任意 SQ1 或 SQ2 设备上运行，不得要求按每台机器重新编译、改代码或替换设备专属启动包。禁止单机身份绑定；本节覆盖下方历史单机实验约定，完整规范见 `docs/DEVELOPMENT.md`。
- **禁止单机准入。** 不得以某台机器的序列号、NVMe 型号/序列、MAC、IP/SSH 地址、SSH 主机指纹、原盘 UUID/ESP 标记、Android 用户编号或历史采集 SHA 作为通用构建/正常启动的准入条件。日志和实验对象可以记录这些值，但它们不是产品运行条件。
- **按运行时硬件事实适配。** SQ1/SQ2 差异通过型号/SoC、硬件能力与运行时资源发现处理；不得固定一次启动的 phandle、eventN、cardN、节点编号或调试主机路径。硬件类别/版本 quirk 必须有硬件依据，不把某台机器的触摸测量结果推广到所有设备。
- **镜像与本机状态分离。** 通用镜像不得复制调试机的密码/shadow、SSH 私钥/主机身份、Wi-Fi 凭据、IMEI/NV/FSG 校准、用户数据或触摸校准到其他机器。需要的设备唯一数据应由目标设备自己的存储或本机首次配置提供；不得把更换私有文件重新打包当作跨设备启动前提。显式实验室私有包必须单独标记，不能称为通用交付。
- **不撤销安全保护。** 模块/固件 ABI、构建产物完整性、真实硬件兼容性、SMMU/资源边界、screen_only/nohibernate、不可重复启动固件等保护保留。写盘前仍需动态识别本次目标盘/ESP/根源并防止误写；禁止单机白名单不等于允许盲写、清空 userdata 或热重置提供者。
- **跨设备验收，诚实标界。** 同一产物覆盖 SQ1/SQ2、默认/Mesa 两入口以及不同设备身份、容量和枚举资源的回归；旧数据升级不得破坏凭据/用户设置。单台样机通过只证明该次观测，不能声称所有设备已实测。现存单机假设/私有包须列为待整改，不因修改规范就宣称现有镜像已完全通用。

## LTE 执行与收尾约定（2026-11-04）

- 按用户要求跳过离线功能验证/QEMU 前置环节，直接推进已指定真机；一次实验连续完成一组有依赖的改动、采集与观察，减少重启与反复确认。保留原件、可执行恢复、实际访问/内存边界及私有资料保护，不扩展成无意义门禁。
- 复用 SQ2 已完成的 Android LTE 验收。已得到 FSG 地址即采集，不因旧哈希、固定序列/IP/UUID或猜测状态增加 helper 拦截；新的失败做针对性修正。正常运行不携带 LTE 观测器、测试入口、原始协议/寄存器转储或第二个 bearer owner。
- 当前正确路径为本机 EFI GET_INFO/GPT/FSG → 本机 UUID 缓存 → RAM RMTFS → RF/注册 → IPA4.1/RMNET → 原生 Radio AIDL/Android 电话框架。不得将一台设备校准内嵌为通用输入。详见 `docs/LTE-PRODUCT.md` 与 `docs/LTE-EXPERIMENTS.md`。

- Default build/menu (2026-11-02 user instruction): this SQ2 now targets EL2 + Qualcomm Vulkan + native 60 Hz + GPU700/RPMh432. **The GPU700/RPMh432 part of the default was reverted on 2026-11-05 (see the top entry): the default is GPU661/OEM416 and 700/432 is opt-in via `--gpu-profile experimental-700`.** The menu/entries, EL2/Vulkan/60Hz and the rest of this instruction still stand: keep exactly the new default and working Mesa fallback entries; this supersedes the older rescue/shutdown-menu requirement below. Preserve shared GRUB/firmware and both payloads. Normal EL2 uses sq2.el2=1, without experiment recovery timers or continuous diagnostics. Do not restore sq2.el2=experimental in the normal launcher. See the latest HANDOFF for actual reboot results.

- Wi-Fi debugging lesson (2026-11-01): test the full Android candidate early once storage, the screen-only guard and recovery are verified. A failure in a stripped diagnostic initramfs does not establish an EL2 hardware limitation. Compare against the known-working full boot before adding EFI/SCM/SMEM mechanisms; see the debugging retrospective in docs/SQ1-EL2-PREPARATION.md.
- Before each hardware experiment, record one hypothesis, the changed variable, the observation that distinguishes outcomes, and the stop condition. After two consecutive experiments fail to add distinguishing evidence, stop extending that route and return to the working baseline/environment comparison. This is a change of debugging direction, not a requirement to ask the user for permission.
- Verify effective packaged/runtime files, firmware, DT, module dependencies/load order and original error payloads before rewriting a subsystem. Separate board Wi-Fi from guest forwarding; preserve fixes already proven in full Android. Functionality passing does not prove the historical failure's root cause.
- EL2 audio (2026-11-02): the adopted ADSP reports `attached`, not `running`. Accept both active states for the identified ADSP; never reset it to change the state string. Full EL2 boot checks must include the physical ALSA card, AUDIO_READY and actual HAL hash, not only Android boot completion/video/Terminal readiness.
- EL2 stop-pop (2026-11-02): use `patch-sq2-audio-sequencing.py --machine-only`. Trace showed prepare resetting SoundWire while PA was enabled; the SDM845-only patch mutes both PAs before reset and the existing prepare path unmutes after enable. User confirmed sound, no stop-pop, 10-second reopen and the original app. Keep the original WSA881x driver; the combined PRE_PMD version silenced playback and remains excluded. Pin the accepted module pair; HAL/helper/PCM success alone is not listening proof. Extended acoustic testing remains pending.
- Normal EL2 must not open the Terminal UI or run the KVM witness at boot (2026-11-02 user instruction). Keep the explicit passwordless-user login and required AVF/network preparation; the autostart helper exits after login on sq2.el2=1. KVM witness is limited to explicit experimental/debug boot. Remove temporary audio tracing/probes after testing.

- Terminal autologin (2026-11-01): user 10 was verified passwordless (is-user-secure=false, get-disabled=true). The explicitly selected EL2 lab hook now starts/switches this existing user and opens its existing image; a full unattended reboot reached RUNNING_UNLOCKED and Debian shell/HTTPS. Check user/security state before asking for manual unlock. Never infer a credential problem from active user 0; secure users are refused by this helper. No credentials were cleared.

- Shared storage was repaired by loading FUSE before Android init. Both emulated volumes mount and the user confirmed Files and Chrome downloads work. Preserve this fix; never format userdata to repair a mount failure.
- Suspend research was explicitly resumed on 2026-11-03 on `codex/suspend-fix-5.94-20261103`. Preserve normal screen_only/nohibernate and both original payloads. Debug stages and any coordinated single-task trial use isolated opt-in launchers; never globally disable the guard or turn on unattended native autosuspend. The prior forced-wakelock control black-screen failure remains unresolved; see docs/SURFACE-SUSPEND-20261103.md.
- SPI-HID PM regression (2026-11-03): retain the existing SPI sleep patch in EL2 kernel builds. Check actual module/build-id, resume_count and suspend/resume callbacks; prepare/complete alone proves no device sleep. One isolated SQ2 trial recovered by PMIC after temporary per-request SSAM wake exclusion; this does not authorize globally disabling SSAM or promoting native autosuspend. See the latest suspend record.
- Audio work was resumed by the user on 2026-10-27: investigate stop-pop, low/thin/compressed sound and stereo image. Keep gain changes bounded, preserve matching DAC/COMP/BOOST SoundWire ports, and distinguish listening results from register/PCM checks.
- Microphone on 2026-10-28: preserve DMIC0/1 -> MultiMedia2 (CARD_0_DEV_1), raw DEC gain 96, stereo 48 kHz policy and HAL 774cd7e7… with only its built-in microphone address overwrite removed. Stock playback/capture transport is retained; do not reapply the old pacing/MonoPipe experiment. Actual HAL inode/hash and recording/playback were verified after reboot; user confirmed both AudioRecord formats and original diagnostics. Sensor/camera work is deferred at user request; current P1 is Wild Life misrendering despite completed scores, see docs/TODO.md.
- Preserve mapper import/multiplane graphics and touch state fixes. Default contact detection/stability plus AspectMax=2.0 were validated; do not reintroduce high detection thresholds.
- Wild Life compatibility on 2026-10-28: preserve the Turnip sysmem workaround scoped to chip 0x06080001 and com.futuremark.dmandroid.application. Fixed sysmem was user-confirmed normal; scoped patch passed reboot and a full Wild Life run with normal sampled scenes (5264 / 31.52 FPS), then the identical image was promoted to default. This is not a GMEM root-cause fix or proof for other games. Keep other apps' normal heuristics; see docs/WILD-LIFE.md. Respect the user's latest 30-minute screen-off/lock-delay preference and retain credentials.
- GPU update on 2026-10-28: normal boot uses the tested 661 MHz DTB, original 514 MHz DTB remains for rescue. Preserve the matching early-loaded TSENS module and GPU-top 75C passive cooling / 5C hysteresis. Added frequencies use the existing TURBO_L1 firmware corner, not a verified OEM mV curve. SQ2's marketed Adreno 690 still uses SC8180X/A680.1; do not substitute SC8280XP identity/firmware.
- CPU adaptation on 2026-10-28: normal boot now loads the original qcom-cpufreq-hw module and adds missing hardware-LUT-backed 3072/3148.8 MHz big-core OPPs. Preserve schedutil, hardware-provided voltages, cluster passive cooling at 80C / 5C hysteresis, and rescue/sq2.cpufreq=0 opt-out. Reboot and eight-core 20-second checked load passed at 1.7664/3.1488 GHz reported frequency; sampled CPU peak 53.3C. Both thermal maps passed emulation; user confirmed desktop/touch/video normal. Long-term stability and battery testing remain pending.
- Resource monitor on 2026-10-28: preserve resource-monitor-start, SQ2 Thermal HAL sensor mapping and sq2_battery_compat.ko. The module adds read-only energy-to-charge/current/SOH attributes to BAT1 without replacing the Surface battery driver or charging control. Capacity in mAh is estimated at design voltage; 75% SOH is full/design energy, not a fabricated GOOD fault-health status. No battery temperature is available; never substitute CPU temperature. Real charger unplug/replug and reboot passed, user confirmed UI. sq2.monitor=0 skips the adapter; kernel CPU/GPU cooling remains authoritative.
- SQ2 edge ghost taps were reproduced in operator-confirmed idle captures. Preserve the 045E:0C21 / 46x68 heatmap quirk: interpolate only four observed near-saturated edge spikes from their original neighboring cells before contact fitting. Keep normal thresholds and immediate onset/release; the two-frame onset experiment was insufficient and was removed. After a normal reboot, the binary hash matched, a user-confirmed idle 30-second capture had zero touches, and the user confirmed normal corner taps/drag/pinch with no automatic clicks. Extended observation remains pending.
- Keep the normal audio and explicit rescue entries plus shutdown. The user removed the old no-audio second entry. Normal boot must not run diagnostic Wi-Fi association/DHCP, rollback or continuous logging; Android owns saved Wi-Fi state. The user subsequently requested automatic public-key SSH: start it asynchronously after Android boot via ssh-framework-boot (sq2.ssh=0 opts out), without enabling diagnostic networking. Rescue or sq2.debug=1 retains full diagnostics. Audit concatenated initramfs in last-entry-wins order; back up, validate gzip/hash, sync and replace atomically.
- Identify the physical disk, ESP marker, UUIDs and root source before disk operations. Old USB scripts deliberately reject NVMe; do not weaken guards to reuse them blindly. Never touch the workstation system disk.
- Do not routinely unload active QCA Bluetooth transport modules: this previously hung the board. Use the documented HAL/bridge path.
- Initialize Bluetooth transport only after Android boot completion; preserve BluetoothManager's persisted bluetooth_on choice. Do not force enable on boot or add a second state file that can override airplane/user policy.
- Distinguish framework restarts (system_server/SurfaceFlinger PIDs) from board reboots (boot ID). A user long-press reboot is not an autonomous crash.
- PM debug-stage completion does not prove actual suspend works. A framework wake lock does not reliably prevent sleep on this port.
- Keep private/, out/, downloads/, upstream/, work/, keys, credentials and raw logs out of Git. Use the operator's own SSH identity and host verification.
- The user subsequently enabled root password login on the current lab SSH server. Preserve public-key access too. The password and shadow entry stay private; local password-ssh addon must remain last when repacking this device. Standard public builds still default to key-only.
- Report observations and limits honestly; do not label untested patches as working features.
- Root/permissive/debug builds are authorized in this lab. This does not authorize erasing an unidentified disk or publishing secrets.

- 2026-11-04 用户要求：后续任务不要擅自新增按次数、持续时间或异常区域大小拒绝处理的产品门槛。触摸检测已发现异常时，用现有证据生成可撤销候选并进入复测；不能把生成候选写成修复成功。保留设备身份、数据解析、回滚与明确保存机制，不虚构未采到的数据。
