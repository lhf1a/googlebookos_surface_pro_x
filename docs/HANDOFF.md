## 2026-11-05：6.18 首启 EFI Not Found，补齐 DT 握手后待复测

用户从回退入口恢复后报告默认项 `preboot prepare/EFI_NOT_FOUND`。本轮实际读取确认
仍运行 6.3.8/SQ2Fallback，而默认 SQ2MesaDev 的 Image 已为 6.18，菜单也已改为两项/6.18 默认；
旧迁移文档“三项/默认未改/未部署”不再代表当前状态。错误发生在 Linux LoadImage 之前。

默认新 DTB 缺 `microsoft,el2-preboot-rmtfs`，正对应真实 `sq2_rmtfs_prepare` 返回 Not Found
的条件；三 DSP 和根节点握手槽也漏带。新增独立准备器只添加 18 项 EFI ABI 属性，
四节点完成位保持0、不伪造成功；6.18 原资源/phandle/firmware/OPP逐属性未变。
真实 C+libfdt 主机测试（EFI 分配桩、无SCM）B/M/R 为 NotFound/Success/NotFound，
exit14/0/14；rollback 恢复原hash、patch重建一致、7测试通过，不是真机认证或开机通过。

只原子替换设备默认 DTB：e91bd896… → 6c6afe67…（88771 bytes），完整回读一致；原件留
`/media/esp/EFI/SQ2MesaDev/android-el2.dtb.before618-handshake-20261105-e91bd896`。
默认 Image/initramfs/EFI、GRUB 与全部回退文件hash不变，boot ID不变，未重启、未切图形驱动。
四角色固定在 `out/kernel-6.18-migration-20261105/`；详情与下一步实验边界见
`docs/KERNEL-6.18-MIGRATION.md` §0。下一步复测是否越过 preboot prepare；热管理等缺项未完成，
不做压力测试、不宣称整树迁移或黑屏修复已验收。

## 2026-11-05：高通与 Mesa 切换后必须清理所有应用图形缓存

用户明确要求：**每次高通闭源与 Mesa 驱动双向切换后，在重新启动应用前，必须清理所有应用的图形缓存，不限于 Chrome。**

- 范围覆盖设备上所有 Android 用户/工作资料及其应用：实际存在的 GPU、Shader、Vulkan pipeline、OpenGL program、ANGLE/Dawn/Graphite 等可重建图形缓存；动态发现用户和应用路径，不绑定样机、用户编号或固定目录白名单。
- 只处理可确认的图形派生缓存。不得以“清缓存”为由清空应用数据、整个 cache 目录、账号、Cookie、历史、下载、密码或凭据；未知用途文件先辨识，不盲删。
- 切换前停止相关应用/图形消费者，保留原驱动和可恢复备份；切换后使旧后端图形缓存失效，再启动应用并记录实际后端及结果。无法清理的应用应明确报告，不能静默声称已完成全应用清理。
- 清理与实际驱动切换关联，不在每次普通启动时无条件清理。保留 Mesa 回退入口与恢复能力；回退到另一后端也遵守同一要求。
- 证据边界：本轮只对 Chrome 的五处图形缓存做了备份移走及哈希一致的文件恢复验证，用户确认随后 Chrome 正常。所有应用清理是新增操作规范，不是已经完成的全应用实测；缓存相关性不等于已证明所有 GPU/GMU 故障根因。

本次 Chrome 处理：GPUPersistentCache、Default/GPUCache、Default/DawnGraphiteCache、Default/DawnWebGPUCache、设备加密存储中的 com.android.opengl.shaders_cache.multifile。原有 11 月 2 日备份保留；本轮约 12.8 MiB，未清浏览器数据。证据在本机 D:\spx_gbos\NTEGraphicsProbeSuite\device-test\chrome-cache\VERIFICATION.txt。

## 2026-11-05：SQ2 黑屏定位与 GPU 默认回退到已验证的 661/416（未部署）

用户指定 out/sq2-black-screen-20261105 的 GPU/msm 问题。两次实机复现的首次事件完全
同类：`HFI_H2F_MSG_GX_BW_PERF_VOTE` 5ms 内无应答（基线 seq 3615、候选 seq 950），
随后 `GMU watchdog expired`，再往后 hangcheck/`recover` 无限循环，SurfaceFlinger 卡在
`DRM_MSM_WAIT_FENCE`，最终整机 UI 挂死。从候选 devcoredump 的 gmu-hfi 段解出 host→GMU
消息流与 perf 表：GMU 收到 11 档（0=off、1=177MHz…9=661MHz、10=700MHz），卡住的那条
vote 正是 perf_index=10；同档前几条已成功消费，所以不是"档位非法"，而是该档下的
DVFS/RPMh 事务未能完成。

恢复路径两次失败点不同但同源：基线是 `a6xx_rpmh_start` 报 Unable to power on the GPU RSC
（PDC/RSC 序列卡死后每次 recover 都失败）；上一轮候选（已移植上游 PDC 序列）RSC 交换
通过，但 2 秒后 `GMU firmware initialization timed out`——CX/GDSC 没有真正复位。主线在
`a6xx_recover` 用 genpd notifier + `dev_pm_genpd_synced_poweroff(cxpd)` 强制 CX GDSC 崩溃并
检测（6.3.8 只有旧的 `reset_control_reset(cx_collapse)`），这是下一步若要修“自愈”的落点。

本次改动只做一件事：把 GPU 默认档回退到唯一没有观察到 GPU/GMU fault/hang/timeout 的
实机配置。`sq2-gpu-boot-payload.py` 的 `build()` 现在原样返回固定 661/416 DTB
(a5a25e43…)，700/432 (6db562c2…) 只保留为 `build_700()` / `--gpu-profile experimental-700`
显式实验档；`configs/sq2-gpu-opp.json`、菜单标签、README/STANDARD-BUILD/BUILD 同步。
理由是 700/432 在 2026-10-30 设为默认时只确认“能开机”，负载稳定性从未验证，而本次锁死
正发生在它作为默认期间。

实际落地（设备中途掉线后又恢复）：核对发现已部署 DTB 不是仓库固定哈希，而是
`SQ2MesaDev/android-el2.dtb` = e9264ca6…、`SQ2Fallback/android-el2.dtb` = 85b436a3…，
两者都是 590@416 / 661@432 / 700@432。用 `strip-700-opp.py` 只改默认项：删除
`opp-700000000` 并把 `opp-661000000` 的 level 从 432 改回 OEM 416，其余节点/属性/保留内存、
GPU 与 GMU compatible 全部逐字节校验不变，结果 e9264ca6… → fadd4771…（9 档，末档 661@416）；
原件留在设备 `.gpu700.bak` 与主机 `gpu661-revert/backup-7e55a199/`。回退项**故意保留 700**
作为可启动安全项，菜单标签暂仍写 GPU700。

明确未做：没有重启（DTB 只在启动时读取，需用户自行重启生效）、没有改内核/initramfs/Mesa、
没有动 SQ2Default/SQ2Free/SQ2Power 的 ip4 DTB。因此这不是已证根因修复：661 下仍需长时负载
验证才能宣称黑屏解决；若在 661 复现，下一步按上文做 CX GDSC/PDC 恢复硬化，而不是再调频率。
确认默认项在 661 上启动正常后，再对回退项做同样处理并同步菜单标签。

## 2026-11-04：接回81c5c67的本机LTE自动采集构建链

实际标准构建exit0：在不含FSG/unit.json的输入目录完成Radio、缓存解析器、EFI router及
SQ1/SQ2默认/Mesa整镜像。四载荷及整盘ESP逐文件回读、非ESP模板字节比对、gzip解压
SHA一致；压缩文件8558884881 bytes，SHA256
6826c6ce0b22db4fbbdf26ba83f3ae40c61038aa55d230eb0f5ad70334a8e2b9。
触摸缓存落后主线，已以固定归档和现有依赖重编译当前源码，不绕过源清单匹配。
构建路径B/M/R同检查为内嵌FSG true/false/true、EFI本机采集 false/true/false，均exit0，
独立源副本回退hash一致；这是源码/产物验证，不是真机新启动或LTE联网验收。

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

## 2026-11-04：移除旧单机诊断工具（不改变正常启动）

用户要求清除仍有单机限制的旧诊断工具。本次移除lte-fs12-seed、lte-fsg-seed、
el2-preboot-init及两个专用LTE打包器、prepare-sq2-el2-preboot-ram.py和两份专用测试，
共8文件；删除unrestricted转换器中仅服务旧preboot-init的特例。历史引用标记退役，
原始实现可从Git历史取回，不再作为可运行入口。没有将NVMe serial/ESP标记检查直接
删掉后继续允许旧工具采用校准数据，也没有增加替代的单机白名单。

正常LTE lte-unit-seed/lte-unit-guard/lte-framework-boot、标准build.py、U4打包器及Mesa
入口字节不变；prepare-sq2-el2-preboot.py是仍被EFI准备链使用的共享工具，予以保留，
不要与已删除的preboot-ram.py混淆。没有设备访问、写ESP、重启或新的睡眠试验。
这不代表所有常规组件枚举/模板依赖已清理，更不表示十分钟挂死已修复。

## 2026-11-04：当前U4开发版本合入主线（未部署）

按用户“冲突保持现在能力和启动流程”：保留unrestricted策略、Venus system PM、QCA最终
失败清理、GENI UART补丁、SPI/FUSE/音视频既有链路。7处文本/删除冲突选当前版本；另
解决LTE语义冲突：不以主线新EFI FSG/router替换当前启动路径，保留当前LTE兼容工具及
打包依赖。上游新增LTE实现保留为尚未接入默认的源码；旧主线“已默认接入”描述在本合并
中不代表当前入口。主线触控v7及对应改动保留，未作本机重新部署或声学/触控验收。

U4固定默认仅指上轮私有离线ZIP；包、身份、日志均不入Git，设备仍原SQ2Free。
pm_test=devices诊断包不改为真实连续睡眠；未冷启动WSA helper源码仍明确标候选。
十分钟静置无响应仍未解决，不以合并代码替代实机验收。不写ESP、不重启、不推送远端。

## 2026-11-04：按用户确认制作U4离线固定默认启动包（未写真机）

用户明确仅制作离线默认启动包。新私有增量ZIP逐字复用U4 Imagef64bc678、overlayd339e6de、
EFIfbea8768；未重新编译内核或加入未冷启动WSA新版。只把包内sq2-default的条件chainloader
改为固定/EFI/SQ2U4/devices.efi，不消费boot-once.env；恰两项，原Mesa第二项保持。
原SQ2Free/base initramfs/共享固件DT/Mesa是已有部署依赖，包内列明SHA，不是完整重装镜像。
pm_test=devices、诊断trace及RAM启动日志原样保持；不是持续真睡或pstore持久化修复。
菜单B/M/R同检查：conditional true/false/true，默认目标U4+Free/U4/U4+Free，均exit0；
主ROLLBACK新增u4-offline-menu模式恢复原字节，patch重建匹配；ZIP逐文件回读匹配。
无设备访问、无ESP写入、无重启，新默认入口未真机启动验证，十分钟静置挂死仍未解决。

## 2026-11-04：十分钟静置再次无响应，用户手动重启；不得标记睡眠已修复

用户反馈“我静置十分钟后，睡死了，已重启恢复”。此前最后确认U4 boot3c371f1a…，
pm_test=devices、debug0；故障瞬间是否改变阶段及确切卡点无日志，不能直接断定进入了
真实低功耗，更不能把SSAM早醒直接等同这次挂死根因。短程视频/声音/输入通过仍成立，
但长期静置稳定性明确失败。没有安排新睡眠、修改用户电源策略、重启或部署新候选。

首次SSH超时exit255，随后保留相同主机验证/公钥增加verbose只读诊断exit0，连接已恢复。
新boot a20344b4-f727-402f-b01e-ce5f0145ed26，实际cmdline仅原SQ2Power base + SQ2Free
覆盖层，内核#2 Nov3，pm_test none，N/autoN/testN/trialN，当前success/fail0/0。
证实一次性U4入口已回原默认；这些重启后计数不能解释旧boot最后一次挂起。

pstore已挂载但为空，/proc/last_kmsg不存在，DropBox SYSTEM_LAST_KMSG及SYSTEM_RESTART
无条目。当前dmesg不是旧boot日志；不能据缺少panic记录排除kernel hang/panic。
已读取保存持久DropBox与近期tombstone：8份压缩记录和2份当前记录均是gscd因/dev/tpm0
不存在而SIGABRT。重启导入文件时间20:07，内嵌旧记录时间19:57–19:58；必须使用载荷时间，
不能把导入时间当崩溃时间。这是反复服务崩溃线索，尚无证据证明导致整机挂死，不禁用
安全服务来冒充休眠修复。原记录与命令/输出/退出码保存在本次POST_IDLE_HANG目录。

下一步先确认故障时接线/合盖条件，补足跨重启保留的PM阶段证据；在有区分性取证前不再
盲目等待十分钟复现、不提升U4默认、不屏蔽SSAM唤醒。继续保留既有四角色及B/M/R证据。

## 2026-11-04：U4恢复正常，音频健康起点重试仍被SSAM提前唤醒
<!-- U4_AUDIO_HEALTHY_RETRY_20261104 -->

本节更新下方候选“尚待实机”的阶段状态；同一工作区/分支，不回写父源码，不重复已完成
离线矩阵。本轮U4实际boot为3c371f1a-416f-4881-97e8-f65373002f09，Imagef64bc678…、
Venus coree46fdfe6…/decf0bfdf50…、hci_uart8772d304…均已实读。相同platform输入write0，
success0→1/fail0，Venus stop/start1/1，未再见上一内核的GENI UART IOMMU重复unmap WARN。
随后第一次显式pm_test=none/freeze write0，success1→2/fail0，八CPU s2idle计数增加，
478.423196→478.429939仅6.743ms即被SSAM IRQ173唤醒；不是30秒低功耗驻留验收。
两次阶段/真请求的UART未见旧WARN不等于竞态已穷尽证明；QCA初始化成功未走新增最终
失败清理分支，不能把启动成功归因于该补丁。原noTZ固件/hash/stream完整性保护保持。

用户随后说内置无声。原始RAM启动kmsg在52秒已有SWR FIFO/WSA读-5，早于首次PM241秒；
左WSA UNATTACHED/numberN/A，右Attached/number1，而ALSA/AUDIO_READY/HAL774均存在。
不能仅因反馈发生在PM之后就归因为睡眠丢音。ADSP始终attached，不重置它或SoundWire主端。
只在停止HAL/audioserver、确认全部PCM closed后做一次原WSA成对runtime auto/on周期，
恢复原on/3000策略及原HAL。双WSA变Attached，动态number1/2，用户明确“恢复”。
此前相同路由开关仍-61的路线不再重复；未改WSA驱动/SDM845 machine-only时序/增益/端口。

audio-drivers-start副本已移除会真实读寄存器的全量tinymix contents，并以实际wsa881x
驱动链接的Attached/正devnum/runtimeactive判枚举完成；仅开机部分枚举且所有PCM closed时，
允许一次现有slave runtime恢复并还原策略。不加每次睡眠恢复钩子/后台轮询/owner门禁。
原helper SHA f696da16…，新36fb57ed…；同一部分枚举夹具BASELINE/MODIFIED/ROLLBACK：
exit0/0/0、Attached1/2/1、成对runtime周期无/有/无，全量contents有/无/有；rollback原hash
相同。夹具不是硬件冷启动证明。新7条覆盖的未来诊断包overlay36142f52…有效脚本逐字匹配，
身份member仍最后且未改；尚未部署/未冷启动，当前设备的恢复是上述同boot人工一次操作。

用户补充上一轮其他功能正常且自己手动点亮，不能将上一轮写成“等待全程无操作”。
随后明确“再试一次”，新测试先确认声音已恢复、双WSA Attached。首次预读仍有两个活动
解码session，未请求睡眠；用户退出播放回桌面并答“就绪”后，精确只读快照sessions0/atomic0、
无video FD，仅18项/36466688 bytes待回收。读者加载即卸载，无固件/寄存器/IRQ写入。
发新“开始”后仅一次直接内核pm_test=none; echo freeze，传输/写入均exit0：
success2→3，fail9→9，Venus stop/start2/2→3/3；已有9失败不抹去，不能说全boot零失败。
新SSAM IRQ在2318.378593触发，比2318.380757 s2idle调用早2.164ms；调用立即返回，
八CPU s2idle usage/time完全不变。SSAM计数1→2，PMIC电源键6→6，wakeup_irq173。
因此本次未形成新低功耗驻留，success计数增加也不能冒充真睡30秒。该次无新UART WARN。
双WSA前后Attached/number1、2/runtimeactive，实际HAL同PID20512/hash774；用户统一确认
“正常 自动亮起”，覆盖画面、视频声音、持续触摸和键盘。这是恢复功能通过、持续睡眠未过。
本次是直接内核freeze，不是Android原生睡眠策略/合盖/长时间耗电验收，不混淆层级。

同boot只读复核当前SSAM原模块773f191c…，文件build-id note SHA f35d1d62…与已加载note
一致；实际ssam_irq_handle反汇编除可选dynamic_debug仅返回IRQ_HANDLED，没有EC GPIO
callback排队事件释放/按事件判定唤醒。源码controller.c:2693也明示该逻辑未实现。结合
本轮IRQ早于s2idle且无电源键增加，可将下一步聚焦EC待处理事件及其真实唤醒资格；
仍未采到本轮原始EC载荷，不能认定是空键盘包、电池或某个按键，更不屏蔽整个SSAM IRQ。
不继续相同30秒计时重试；先核对现有原始采集路径与EC callback时序，再推进Android原生
对照。SSAM wakeup仍enabled、BT ON、观察模块已卸载，用户设置/接线/正常输入保持。

pm_test已恢复devices、pm_debug0；screen_only/auto/test/trial均N、owner0，不新增次数/期限/
保活门。这只是隔离阶段boot，不是正常默认持续睡眠已修复。一次性入口已消费，下次启动
仍回原SQ2Free，原SQ2Power/工作Mesa/共享GRUB、固件、DT及载荷保留。本次重试没有重启。
历史连续黑屏、多轮驻留、合盖及冷启动音频枚举修正版仍未验收，不提前提升新候选为默认。

## 2026-11-04：Venus阶段恢复通过，QCA失败清理及GENI UART候选继续真机
<!-- VENUS_QCA_GENI_LIVE_20261104 -->

按用户要求直接真机、不再扩展离线矩阵、每次启动合并检查。Venus v2在7f5a053b…
先devices后platform均write0、fail0，stops/starts2/2。第二次前真实视频遗留4项/
2834432 bytes，在确认CPU reset/5MiB unmap后回收为0，HFI/SYS_INIT恢复。用户确认
恢复后视频、声音、触摸、键盘全部正常。不是实际低功耗通过；每阶段SPI增加一次-22，
platform另有USB xHC恢复重置，继续保留原错误。

随后获新30秒配合，pm_test=none只请求一次真实freeze。实际636.078→639.531s，
在devices阶段因hci_uart_qca serial0-0等待SSR/FW状态3秒而-110，write1；success仍2、
CPU0..7 s2idle usage/time全0，未进真实睡眠，不冒称一次唤醒成功。立即恢复devices/
pm_debug0，没有自动重试。可选kretprobe静态s2idle_loop/enter已内联无符号，register -2，
读者已卸载；改用内核自有CPU s2idle计数及pm_debug边界，不把采集器失败变硬件失败。

同boot只读精确结构快照：HCI_UP/INIT/RUNNING0、SETUP1/AUTO_OFF1、power_on_work0、
queues0；QCA IBSdisabled1、BT_OFF0、SSR0、memdump0、rx/tx asleep0、所有work0。
原bridge等待300次后EBUSY退出，Android BLE_TURNING_ON，用户持久bluetooth_on=1。
没有后台初始化能再清IBS位。实机hci_uart note9886e002…与精确匹配内核build相同。

用户明确允许修改蓝牙驱动。qca_setup最终失败耗尽重试后缺少硬件shutdown，现只在该
错误分支调用已有qca_power_shutdown，保留原errno、成功路径不改。不清标记伪造下载
成功、不写用户蓝牙开关、不热卸载QCA。原K源码SHA57cc000b…不变；实际改动C副本及
native patch/可重建CLI位于私有工作目录和probe/bluetooth。相同失败输入的实际函数mock
B/M/R：均ret-110/4 attempts；shutdown3/4/3、最终off0/1/0，执行主rollback恢复原C和
原模块hash。mock不是硬件断电证明；新hci_uart8772d304…/loaded note6fab2b74…已上机。

853b1222…首次启动原始BT日志完整采集到：Product0a/SOC40010224/ROM1001，下载原
crbtfw01.tlv/crnv01.bin后setup completed；HCI SETUP0/UP1/RUNNING1/USER1、IBSdisabled0，
桥接就绪、Android ON且保存开关仍1。随后platform write0，fail0，Venus stop/start1/1。
物理ALSA/AUDIO_READY/实际HAL774及用户“全部正常”通过。此次没有触发新失败清理，
不能把初始化成功归因于该补丁，也未证明前次初始化失败根因。第一轮SoundWire无声
仅在后续重启恢复，没有音频代码改动或声称根因修复。

该platform回程新见GENI UART TX→iommu_dma_unmap_page WARN(!phys)，停止继续真睡。
源码handle_tx_dma无条件unmap/advance，而stop_tx_dma已可将address/remaining清0，
迟到TX_DMA_DONE能二次回收。已在副本将回收限定为仍有非零在途字节，后续排队/唤醒
路径照旧，不按DMA地址0判合法性，不屏蔽非零真实传输的映射错误。原内建驱动未热改。
相同late-DONE/retired0输入的实际函数mock B/M/R，unmap及advance调用1/0/1、均exit0；
原C回滚hash相同。这不是UART硬件race根因已证实。

私有mount namespace只把新C副本只读映射到原编译路径，用隔离build增量编译；日志只重编
GENI UART及版本对象。配置hash保持b8c8ea74…，原源码/原Image86d字节不变。新Image
f64bc678…（30794240B）已独立部署SQ2U4并发起启动，原正常SQ2Free/原SQ2Power、
Mesa、共享固件/DT、所有原载荷保留。新EFI fbea8768…加载独立Image，三initrd沿用
原base+Free+Venus/QCA候选；stage仍devices。一次性选择已准备，下次再启动回原Free。
新Image尚待本次启动/同platform原输入以及进一步真实睡眠，不提前标通过或提升默认。

## 2026-11-04：Venus第一轮真机区分证据与回收顺序修正（v2已重启）
<!-- VENUS_RETIRED_ORDER_V2_20261104 -->

首轮50d9612a…完整AndroidC2 running/video0，新core c270/dec2d97实读，用户确认视频、
触摸、键盘正常，但内置扬声器无声。ALSA/AUDIO_READY/实际HAL774通过不能替代听音。
首次trace目录不存在使辅助采集exit1，尚未请求PM；直接命令v2才实际echo freeze一次，
exit1，fail0→1、last Venus -16、phaseRUNNING/stops0/starts0。未进真实低功耗。

为区分此次EBUSY，临时加载只读一次snapshot模块（精确core.h结构），读取后立即卸载，
不写寄存器/IRQ/固件/PM/缓冲，不卸载主驱动。实际sessions0、atomic0、无video FD，
noTZ/fwowner/secure/HFI均1、sys_error0，但charged4960256 bytes、retired7且scope均未ended。
这是本候选把待回收缓冲错误放在停固件之前的顺序问题，不能将其称为活动视频阻塞。

已改venus_sq2_pm_idle：不再要求待回收list/charge预先为0；保留活动session拒绝和
running/DMA状态检查。venus_sq2_pm_stop仍先IRQ同步/CPU reset确认/5MiB完整unmap、
再hfi_destroy及retire_stopped，回收后才核对charge/list为0；不一致终态保留，不清零
计数造假、不在固件活动时释放缓冲。无新增helper门禁/owner/预算/超时。v2重新配对编译，
不扩展离线测试；独立SQ2Vp2已部署并发起第二次重启，仍devices诊断不是正常默认提升。
coree46fdfe6…/decf0bfdf50…、overlay2f34e57d…、EFI014874e3…，原SQ2Free/Mesa继续保留。
这次必要重启是应用内核代码修改，不重复已通过启动包的整套验收，不热改活动内核字节。

无声原错误SoundWire wr_fifo overflow/WSA DPN_PortCtrl失败、SLIM/MultiMedia1 prepare -61，
时间151s早于本轮PM命令228s，不归因于本轮设备恢复。MUSIC非静音且路由内置speaker，
ADSP保持attached，音频module/固件/HAL未改。只做一次停HAL/正常成对DAC-COMP-BOOST
路由关开/再起HAL和audioserver，无ADSP或provider reset；route shell返回0但SDW -61
仍存在，停止原样重试。用户随后明确内置仍无声；本轮不能标音频修复或候选验收。
下轮同一boot优先完成Venus真实冷停/重建区分及实际视频/音频，仍不直接真睡。

## 2026-11-04：Venus blanket EBUSY已定位，独立system-PM候选已上机待阶段结果
<!-- VENUS_SYSTEM_PM_20261104 -->

继续同一unrestricted工作区/分支；原父工作区源码未改。用户已确认可配合重启，随后
要求停止扩展离线验证、直接推进真机、单轮覆盖更多、减少重启、不重复已完成验收。
因此只沿一次新候选启动推进，不再扩展host测试矩阵；真实磁盘/DMA/固件/回退安全不删除。

实机原venus-core b9b9d835…、dec3ccf818a…及加载note/反汇编已对齐：SQ2 HFI的
venus_runtime_suspend直接-EBUSY，system PM映射pm_runtime_force_suspend，尚未调用HFI。
当前Image86d896…精确source/build隔离副本生成真实Module.symvers；重建基线core299及
decoder56个函数机器码均与实机相同，非alloc路径字符串/build-id不同，不冒称整文件SHA一致。
实机本轮同boot acf4e7a8…做一次pm_test=devices、echo freeze：write exit1，原始stderr
Device or resource busy，fail86→87、failed_suspend82→83、success0；最后aa00000.venus-sq2
suspend -16。结束pm_test恢复none。后台自然尝试另有记录，不把它们计成这一次命令。

显式prepare-sq2-venus-iris1.py --system-sleep才选新system callbacks；runtime guard仍保留。
新venus_sq2_system_suspend/resume仅空闲noTZ：核对session/DMA owner，IRQ同步后确认CPU
reset及完整5MiB unmap，再释放HFI；保留三genpd引用/七clock handles，关clock/ICC；
resume重建HFI、原已校验固件/stream和SYS_INIT。一个module-owned固件缓存避免冻结用户态
后读盘。只有确认clean-stop才清boot_attempted；stop/start失败终态隔离，不自动重试，
不把EBUSY改0，不卸载活动模块，不通过重置provider冒充成功。活动会话/未清DMA仍拒绝。

新pair core c270304f… / decoder2d97310b… 已完整modpost编译。离线实际source+mock回调同
idle/noTZ输入B/M/R：suspend -16/0/-16、resume均0，stops0/1/0、starts0/1/0；均exe exit0，
执行主ROLLBACK.sh恢复pristine fixture hash相同。16轮power handles保持、17故障点、128轮
纯状态机及旧HFI resume/内部buffer/transport回归已完成；均不是实机重启固件或真睡证明。
调用CLI误加-v、非root读cache、mock日志消除产生unused告警的原错误均保留，改调用后通过。

独立SQ2Vpm/devices.cpio.gz仅改core、decoder、native清单、startup trace参数及/init的
pm_test=devices（在Android启动前设置，整个本诊断boot不进真实低功耗）；screen_only=N、
auto=N、owner0，不加入owner/次数/期限/keepawake。原正常SQ2Free、原SQ2Power、Mesa、
共享firmware/GRUB binary、Image/DT、codec service、音频和userdata字节不覆盖。
ESP动态识别及原protected hashes通过，新增约1MiB；两项菜单one-shot选择已写入，
下次启动消费选择后再重启回当前SQ2Free。首启真实GRUB选择/消费/回原正常的既有模式已验证。
新overlay00ee0c7a…、EFI3466ce07…；新候选已部署且reboot命令已执行，等待本次boot与stage
观察。不是正常默认提升；stage通过不等于真实s2idle、合盖或原黑屏修复。后续同一boot尽量
完成区分性检查，失败留原错误并停止该机制，不热修活动驱动。

## 2026-11-04：静置窗口亮屏与输入通过，但真正挂起仍0次（最新）
<!-- UNRESTRICTED_IDLE30_POSTCHECK_20261104 -->

用户先声明静置半小时，结束确认“正常唤醒了”，并另确认20秒持续触摸拖动/键盘
“也都是正常”。期间没有常驻SSH或轮询，没有更改用户超时/合盖/供电策略，没有工具
请求新的睡眠、禁锁或重启。两次快照北京时间16:32:49→17:06:42，间隔2033.06秒。
同boot acf4e7a8…，system_server3671/SurfaceFlinger3179/native2613/C2 3099均未变。

内核success0→0，fail14→46，其中failed_freeze3→4、failed_suspend11→42；
SPI ready保持、resume_count0→0、既有bus_error1(-22)不变。原生服务也报46次尝试/
46次失败、累计睡眠0ms。最后失败仍是aa00000.venus-sq2、suspend阶段-16(EBUSY)。
因此只能验收关屏/亮屏和持续输入，不是实际挂起后恢复通过；原黑屏没有在真睡中排除。

基线时Android已Asleep，wake_lock_summary0且框架两项suspend blocker均false，
说明当时已非screen_only或框架锁拦截；回读时Awake，screen_only/auto/test/trial均N、
owner0、pm_test none。最后SleepReason是power_button，不把这轮写成严格30分钟
完全无输入、纯timeout单变量实验。两次快照电池86%→79%，均无外部电源；
没有真正挂起，不能将这个差值当深睡功耗结果。

回读Venus core runtime active、decoder子设备suspended；当时全进程FD只读扫描没找到
video节点持有者，不能倒推每个失败时刻都无会话。dmesg环主要已被audit覆盖，保留
原始可读载荷，不据最后计数虚构精确卡点。源码prepare-sq2-venus-iris1.py存在SQ2 HFI
返回-EBUSY的PM保护分支；还须核对实机模块的构建选项/对应源码，不能直接删除
保护、卸载活动驱动或重置固件来冒充“真睡修复”。本轮没有新的驱动/内核修改。

下一步对齐实际Venus模块/PM回调与no-TZ/firmware lease状态，选择能区分结果的对照；
保留已确认的视频、音频、FUSE、触控及新默认/原Mesa入口。合盖映射仍未验收。
原B/M/R准入证据保持：Y拒绝、N放行、回滚Y拒绝；source helper同N夹具1/0/1。
这些不代替实机睡眠成功。

## 2026-11-04：无限制环境实机启动、视频修复及合盖边界（最新）
<!-- UNRESTRICTED_DEPLOY_VIDEO_AND_LID_20261104 -->

独立工作区 /mnt/spx-workspace/chromebookOS-unrestricted-20261104，
分支 codex/unrestricted-power-20261104。父工作区988份源码哈希保持，没有提交/推送。
本节更新下方早先“尚未部署/15条目/模块不变”的阶段记录。

完整Android实读screen_only=N、autosuspend=N、test/trial=N、owner/token0、
pm_test none、s2idle。最终22条有效覆盖均在实机校验；正常入口已切换SQ2Free，
菜单恰为新默认+原Mesa两项，原正常包/两原载荷/共享GRUB和固件字节保留。
首次验证的一次性启动选择已移出默认菜单；没有休眠次数门、期限或实验保活钩子。
合法Android锁、用户超时/凭据、nohibernate和热/磁盘/ABI保护保留。

首次启动停在video-c2-stage：改过install.sh却保留旧SHA清单，用户现场确认仅该项
FAILED。修正为先验证原哈希，再递归刷新受影响清单；没有跳过完整性检查。
经shell reboot-f回原正常包，实机再次验证原菜单/载荷，修正版再启动到桌面。

随后用户报告bilibiliHD闪退、Chrome视频页白屏。实际原runtime.tar内start.sh
仍test N=Y而exit1，服务stopped；老助手固定VIDEOCC phandle=b5，实机b9，
在HFI/core前退出、没有video0。不是把b5改成b9：复用11月3日已验证动态provider
引用模块86b6acbc…及配对启动次序，PAS尝试标记先于core；不重置/卸载provider。
唯一更换的内核模块为该HFI overlay；内核、SPI/SSAM、DT、固件、codec service和
mapper字节保留。嵌套guard已清理，launcher由配对源码编译到独立v2u版本；
旧v1不覆盖/删除，两份实际文件清单均通过。未知嵌套限制/不匹配输入拒绝组包。

最新重启实机C2服务running、default1已注册、video0存在、全部22项hash匹配；
用户明确“已恢复”“视频正常”。两用户共享存储MOUNTED且RUNNING_UNLOCKED；
物理ALSA/AUDIO_READY/实际运行HAL774cd7e7…通过，ADSP attached按已采纳规则接受。
这不是新声学/全部codec/长期帧率认证；没有当前播放的逐帧硬件计数，不扩大用户反馈。

最新合盖只读核验：global.lid_behavior=0，WindowPolicy为LID_BEHAVIOR_NONE；
输入有KIP Tablet Mode Switch（SW bit1），没有SW_LID bit0能力。本轮未改变合盖
设置、未要求用户合盖、未请求睡眠。内核success=0、SPI resume_count=0，不能说
“所有障碍清零”。合法应用/显示blocker仍存在，亮屏时不据此诊断睡眠失败。
核验时已无充电，5分钟关屏设置保持；早先启动验收接AC，条件不混用。

QEMU同root/owner0/三次freezer输入BASELINE/MODIFIED/ROLLBACK：
Y拒绝1/1/1、N允许0/0/0、回滚Y再拒绝。真实包清单失败/通过/执行回滚后失败
为exit1/0/1；嵌套脚本同N/相同mock launch输入为1/0/1，mock不充当实机frame证据。
原连续真睡黑屏尚未修复证明，合盖睡眠/开盖唤醒/多轮恢复未验收。下一步先采实际
姿态报告并核对策略映射，现场协调后再测；不拿SW_TABLET_MODE当SW_LID，
不以功能通过替代历史根因证明。

## 2026-11-04：用户确认关闭screen_only，创建无限制环境
<!-- UNRESTRICTED_POWER_ENVIRONMENT_20261104 -->

用户明确“是，关闭 screen_only 并清理依赖”。这里不是screen_on/KEEP_SCREEN_ON。
默认源码策略变为unrestricted，实际启动参数screen_only=0、autosuspend=0；不安装/运行
owner准入或实验保活启动钩子，次数/期限/特许owner不再参与。功能模块不再因flag不是Y
而退出；原正常/Mesa载荷与共享固件不覆盖，合法Android锁/用户超时、ABI/热/磁盘保护
及nohibernate保持。完整102文件引用清单与处理分类见UNRESTRICTED-POWER.md。

源码在独立副本修改，原开发工作区代码保留。有效原正常包已重打为增量覆盖层650641字节，
最终15条目变化、所有.ko/DT/固件/凭据数据不变，身份成员仍最后。新增ESP文件约32.6MB，
不须删除旧诊断包或原载荷。实际6.3.8 Image86d896…在QEMU中同一root/owner0/连续3次
freeze+pm_test=freezer输入：BASELINE Y为1/1/1拒绝，修改N为0/0/0，回滚Y再1/1/1。
三组退出0，回滚EFI哈希回到99a811…；这只是准入/freezer阶段，不是实机低功耗稳定性。
第一次QEMU因测试入口漏掉实际lsm=selinux参数而KernelSU早期panic/超时124，原错误保留；
按实际入口补lsm=selinux enforcing=0后通过，没有为测试禁用KSU或更改内核。

旧全量unittest discovery误导入需源码参数的CLI harness并缺私有输入，失败原样保留；
改为逐文件隔离运行实际相关TestCase并更新N不再拒绝的预期。固件/hash/overlay/未知目标、
symlink、用户安全状态等其他拒绝场景保持。物理设备是否切换N另见部署记录，不能把
包参数/QEMU结果写成当前设备已关闭。原连续休眠黑屏仍未解决，未宣称本改动是根因修复。

## 2026-11-04：带Hub/Wi-Fi/USB2原生单请求早醒与输入恢复闭环

仅拔网线后默认网络101、eth0 carrier0；7次采样应用锁false但Awake/display blockertrue。
tips job189在首次采样约5分钟前自行jobFinished，不能将锁释放归因于拔线。USB树与上次
v3记录不同，两个外接USB3 Hub缺失、网卡/读卡器重新枚举为480Mbps；旧断连段已覆盖，
不能把这轮称严格单变量网络试验，也不能据此写USB驱动根因修复。

另获新的现场确认后，owner0下先普通KEYCODE_SLEEP，3次自然无框架阻塞后发新开始，
此前未执行的v5 b101ef82…只注册一次。machine_suspend6420.780198→6420.820863，
40.665ms；2452回调全返回0、5112/5112无覆盖，success1→2、SPIready/count1→2。
SSAM IRQ173计数1→2；kernel也有active timerfd，不能将早醒唯一归因于键盘盖。
18条原始12字节Report1输入字段全0，与18条KIP/iid1元数据数量相同；evdev0事件、
event14零激活。这不能反推v2未采载荷，亦不能把空报告本身说成v2 event14根因。
PMIC窗口前后均16，稍后物理按键18；用户确认“已恢复，触摸键盘正常，等待期间无操作”。
cleanup0、owner/token0、pm_test none、debug/times0、自己的trace挂载已撤除。

真实touch-resume-watch1825778f…异步检测resume_count、停止iptsd后等待1秒重建；trace
解冻102.023ms后已有新的原生尝试被单次门拒绝，后续iptsd事件仍在出现。它是重入假设，
不是已复现的第二轮故障。最多两次准入的独立源码456c9093…做了实际谓词ASan/UBSan
BASELINE/修改/回滚1/2/1和完整patch重建；没有构建或部署内核。构建第一次因work父目录
缺失exit1，尚未复制源码就停止。用户随后切换到无限制环境，此诊断构建路线不再续跑。

## 2026-11-04：无操作反馈闭环，原始采集窗口无PM请求，停止盲目计时转向环境对照
<!-- HUB135_OPERATOR_FEEDBACK_AND_RAW_WINDOW_20261104 -->

Hub v2现场反馈现已闭合：用户明确“已恢复”“等待期间完全没操作”。因此本次
键盘盖事件不是等待期间人为输入；仍没有该轮原始载荷，不能把len12直接断为空报告。
随后同boot/PID保持、SPIready/count1、PMIC6→8；之后关屏不等于再次真实睡眠。
历史SSAM诊断延时下的空报告记录保留，不以它替代本次stock SSAM的未采载荷。

采集方案按实际源码修正：HID debugfs仅512字节FIFO，冻结时可静默截断，故不使用。
改为只读hidraw与非独占evdev的有界ARM64静态采集器；不ioctl、不grab、不发硬件报告。
Surface HID实际LL open/close是空操作、无power回调；hidraw的64环只可排队63条，仍须
与SSAM trace逐条数量核对，数量不匹配不称完整。记录的是read时间，不冒充EC生成时间。
C源3be460b6…，静态ELF6a367396…；首次-Werror缩进告警exit1保留，补大括号后两架构
严格编译exit0，主机FIFO fixture12字节与3字节分帧exit0，预期不存在节点exit3。
实机5秒只读自检0条/exit0，owner0、success和SPIcount不变，不是睡眠试验成功。

用户另次确认135秒配合后，明确发开始，只执行新raw v3观察器734a95f7…一次。
同495331fe…，stock SSAM773f191c…/JAR126efcf2…/SPI3e9e6317…，Hub与电池条件保持。
本窗口注册TGID2342一次，120秒admission_timeout：原生attempts62→62，内核success1、
fail61均不变；trace357/357无覆盖，零suspend_enter/组件回调/machine_suspend/KIP HID。
两个读者均0条，SIGTERM排空后exit0，cleanup0，owner/token0，SPIready/count1。
前后PowerManagerService.WakeLocks均Active，累计持有时间增加120973ms，Google搜索、
tips、Play商店等合法后台锁出现，故没有请求实际进入内核。不能称休眠失败、采集到
空报告或问题消失。通用callback分析器对无回调trace报ValueError/exit1已原样保留；
改用无PM分支摘要exit0。用户明确本窗口“已恢复，触摸键盘正常，等待期间无操作”，
结合无PM证据只能验收关屏亮屏及输入，不冒充真实睡眠恢复。

后续只读v4监测在boot4974/4979/4984连续三次Asleep且框架两项blocker自然false，
中间捕获一次合法微信短锁；owner始终0，未注册/强停/禁锁。native失败退避仍可Active，
历史最大60001ms，不能将框架无blocker直接写成服务已无退避。监测退出0。

用户再确认后准备独立v5 b101ef82…，只在准入前增加自然blocker复核；尚未执行。
v5被动预检37次/约185秒均未就绪并exit1，未发新的开始，也没有注册；不是硬件失败。
实际前/中/后快照均Awake，display blocker保持，同时tips job已持锁数分钟。停止
重复计时路线，返回成功baseline与当前环境比较，不自动复用消耗过的v2/v3窗口。

成功无Hub、Hub v2/v3和当前四份有效快照的mScreenOffTimeoutSetting均300000，
sleep_timeout均-1；不能把当前5分钟设置当新回归或擅自重置。相同运行JAR/SSAM/SPI和
原菜单9c71d06d…再次匹配。当前默认网络103为Hub以太网eth0，Wi-Fi网络101仍validated；
Hub加入还改变了默认网络，不是纯USB负载对照。tips job189要求网络且正在执行，
但没有证据证明以太网、该job或键盘报告是原连续休眠黑屏唯一根因。完整网络/任务
原始记录仅在忽略out，不把地址、SSID和身份细节提交Git。

已询问用户仅拔Hub上的网线、保持Hub/键盘盖/其他USB与电池供电、不按电源；待反馈。
下一步只观察默认网络/相同USB枚举/后台锁，owner保持0，不请求睡眠、不禁网口驱动，
不改用户超时，不把网络对照结果提前当休眠修复。v5脚本保留未执行，不能因旧135秒
确认就稍后自行触发；恢复现场试验须重新说明当前条件与开始时刻。
原正常持续休眠黑屏仍未解决；本轮没有更改产品驱动、内核或默认/回退载荷。

## 2026-11-04：带Hub的135秒协调窗口已执行，键盘盖事件导致设备挂起提前中止

本记录取代下方旧记录中“Hub v2尚未执行/待135秒确认”的待续状态。用户已确认
“可以，135秒后按键”；显式发出开始后，只启动一次已核验52bf4b71…观察器。
同boot495331fe-0e50-4827-87c2-d05fa1fabbca、stock SSAM/JAR、原生服务TGID2342，
外接Hub/AX88179A/读卡器，电池供电；只注册一次，不强停应用或禁用合法wake lock。

本次实际准入不是成功睡眠：machine_suspend边界为零；4360/4360条trace无覆盖，
2154条已执行组件回调全部返回0，阶段为prepare1017/complete1017/suspend63/resume57，
尚未到late/noirq。内核原始错误为-16（EBUSY），last_failed_step=suspend，明确记录
Wakeup pending / active wakeup source:event14。success保持1，窗口fail27→28；后续
owner0后-95拒绝属于准入保护，不能混称更多硬件失败。SPIready、resume_count仍1，
不是新增恢复成功。当前仍同boot、Android PID3262/2818，没有SSH失联或已证实的硬冻结。

实际sysfs/InputReader映射event14为Microsoft Surface 045E:09AE Keyboard，PHYS
01:15:02:01:00，经SSAM/KIP连接，不是USB Hub事件节点。trace boot clock顺序：
2486.971256收到KIP/HID cid0/iid1/len12，2486.971296 event14激活，随后设备挂起中止；
恢复中另有同类报告。SSAM IRQ173在窗口前后均1，PMIC IRQ216均6；不称电源键结束睡眠。
这支持键盘盖报告触发本次提前中止，但不能把元数据推断成空报告、操作者输入、
Hub根因或正常持续休眠黑屏唯一根因。历史曾有无操作时全零12字节转换报告，不能
替代本轮未采集的原始载荷；fs/eventpoll.c的EPOLLWAKEUP可按设备文件名生成event14
锁，事件源名字不等于新硬件IRQ。evdev会丢空SYN_REPORT，不能凭len12就过滤报告。

观察器cleanup0，autosuspend_tgid/trial_pid均0，test/trial均N，SPIready，pm_test none，
debug/times恢复0，自己的trace mount已撤除；原菜单仍9c71d06d…，未改默认/回退载荷。
补充只读能力核对：实际键盘HID debugfs events/rdesc可用；只挂载读取描述符后卸载，
未启动报告读者或新的睡眠；CAPABILITY_CLEANUP_EXIT=0。未加入1.1秒延时、未屏蔽
键盘/SSAM唤醒、未写USB retention补丁、未重试准入。

一次只读收尾脚本误读不存在summary文件而exit1，错误源保留；修正为summary.log
及实际参数名后exit0，无副作用，不称硬件失败。收尾实读Asleep，最后PMIC计数6，
因此135秒后物理亮屏/持续触摸键盘结果与等待期间是否操作仍待用户反馈。不能因
SSH可连或SPIready替代用户验收。下一步先闭合该反馈，再决定是否增加有界HID原始
报告采集以区分真实操作与转换报告；不复用已消费v2观察器，不自动发起第二次睡眠。
原正常持续休眠黑屏尚未解决。本轮没有端口驱动/内核/产品策略代码修改。

## 2026-11-04：无外接Hub的stock原生单请求恢复通过；带Hub窗口尚未产生真实请求

同boot495331fe…，后台合法任务自然释放后，人工只注册一次实际UID1000服务2342，
SHA518d1319…。原生trace machine_suspend1147.216252→1176.731662，29.515410秒；
2256条回调全0，4623/4623无覆盖，内核success0→1，SPIcount0→1、ready，Android
PID3262/2818保持。用户明确反馈“完全正常”。结束内核睡眠为SSAM IRQ173，PMIC
计数在请求前后均2，之后物理亮屏时增加到4；不称PMIC直接结束内核睡眠。
Android mLastWakeTime是uptime，trace用boot clock，不能不对齐就直接相减推算延迟。

前后9→15/16等fail包含owner0/单次门关闭后的EOPNOTSUPP保护拒绝；不能当多次真实
硬件挂起失败。native total suspend time仍0，不代替实际trace区间，不作能耗证明。
观察器cleanup0、owner0、debug/times0、原菜单9c71d06d保持；未自动重试或软件亮屏。
这证明本次stock组合原生单请求恢复，不证明正常持续重入、长时间稳定性或所有外设。

用户随后只接回Hub、不接充电器，实读同boot/AC及USB供电均false，外接2109 USB2/3
Hub、0b95:1790 AX88179A、05e3:0749读卡器出现，SPIready/count1。下一次窗口在注册前
因新AudioMix、Play Store和微信回调锁而退出1，owner始终0，success/count未增加；
没有产生新的真实睡眠，不能记录为“带Hub睡眠失败”。原始power-before保留。

诊断助手后续改为记录合法blocker、保留原生服务自行等待，不强停/禁锁；已在独立
日志目录准备Hub v2观察器52bf4b71…，尚未执行/注册，private trace instance inactive。
窗口最多准入120秒、只注册一次，结束或到期撤销；这不是定时唤醒保证。
已请求用户确认“收到开始后135秒再短按”配合；尚未收到本轮确认，因此没有发开始。
用户确认后先重新核对fresh boot、owner0、SPIcount1、实际Hub/供电、helper哈希、trace
instance和已恢复原菜单，再执行此唯一待续窗口。不得复用已消费的无Hub/Hub-v1脚本。
USB retention候选仍未更改、构建或部署；原正常持续休眠黑屏仍未解决。

## 2026-11-04：stock手动真实睡眠恢复通过，原生单请求stock入口已启动但owner0

本轮手动trial boot a2c0729b…已执行且只执行一次：machine_suspend289.295305→318.851842，
29.556537秒；2256条组件回调全0，trace4682/4682无覆盖。同boot/PID3287/2828、SPIready/
count1、success1/fail0、owner/token0，debug/times恢复0。用户确认亮屏、持续触摸和键盘
都正常；实际结束内核睡眠为IRQ173/SSAM。物理PMIC计数随后0→2，Android最后亮屏
435928ms晚于内核醒来318851ms，仍是SSAM先醒、稍后按键亮屏，不是PMIC直接结束睡眠。
一次SSH reset255后同boot重新连接并取回完整trace，不将SSH断开直接称板卡冻结。

用户随后说当前未接充电器；此前接键盘盖/Hub/充电器的快照不能沿用。新原生入口实读
AC/USB/Wireless/Dock均false、电量76%，USB仅六个root hub，无外接Hub/网卡；因此与最初
带Hub的阶段检查并非完全相同物理环境，不能以单次成功排除Hub或直接归因为Android。

已复用旧Nat01 f1128b91…准备stock原生单请求对照。只在当前initrd999cbadd…的最后私有
身份member之前插入禁自动注册helper的248字节gzip增量，有效变化仅framework-suspend-start；
身份member仍最后且逐字不变。新natst45c932d3…、launcher3be38ea8…放独立路径，正常包
和Mesa未覆盖。initrd独立副本ROLLBACK已恢复原999cbadd…；实际源码谓词ASan/UBSan
BASELINE持续准入→MODIFIED单次/120秒到期/撤销→ROLLBACK持续准入均退出0，原源码未动。
实际GRUB新的三组菜单均验证首选候选、二次回正常，回滚选正常；不是硬件睡眠通过。

当前boot495331fe-0e50-4827-87c2-d05fa1fabbca，实际GNU build-id在Nat01 Image中唯一匹配。
完整Android、ALSA/AUDIO_READY/实际HAL774、三attached remoteproc及FUSE已核对，用户
确认桌面/触摸/键盘正常。最初声卡未就绪的检查退出1，随后自然就绪；未改音频或ADSP。
stockSSAM773、SPI3e9、JAR126保持，nativeY/testN/trialN、owner/token0，自动注册stub
和NO_LAB_WAKELOCK日志匹配；原菜单9c71d06d已恢复、once.env0。原生真实睡眠尚未许可。

下一步保留合法应用锁，先看后台任务是否允许原生睡眠，再开一次人工注册窗口；不靠
禁锁/强停增加计数，不把owner0保护拒绝或无请求称硬件失败。USB retention候选仍未改/
构建/部署。原正常持续休眠黑屏未解决，不能提升本诊断候选或称全部问题已修好。

## 2026-11-04：正常组合阶段/仅关屏通过，真实休眠故障继续定位

同 VHDX 工作区、同休眠分支继续；用户要求参考历史记录追踪并解决，随后补充黑屏发生于
空闲、无主动应用负载，并自行模拟关屏。已保存 SQ1 初始/EL2、SQ2 SPI/SSAM/原生单次与
正常策略的对照；不把单次诊断成功推广到正常持续策略。

当前 boot `d3bbe707-676b-4e06-9032-04fbd3cc37fd` 为本轮主动的一次受保护调试启动；
Image86d896ad、initrd999cbadd、SPI3e9e6317、stockSSAM773f191c、stockJAR126efcf2
均保持。testY/nativeN/trialN、owner0；原菜单9c71d06d已在检查前恢复，one-shot env0，
正常/Mesa/固件21项及SQ2Power六项完整性通过。用户确认到桌面。

同 stock 组合的一次 Asleep/platform 返回0，2454条组件回调全配对返回、错误0，
trace4996/4996无覆盖，包含late/noirq及恢复阶段，无machine_suspend。SPIcount0→1、
零报头进入既有Error Handler后ready，用户持续触摸/键盘确认正常；Android PID3620/3192
保持。初次采集因/sys/kernel/tracing被挂载层遮蔽，在PM请求前TRACEFS_REQUIRED退出1，
改用历史独立/tmp挂载后成功；原错误保留，不算一次硬件睡眠失败。

另发现USB xHC恢复USBSTS0x401/Reinit/root hub丢失状态。源码usb30_sec/prim/mp_gdsc
仍PWRSTS_OFF_ON；上游25bc96f26cd6的三域RET_ON修复已核对为相关候选，尚未修改、
构建或部署。PCIe现有ALWAYS_ON不撤销。该USB异常不能直接当作黑屏根因；为保持本轮
真实睡眠对照单变量，USB候选暂不混入。现场键盘盖、USB Hub、充电器都连接。

用户随后自行仅关屏/短按亮屏，确认“可以，都正常”。180秒被动trace362/362、无覆盖，
PM回调0、machine_suspend0；计数保持success1/fail0、SPIready/count1，同boot。
1次success来自前面的platform检查，不是真实睡眠。最终读回Awake、PMIC计数增加。
空PM trace被逐组件分析器拒绝退出1，已以明确零回调检查记录，不当设备故障。
这将重点收窄到真实系统休眠，尚未证明Android框架本身错误。

用户已同意下一轮收到“开始”提示后30秒短按一次电源键，检查持续触摸/键盘。
现只准备同一kernel/runtime的当前任务五秒单次trial启动器；持续native保持关闭，
不全局禁应用锁/SSAM，不改SPI或USB。为容纳既有EFI字符串，仅移除历史试验同样移除的
earlycon/printk.devkmsg控制台选项，差异单列。真实请求尚未执行，须新boot/IRQ/UI/
实际文件/guard及原菜单恢复核验后再发。不得把此手动trial称Android原生策略通过。

证据与计划延续原out/suspend-followup-20261104四角色，补充normal-trace/；
ROLLBACK.sh新增launcher/menu副本模式，旧单参数文档回滚仍保留。故障未解决。

## 2026-11-04：新 VHDX 工作区接续；正常休眠黑屏仍未解决
<!-- SUSPEND_FOLLOWUP_20261104: UNRESOLVED -->

按用户要求，从远端 `codex/suspend-fix-5.94-20261103` 的最新提交
`73dcdc862ac2948ed26f90a90f05f0f6df7e343e` 建立独立工作区：
`/mnt/spx-workspace/chromebookOS-suspend-20261104`，位于 D 盘
`D:\WSL\chromebookOS-workspace.vhdx`（WSL `/dev/nbd0`、ext4）。
原 Windows 工作区及其未提交内容不作切换、清理或合并。

**最新现场结果覆盖“休眠可用”的推断，不抹去历史有限通过：** 用户报告在
192.168.5.91 部署最新工作后，睡眠仍黑屏无响应，并确认“是重启过，无响应”。
当前读到的是用户重启后的 boot `b2c8dc03-18a9-4432-ad9f-baab1cbe928a`，
不能称为自主重启或自动恢复。用户未另行逐项说明故障时 SSH/键盘/触摸的状态。
前次正常组合只验证了启动、锁屏/解锁、输入和自动关屏；当时 native attempts0。
此前带诊断策略的单次睡眠成功不能移作当前 stock/持续准入组合的稳定性证明。

本轮严格固定既有主机密钥，只读核对 .91；实际 SQ2Power 的 Image `86d896ad…`、
initramfs `999cbadd…`、normal.efi `99a81102…` 与最新部署记录匹配，六项
SHA256SUMS 和 initramfs gzip 检查通过。生效默认菜单确实指向 SQ2Power，Mesa
回退仍在。运行 init `511cf1af…`、SPI `3e9e6317…`、stock SSAM `773f191c…`、
stock framework `126efcf2…` 匹配；本轮不是发现部署了旧的单请求诊断组合。

当前 owner2700，screen_onlyY/autosuspendY、testN/trialN、pm_test none；Y 是未注册
调用者的边界，不等于正常服务被禁止睡眠。恢复后 Android boot_completed1/Awake，
native attempts0、kernel success0/fail0，SPI ready/resume_count0/bus_error1(-22)。
本轮三次读取 boot 一致、system_server3884/SurfaceFlinger3443；这些只说明本次读回状态，
不能解释已丢失的上次故障。pstore 空，当前 dmesg 被本次启动的审计噪声覆盖，
未取得上次黑屏的冻结/设备回调/idle/恢复边界，根因仍未知。

声卡 Surface-Pro-X、AUDIO_READY、实际 HAL 774cd7e7…及三个 remoteproc attached
只读核对通过，不是新的听音或其他外设功能验收。本轮未改目标设备、未重启、未触发
睡眠、未改超时/锁屏、未禁应用锁、未禁 SSAM wake，也未热卸载任何驱动。

下一步先在当前正常组合下设计有界跨冻结取证，区分挂起前、idle 内或恢复阶段无响应；
每轮先记录假设、单变量、区分判据与停止条件，并核对 fresh boot、实际包及可用回退。
不原样反复睡眠，不把阶段测试返回0或一轮恢复当长期通过。连续两轮无新证据即回到
工作基线/环境比较。此轮仅完成工作区建立、部署核对及失败记录，没有新修复候选。

原始命令/输出/错误、工作区 checkpoint 与记录副本三态校验保存在忽略的
`out/suspend-followup-20261104/`。该目录四角色只验证本次交接文档的增补和回滚，
不是硬件睡眠 BASELINE/MODIFIED/ROLLBACK 实测；不提交凭据、私有输入或原始日志。

## 2026-11-04：正常策略已设为默认，锁屏/自动关屏现场通过

用户确认启动、触摸/键盘正常，随后确认锁屏、解锁、输入及约15秒自动熄屏均正常。
现机boot a77ea624…仍运行已验证的SQ2Power包；默认GRUB已切到/EFI/SQ2Power/normal.efi，
原Mesa回退/原默认载荷/共享固件均保留，除有意变更菜单外原26项完整性通过。
持久nativeowner2690，screen_onlyY/testN/trialN；此Y保留未注册进程边界，不阻止Android。
实验保活已退出，stock框架/SSAM恢复，已取消超长关屏及30分钟锁后延时覆盖，凭据未改。

仍须诚实区分：本正常stock组合受后台合法锁影响，采样nativeattempts0/success0；
单次原生实际睡眠通过来自此前实验组合。当前证明正常策略、启动、锁屏与输入正常，
尚未证明持续休眠稳定性、能耗或SQ1。没有为了增加成功计数禁锁/强停应用。
回退可选Mesa；恢复旧默认菜单可从EFI/SQ2Power/grub-before-normal.cfg校验后替回。
部署证据在out/power-normal-20261104。推送目标为休眠开发分支，不合入main；
任务前MIGRATION.md改动保留本地并备份，未混入休眠提交。

## 2026-11-04：用户要求正常化策略并推送，正常候选已启动，待现场锁屏验收

用户明确授权部署正常Android自动休眠/锁屏，恢复系统默认时间；覆盖此前不许持续native
的历史约定，保留未注册调用者保护/nohibernate和原Mesa回退。当前boot a77ea624…，
独立EFI/SQ2Power：Image86d896ad…、initrd999cbadd…、launcher99a81102…；persistent
owner2690，test/trialN，SPI3e9e6317…就绪。SSAM回原773f191c…无1.1秒诊断延时，
框架回stock126efcf2…无AC宽限补丁，lab保活退出。音频/remoteproc/FUSE核对通过。
实际内核QEMU正常保护与注册进程连续两次到达合成失败PM回调/撤销owner通过；未入真实idle。

已显式一次删除当前用户关屏/锁后/sleep_timeout及充电常亮覆盖、set-disabled false，
未更改凭据。框架实读关屏15000ms、sleep_timeout-1、充电常亮默认1；LockSettings服务
报告user10锁屏未禁用，WindowPolicy showing/inputRestricted/enabled均true。CLI get-disabled
仍返回true，与服务dump不同，故需真实UI验收，不能只凭CLI判定。当前无native尝试，
合法后台锁保留；尚未证明本正常stock组合实际睡眠或连续稳定性。

一次性启动已消费；菜单目前仍是消费后回原正常项的临时菜单，待用户锁屏/解锁/输入/
自动熄屏反馈再把同包设为默认。原默认载荷和Mesa未覆盖。源码b867659及后续记录待推送。
正常构建默认android策略，需匹配power-bundle；可用--suspend-policy screen-only。
MIGRATION.md为任务前改动，未混入休眠提交，备份diff在out/power-normal-20261104。

## 2026-11-04：native单次实际睡眠28.36秒，SSAM后按键亮屏及输入恢复通过

同boot e7638f55…第二轮只增唤醒源追踪，实际machine_suspend28.355558秒、IRQ173，
首个SSAM事件BAT/BST cid0x16，同时timerfd活跃，不能唯一归因。2338回调全0；SPI
count0→1/bus_error1→2后ready/IPTSd重初始化，内核success1，同Android PID。
单次许可消耗/owner0、原生后续请求被保护拒绝；debug恢复0、trace清理、原ESP27项保持。
用户已反馈恢复正常；后读Awake、PMIC2→4，最后按键时间晚于内核醒来，属于SSAM
先醒后按键亮屏，不是PMIC直接结束native睡眠。SPIready/count1/bus_error2、框架PID
保持，owner/token0、10份once.env0；未提升正常入口或开放连续休眠。
首次prepare中止未复现但未修复；不是正常连续autosuspend/功耗验收。详见休眠记录末节。

## 2026-11-04：首次native请求在prepare后中止，尚无原生实际睡眠

boot e7638f55…、电池供电/带键盘盖，Android一次获准请求完成952prepare/952complete，
全返回0；没有设备suspend回调或machine_suspend，SPIready/count0/bus_error1保持。
键盘盖与BAT1 wake计数都增加，尚缺直接唤醒源追踪，不能唯一归因。原生后续重试已由
单次门拒绝，owner0；用户按键后反馈无问题，同框架PID/Awake，ESP27项保持。
已准备第二份仅增wakeup_source追踪/临时PM debug的脚本，未执行；须现场协调再注册，
不把此次界面正常当原生睡眠通过。详见休眠记录末节。

## 2026-11-04：原生单请求候选已启动，现场输入正常，尚未开放休眠许可

当前boot e7638f55…，Nat01 f1128b91…/naton0001 e75f7dbe…；实际内核note/配置、
有效init/JAR和SPI/SSAM文件/loaded note匹配，Android启动完成，用户确认触摸/键盘正常。
JAR标签system_file、Enforcing；音频HAL实际hash774cd7e7…/ALSA/AUDIO_READY、
remoteproc attached与FUSE通过。nativeY/testN/trialN、owner/token0，attempts0/success0。
原菜单已恢复，ESP27项匹配、10份once.env=0，下次正常启动回原原有包。未开放许可。
下一步准备一次手动准入/120秒到期撤销的原生观察；保留合法应用锁，不做全局禁锁。
充电后台任务尚有锁，不把Awake阻塞当原生休眠失败。详见休眠记录末节。

## 2026-11-04：SPI恢复候选两轮真实恢复通过，开始准备受限native试验

当前bootb3221fcd…，SPI候选3e9e6317…/私有包2e4427e4…、SSAM1.1秒诊断模块保持。
第一轮SSAM唤醒后按键亮屏/持续输入正常；第二轮machine_suspend51.601193秒由
PMIC IRQ216结束，无SSAM唤醒，2336条回调全0，SPIcount1→2、错误计数2→3，
全零报头进入新Error Handler后恢复ready/IPTSd。用户确认持续触摸和键盘正常。
这是直接电源键唤醒及同boot第二次恢复通过，不是原生自动休眠/能耗完成。

原菜单/ESP27项保持，9份once.env=0，pm_test none/debug0/times0、nativeN、owner/token0、
SSAMenabled，自己的trace挂载已清理。下一次普通启动回原正常入口。
native单请求诊断候选已完成离线构建、实际谓词ASan/UBSan、QEMU实际Image准入验证
及391模块/18541外部引用核对；只允人工注册后的120秒窗口内一次请求，启动owner0。
独立Nat01 f1128b91…/naton0001 e75f7dbe…/launcher26493e0c…已部署并校验；未装
新菜单或once.env，未重启或开放许可。候选保留原SPI/SSAM模块，init仅取消自动注册。
下一步现场协调一次性启动，先验完整Android与owner0，再准备人工单请求观察。
保留应用锁，不使用全局禁锁，不提升SSAM固定延时，不热卸载。不自动触发新睡眠。
SSAM逐事件唤醒、原生策略、多轮/能耗/SQ1回归仍需完成，详见休眠记录末节。

## 2026-11-04：休眠目标已暂停；候选触摸恢复失败，已回原正常启动

优先读取休眠记录末节。带键盘盖、SSAMenabled、1.1秒诊断窗口的单任务试验虽由PMIC
在32.20秒后唤醒且回调返回0，用户随后报告触摸失灵。故障实读SPI not ready，恢复助手
超时；现有用户态重新初始化也退出3。候选不能标为完整恢复通过，也未提升默认。
已保存日志，原ESP27项/全部once.env=0核对后回原正常包。新boot c3898a5d…实读原#14、
原默认initrd、screen_onlyY且无trial参数、SPI ready/IPTSd运行；原ESP27项全部匹配，
用户确认已到桌面、触摸恢复。
用户已暂停研发，本次仅完成故障恢复，不继续休眠请求。不改锁屏或校准，不热卸载驱动。

## 2026-11-03：键盘盖空报告对照；保留 SSAM 时电源键恢复一次通过

同boot1df5aab6…，用户确认无操作的两轮platform均出现18条全零键盘报告及SSAM IRQ；
拔下键盘盖后SSAM事件/IRQ消失，2260条组件回调返回0。随后按用户协调，保持SSAM
唤醒enabled、盖拔下，真实machine_suspend13.964553秒由PMIC IRQ216结束，
success6→7/fail0、SPIcount7、同Android PID，用户确认亮屏/触摸/桌面正常。
这仍是单任务实验；带盖事件处理、原生自动休眠、重复恢复及能耗未完成。

**验收更正：用户指出直接进入桌面，未执行上滑。** 当前user10 get-disabled=true，
11月1日已有同状态记录。此前本SQ2“上滑解锁通过”的表述不能作为证据，收窄为亮屏/
触摸/返回桌面正常；未改凭据或锁屏策略。设备仍受保护nativeN/owner0/token0、
SSAMenabled；正常载荷未提升，临时追踪已清理。详见休眠记录末两节。

## 2026-11-03：SSAM 提前唤醒继续定位到本轮键盘盖事件

目标“休眠可用”继续进行。单次PMIC唤醒现场正常已确认，但原生自动休眠仍未验证。
同boot1df5aab6…新增一次受保护Asleep/platform：2340条回调均返回0，4760条trace无覆盖，
success3/fail0、SPIcount3。SSAM IRQ后释放KIP/HID键盘接口事件，本轮无BAT事件；
不能套用旧SQ1的DPTF结论，也不能凭元数据断言键盘空报告。后续5秒被动采样无报告；
现场输入状态和本轮UI反馈待确认。正常入口/载荷未改、SSAM仍enabled、nativeN/owner0/token0，
Awake、同Android PID，临时追踪已清理。可选SSAM tracepoints接入原受保护助手，
不放宽trial=N条件。证据与下一步见[SURFACE-SUSPEND-20261103.md](SURFACE-SUSPEND-20261103.md)末节。

## 2026-11-03：逐组件响应核对定位 SPI-HID PM 构建回归，隔离候选阶段验证通过

按用户提示复查旧设备前缀/QCA诊断，并解析本轮PM callback trace。
原platform2334条回调全部返回，但SPI-HID仅prepare/complete；运行build-id、
实际module31ee23c6…、有效cpio与符号/sysfs交叉确认旧PM patch未进入EL2模块。
EL2 builder现补回已有patch-sq2-spi-pm.py调用，不重写驱动、不热卸载。
独立模块严格编译/67导出符号审计、源码与二进制副本回滚通过。

候选module072cdec5…只替换实验initramfs的一项。boot4c9d039f…仅debug opt-in；
Awake devices/Asleep platform都返回0/fail0，SPI挂起/恢复分别0.191/24.452与
0.114/22.150ms，resume_count0→1→2；后者2336条记录全部返回、无覆盖。
用户确认屏幕/触摸/解锁正常，音频/存储已复核，原菜单已恢复且env0。
随后boot1df5aab6…真实请求先由SSAM IRQ173在34.891ms提前结束；用户UI正常。
另获现场确认后，同boot仅单轮临时排除SSAM wake，machine_suspend15.688187s，
内核明确PMIC IRQ216唤醒；2338条组件回调全返回0，同boot及Android PID，token0、
SSAM恢复enabled，用户确认画面/触摸/解锁正常。这是一次受控单任务结果，
不是低功耗能耗、多轮稳定性或原生自动休眠完成，也不是所有历史黑屏唯一根因证明。
当前受保护实验内核nativeN/owner0/token0，无循环；下次启动原正常。
原ESP27项、音频实际hash/ALSA/AUDIO_READY/FUSE保持，临时追踪已清理。
正常默认/回退与私有runtime输入未提升。逐组件CSV/源码四角色和原始证据仍仅out。
详见 [休眠记录末节](SURFACE-SUSPEND-20261103.md)。

## 2026-11-03：休眠研发重新接续，SQ2 三阶段调试通过，单任务真实请求仍黑屏

已按用户要求将 main 更新到7b189bd并合入 `codex/suspend-fix-5.94-20261103`
（合并7df7710）；原 MIGRATION.md 未提交内容保留。详见
[SQ1/SQ2 休眠记录末节](SURFACE-SUSPEND-20261103.md)。
SQ2 独立 debug-only 启动后 freezer/devices/platform 各返回0，success0→3、fail0，
trace 含 late/noirq 返回、无 machine_suspend；用户确认屏幕/触摸/解锁正常。
不能称真实睡眠或低功耗通过。正常两项菜单与原载荷不变。

单任务 opt-in boot5bbbc8a3…，screen_only/test/trial=Y，autosuspend=N；
未持 token 请求实机拒绝，原菜单已恢复、once env0。用户随后协调15秒按键，
单次真实请求后SSH超时，用户确认仍黑屏、短按无反应；随后长按恢复，实读boot baa5a989…回原#14。
没有强制禁锁或重试。阶段调试Awake与真实请求Asleep存在runtime差异，
不能排除所有设备回调或直接归因CPU idle；下一步先同状态对照/跨冻结取证。
未经 fresh boot/IRQ/UI/guard 检查不复用脚本，不重复此前强制禁锁黑屏试验。
原 ESP 旧日志与本轮原始证据仅在 `out/suspend-resume-20261103/`。
已实读原菜单/两载荷/共享GRUB/固件27项hash全部恢复匹配，guardY；
ALSA/AUDIO_READY、实际HAL774cd7e7…、两用户FUSE及attached ADSP保持。
real-1只有请求前标记，无返回/晚期trace，pstore空；不声称准确卡点。
新增可复用 debug-only 启动器转换和阶段采集脚本，默认构建未接入；
清理失败不能报告成功。5+5+4项离线测试通过，实机范围与限制如上。

## 2026-11-03：开发与交付禁止单机绑定（当前最高优先级）

按用户要求，同一标准镜像必须面向任意 SQ1/SQ2，不要求按单机重编译或替换专属启动包。
根 AGENTS.md 已加入 `SPX-PORTABLE-SQ1-SQ2`，完整规则见 [通用开发规范](DEVELOPMENT.md)。
禁止以序列/MAC/IP/SSH 身份、固定原盘 UUID/ESP 标记、用户编号或一次枚举值准入；
机型能力选择与动态资源发现应通用化，目标自己的凭据/校准/NV 与镜像分离。
磁盘防误写、真实硬件/ABI/产物完整性、screen_only/nohibernate 和固件防重试保护不撤销。
这是规范更新，不是已修复所有遗留绑定或完成所有 SQ1/SQ2 实测；旧私有实验镜像不自动
成为通用交付，整改与跨设备矩阵见 TODO 顶部。本轮只更新文档和规范检查，不写设备。

## 2026-11-03：Moonlight V+ 恢复，移除固定 VIDEOCC phandle 启动门槛

实际 LTE DT 的 VIDEOCC overlay 被分配为 `0xb9`，旧助手只接受 `0xb5`，流程在
加载 HFI/core 之前退出；Codec2 服务仍运行但没有 `/dev/video0`。现直接删除助手的
固定数值检查及替代格式门槛，不改成新的机型/数值白名单。HFI overlay 按现有 provider
节点解析实时引用，只重绑定 clocks/resets/power-domains 的八个引用单元；保留 provider
身份、字段宽度、SMMU/ICC 引用、模块完整性、screen_only/nohibernate 和不可重复启动固件保护。
EL2 no-TZ 标记移到首次可能启动固件的 core 加载之前；不重置已有 VIDEOCC/provider。

同 boot 恢复 `/dev/video0`，真实 Moonlight V+ 12.14.4 选中 `c2.sq2.hevc.experimental`，
2880×1920 HEVC 已实际输出并显示，用户明确确认“已恢复”。60fps 是请求配置，不等于
长期稳定 60fps 认证；日志仍有网络丢帧/输入等待。未重启，框架进程保持；没有修改
Android 分区/全局图形、音频、DT 或固件，也未写 ESP。本次当前设备改动仅 RAM，
现有 ESP 镜像未更新，重启仍回原包，不能称重启验收。

本工作区默认 runtime 输入已用独立副本更新，仅变更 helper、HFI overlay 和对应 SHA 清单，
原共享输入不动。两项菜单/其他 payload 内容和最后凭据成员保持；已有整盘 IMG/GZ
未重新生成。公开重建：先用 `build-sq2-hfi-overlay.py` 构建动态引用模块，再通过
`pack-sq2-video-reference-fix.py BASE MODULE FRESH_OUTPUT --expected-base-sha256 HASH --module-sha256 HASH`
更新操作者自己的 runtime 输入。二进制、私有输入与原始日志不提交。

## 2026-11-03：默认关闭 4 KB 页模式开发提醒

标准入口仍为 `scripts/build.py`。共享 defaults.integrate 将当前 `apply-device-info`
加入 SQ1/SQ2 默认与 Mesa 四个载荷；助手在 Android init 前的 RAM 属性覆盖中移除
各分区重复的 `ro.product.build.16k_page.enabled`，仅在 product 属性中写入 `false`。
仅关闭 Settings 页无关模式常驻提醒及本移植未提供的 16 KB boot OTA 切换入口；
不改变内核页大小、应用兼容模式、其他通知或 userdata。保留 300 DPI、用户缩放、
KSU、swap、LTE、EL2、screen_only/nohibernate 与最后凭据成员。
AOSP 判断依据：[Enable16kUtils](https://android.googlesource.com/platform/packages/apps/Settings/+/refs/heads/main/src/com/android/settings/development/Enable16kUtils.java) 与
[Enable16KBootReceiver](https://android.googlesource.com/platform/packages/apps/Settings/+/refs/heads/main/src/com/android/settings/development/Enable16KBootReceiver.java)。

离线整镜像构建及四个最终 ESP 载荷检查通过；18 项提醒/defaults/swap 专项通过。
同输入的实际 shell/awk 夹具中 BASELINE/MODIFIED/ROLLBACK 为 true/false/true，
均保留默认密度与 userdata；只替换 bind mount，不将主机夹具当作实机通知消失证据。
旧 LTE 夹具的 5 项失败在干净 main 副本中同样复现，本轮未改动 LTE 子系统。
没有设备部署、重启或实机提醒验收。私有输入、镜像和原始验证记录不提交 Git。

## 2026-11-03：默认 300 DPI，移除 KSU 用户态固定 SHA 校验

按用户要求，标准入口、profile 后端、defaults.integrate 及密度助手统一默认
300 DPI；`--dpi` 覆盖和 userdata 中已有缩放保持。KSU 的 ksud/APK 打包与
首启不再按固定 SHA 拒绝；保留文件存在/非空、内核接口兼容检查及已有安装不降级。
旧组件构建器也移除 ksud/APK 固定 SHA，但原 Image/rescue 内核保护不改。
修正对应旧测试，不以伪造文件的单元测试代替实际 KSU root 验收。
保留检测内存 <12GiB 时创建/启用 6GiB swap；本轮没有设备重启或新启动包部署。
完整性审计、生成输出 SHA、磁盘身份和其他子系统校验仍保持其现有状态。

## 2026-11-03：检测内存小于 12GiB 时自动创建/启用 6GiB swap

WSL 当前工作区 `/mnt/spx-workspace/chromebookOS`。标准构建的 defaults.integrate
无论 KSU 开关都加入 `surface-swap-boot`，两机默认/原 Mesa 回退载荷均保留。
助手异步等待 Android 完成，进入 PID1 的 mount namespace/root；不使用过期的
`/android/data` bind。只处理 `sq2.native=ssd`；按用户更正，检测到 `0 < MemTotal < 12582912kB`
（12GiB）时创建，等于或大于阈值跳过。检测值扣除了固件预留，不等同于
标称 DIMM/SKU 容量。实机 7592612kB 仍符合；`sq2.swap=0` / rescue 跳过。

校验已选 userdata 的 major:minor 与 `/data` 的 rw ext4 挂载一致，在 root:0700
`/data/.surface-swap/` 内首次完整写入 6442450944 字节、0600 swapfile，mkswap 后
原子改名并 swapon。要求 6GiB + 1GiB 余量；失败不阻塞 Android。已存在文件
不重新格式化；已启用不重复 swapon，后续启动只重新启用。保留其他 swap、原
swappiness、userdata/分区、凭据、screen_only/nohibernate 和所有原启动组件。

192.168.5.94（用户确认地址及主机指纹）本次同一 boot 已实测启用：SwapTotal
6291452kB，文件 6442450944 字节/0600；重复执行 ALREADY_ACTIVE，无重复分配。
只把助手放入设备 RAM 执行，未改设备 ESP/initramfs，未重启；故设备重启自动
启用还需部署含本次源码的新载荷，不能把当次启用称为重启验收。
离线 shell 故障/边界与有效 cpio 检查及独立副本回滚记录：`out/auto-swap-20261103/`。

## 2026-11-03：按用户要求解除 SQ1/SQ2 LTE 单机绑定

LTE guard/seed 接受 SQ1 和 SQ2，移除 NVMe 序列号与固定 ESP 标记准入。两机默认项使用 IPA DT，保留各自 OPP 与 Mesa 原回退。EL2、screen_only、nohibernate、禁用开关、RAM-only FSG 保持。不写设备；共享原 SQ2 FSG 未经新 SQ1 实机验收，APN 不猜测。

## 2026-11-03：源码 LTE 与压缩默认交付

最新默认入口从源码编译 LTE/Radio，不再要求预制 Radio04。私有输入统一放 `private/build-inputs/`，不按历史输入 SHA 白名单拒绝；默认输出 IMG.GZ 并清理本次镜像中间文件。位置、公开工具依赖、单位配置与限制见 [PRIVATE-BUILD.md](PRIVATE-BUILD.md)。下方固定 Radio04 章节仅为历史。

## 2026-11-03：320 DPI + KernelSU Next + 通用触摸成为标准构建默认

标准整镜像入口仍为 `scripts/build.py`。默认 `--dpi 320`、`--ksu-manager on`、
`--touch-repair on`；保留各自参数和包目录覆盖，不把默认值变成机型能力门槛。
统一后端将默认密度助手与 KSU 首启安装器加入实际四个启动载荷，不仅改源码。
原冻结 957b 包没有 KSU 用户态助手；已补齐，原 c1ef 内核不变，其内嵌配置
确认 `CONFIG_KSU=y`、`CONFIG_KSU_KPROBES_HOOK=y`。
KSU 官方 3.2.0 的 APK/ksud 按历史验收 SHA 固定，异步等待 Android 完成后安装，
不降级已有包/ksud；为现有用户启用管理器，不创建用户、不授予应用 root。
密度只改 `ro.sf.lcd_density`，保留 userdata 缩放。通用触摸流程与第二层一起纳入。
保持匹配驱动、音频、GPU/DT、screen_only、EL2、LTE默认和最后凭据层。
本次仅离线回归与组包；因缺私有 Radio04，四载荷检查显式 `--lte off`，
并未关闭发布默认 LTE。没有新整盘 IMG/GZ、设备部署或实机验收。
输入获取、SHA、自定义及边界见 [标准构建](STANDARD-BUILD.md)。

## 2026-11-03：通用触摸修复成为默认基础构建，第二层接入校准向导

标准入口仍只有 `scripts/build.py`。默认 `--touch-repair on`，从
`out/touch-baseline` 读取当前源码匹配的统一触摸包；`--touch-bundle DIR` 可覆盖，
`--touch-repair off` 显式保留旧基础运行包。缺包、缺清单条目、过期源码或损坏载荷
均拒绝，不能静默省略触摸修复。构建包的方法见 STANDARD-BUILD。

状态一致性路径改为 `m_info.is_touchscreen()`；`process_touchscreen` 和原始核心
连续性算法不按 SQ1/SQ2、VID/PID 分支。硬件枚举仍限驱动支持的设备，旧 SQ2
四电极热图 quirk 保持其原身份范围；不能把特定硬件观测强加给其他屏幕。
静置图、坐标和新的 `.tap` 参数仍绑定各设备身份，不预装 .93 的测量结果。

标准顺序：静置检测/用户保存与复测 → 15 点单指采集 → 用户确认每点只点一次
→ 原生自动分析/生成临时修复 → 相同 15 点复测 → 手势确认/保存或恢复原设置
→ 坐标校准 → 拇指采集/修复/复测 → 保存。保持完整点击后等 1 秒再换点；
坐标采样不加屏蔽区/目标距离门限，保留音量减跳过和音量加返回。
屏幕只给操作提示，诊断计数/原理保留在数据记录与文档中。

详见 [通用触摸流程与验证边界](TOUCH-NOISE-AND-CALIBRATION.md)。新组件已离线编译/组包，尚未部署或实机验收。



## 2026-11-03：最新 LTE 分支合并到统一主线基线

标准整镜像入口仍为 `scripts/build.py`；默认 `--lte on`，保留显式 off 和自定义机型设置。
仅在默认项叠加经过 Radio04 完整哈希/有效条目审计的 LTE 增量，保留非 LTE Mesa 回退。
私有输入、单机 FSG 绑定、SQ1 未验证边界及构建命令见 [LTE-BASELINE](LTE-BASELINE.md)。
当前合并只做离线回归与副本回滚，未写设备、重启或重构建既有交付 IMG/GZ。
## 2026-11-02：融合触摸校准静置检测v2（.93，RAM验收）

当前SQ1/0996的应用已更新：打开5秒后检测，分别报告条带/大片/短暂异常/未见明显异常/数据不足。
修复宽干扰被第一帧丢弃及无HID报告时计时挂起；检测与安全学习分离，不自动屏蔽整行。
同一充电日志旧版contaminated1帧→新版line_noise1201帧，零学习电极；实际UI已显示行3条带。
仍保留坐标跳过和音量减键、SQ2已验收修复、原音频等。只更新RAM和APK，未写ESP或重启；
不是幽灵触摸已修复/声学/全外设新验收，现有交付IMG未改。详见[融合触摸](TOUCH-NOISE-AND-CALIBRATION.md)。
项目标准构建入口：`scripts/build.py`，生成单个 SQ1/SQ2 通用镜像；默认设置可自定义。见 [标准构建](STANDARD-BUILD.md)。

# SQ2 开发交接

## 2026-11-02：SQ1/SQ2 共用构建与机型配置（仅离线）

最新 main fee67b2 上新增统一入口 build-surface-pro-x.py；build-source-boot.py
默认转入它，--machine sq1|sq2|both 复用同一已验收957b运行包及c1ef内核。
原主线组件重组用 --rebuild-components，legacy EL1入口保持。
SQ2固定512GB布局/700MHz不变；SQ1用既有20GiB Android镜像模板布局、661MHz，
不是其Windows ESP。SQ1只取自己已有的存储增长组件/凭据，不混用旧内核/模块；
正常EL2、60Hz、音频/视频/USB/FUSE/触控/温控/终端准备保持，两个图形入口。
本轮仅Windows只读采集、离线组包和文件校验；没有硬件/VM/功能测试或设备部署。
SQ1功能属于构建能力一致，不能写成实机通过。固件版本、DT界限及命令见
[UNIFIED-SQ1-SQ2](UNIFIED-SQ1-SQ2.md)。以下SQ2专用构建段落为旧入口历史。


**2026-11-02 本地同步主线73ea6ee，USB修复接入默认双启动构建**：用户要求拉取
最新主线、把本轮改动纳入默认构建并推送。先本地stash保全所有工作区改动，
从8f64975快进到73ea6ee，再恢复工作区；BUILD/HANDOFF顶部冲突按“本轮USB记录+
主线音频/默认构建记录”合并，保留双方内容。已有GPU实验、REA和Windows音频
采集的无关本地文件不混入USB提交，安全stash保留。

真正默认入口已是build-sq2-default.py，不是legacy EL1组包分支。新增默认on的
USB bundle集成，固定原实测Image/46模块/索引/别名和父包语义；共享运行包先
完成主线normal_payload，再加入USB，之后写两个EFI目录。保留音频92852a7b…/
b165ca2c…组合、HAL774cd7e7…、no-TZ视频、终端不弹出/不自动KVM测试及原凭据。
原Image/DTB/菜单未改，本轮没有部署或重启设备。

30项USB测试通过，真实默认载荷集成输出957b5ee6…与前次设备重启验收字节一致；
BASELINE无usbnet/输入watcher，MODIFIED两者都有，独立ROLLBACK副本恢复320e5076…，
三个状态音频HAL字节一致。标准完整EFI构建已尝试，但本机缺out/default-input
及新主线依赖缓存，在首个qebspilaa64.efi输入处停止；不假造缓存或称完整EFI
重编译通过。具体命令、测试及Git同步证据继续追加原out/usb-network-input-20261102
的四角色记录；模块/私有输入不进Git。下文前次实机验收保持有效。

**2026-11-02 USB Hub 网卡与鼠标修复，持久安装和重启验证通过**：
用户指定 root@192.168.5.91，并要求修复、扩充 Linux 网卡驱动和接通 Android
输入。实际当前设备已是 EL2 默认 Qualcomm 入口，不能沿用下文旧 Mesa 菜单状态。
当前默认 Image c1ef559f…、DTB 85b436a3…、launcher dc7b5ac9…；本轮均未修改。

网卡 0b95:1790 AX88179A 枚举在 USB 配置2、接口类02/0D（CDC NCM），不是
强行切配置或 new_id：加载匹配的 usbnet/cdc_ether/cdc_ncm 后生成 eth0。
用户接好网线后 Android 自己完成 DHCP（30.32.177.131/21、网关30.32.176.1），
Ethernet CONNECTED/VALIDATED 且成为默认 network104；绑定 eth0 的网关 ping
2/2、0丢包。未手动配置地址/路由、未运行诊断 DHCP、未改 Wi-Fi 保存状态。
之前对旧 Wi-Fi 网关192.168.5.1及IPv6路由器的 ping 失败不算驱动失败；保留
失败日志，不把当时尚未完成的 DHCP 说成已完成。

USB 有线/WWAN 协议集增加46个驱动及依赖模块、811条USB别名，含 ASIX、
AX88179、RTL8150、LAN78xx、SMSC、AQC111、CDC ECM/NCM/EEM/MBIM、RNDIS、
QMI、ipheth及多个旧款 USB 以太网。现机 r8152 和 mii 原本内置，未覆盖它们；
并非“新增46种实测网卡”，也未扩展 USB 无线网卡的固件/Android Wi-Fi HAL。
以运行 config 编译新增模块，不替换 Image 或已存在模块。原完整 symvers 不在
本机构建缓存，modpost有明确警告；独立检查所有新增未定义符号对现机导出+
新增模块导出、vermagic及depmod无环后，实机46模块逐个加载全部成功。
原版 mdiobus board-info 与 libphy 合并到私有构建副本，原版 net selftests
另作GPL支持模块，不使用空函数代替缺符号。原始源码目录不改。

罗技046d:c539的 usbhid 和 event23 原本正常，但 EventHub 遇到 devtmpfs 节点
权限就绪竞态而未接入。新增独立 usb-input-watch，仅选 bustype0003且USB来源
的evdev节点，先准备0660/root:input与input_device标签，再用IN_CREATE发布；
按完整sysfs身份缓存，eventN复用会重新发布。不grab、不注入、不改盖键盘、
触屏或蓝牙。发布后 Android 显示 CURSOR|EXTERNAL、Enabled true和MOUSE映射，
用户明确确认“移动、按键、滚轮均正常”。已在线启动守护。usb-network-watch
只按审计USB别名按依赖加载，不负责DHCP/路由；两个helper均保留rescue/usb=0
退出条件。网络模块需先构建/审计，现已按上节默认纳入EL2共享运行包。

25项USB相关测试通过（含真实shell隔离fixture、身份复用/权限标签失败/禁用/
模块依赖顺序/未知别名/打包防篡改）。回滚在独立helper副本及启动包副本执行，
恢复哈希与原件相同；未为回滚测试撤销已修好的现机输入。四角色产物和原始
命令/输出/错误在忽略 out/usb-network-input-20261102/，敏感原包不得提交。

安装前再次核对 BC711 512GB、序列CYA5N050012105J77、ESP B027-0512、
data 4f46d2ec…、ESP标记和root loop。只原子替换
/media/esp/EFI/SQ2Default/initramfs.cpio.gz，从320e5076…变957b5ee6…；
有效条目审计仅增加USB模块/索引/helper并更改/init，原凭据member逐字保持最后。
gzip及目录内全部SHA256SUMS通过，原Image/DTB/EL2固件和Mesa fallback未改。
备份为同目录initramfs.before-usb-network.cpio.gz及SHA256SUMS.before-usb-network。
旧boot836903c9…下screen_only=Y、nohibernate保持。用Android sys.powerctl
重启后，SSH曾超时，后来自行恢复到原Wi-Fi地址。新boot
5aa55ff9-f443-42af-979f-82793b23815e，Android complete1、guardY、nohibernate；
实际helper/hash和ESP全部SHA256SUMS通过。网卡自动加载cdc_ncm，Android重新
DHCP到同地址、Ethernet network101 CONNECTED/VALIDATED，绑定eth0网关ping
3/3零丢包。输入watcher自动发布event16–19，鼠标event17在EventHub为
CURSOR|EXTERNAL/Enabled true，InputReader具有MOUSE映射。原盖键盘helper
hash仍b6e4d248…，音频HAL774cd7e7…、蓝牙桥65bef26e…；video0/kvm存在。
这些节点/哈希不是本轮录音、硬解帧或KVM guest的新运行验收。

用户随后反馈重启后无音频：只读检查确认声卡0仍是Surface-Pro-X、音频helper
已报AUDIO_READY、HAL运行、左右DAC/COMP/BOOST均On，PCM在RUNNING且有帧。
内核历史出现SoundWire FIFO underflow/transport -5，不能据HAL/PCM成功称听音
正常。尚未做任何音频路由/增益/服务/模块改动时，用户明确反馈“声音已恢复”。
因此停止音频操作，记为短暂无声后用户确认恢复，根因未定位，不归因于USB补丁
也不声称已修复SoundWire问题。输入/网卡目标完成，保留现状，不再自行重启。

## 2026-11-03：原生 Radio、状态栏与 SIM 设置已接通并成为默认

最新实现和故障对照见 [LTE-RADIO.md](LTE-RADIO.md)。实际 UIM/NAS/DMS → Radio AIDL V3
→ 电话框架/订阅/APN → netd/SystemUI/Settings 已接通。状态栏显示真实信号与LTE；设置
显示中国广电SIM、移动数据、流量统计、LTE类型和APN入口。数据关闭撤下网络/地址，
开启恢复；完整飞行模式DMS1→0恢复通过；关闭Wi-Fi后Chrome HTTPS通过且LTE成为默认，
恢复Wi-Fi后优先级回切。旧Java NetworkAgent在Radio模式不启动，只有HAL管理bearer。

修正了NAS非法UTF-8名称导致属性异常、同build ID不刷新APN、无PDU session错误使用-1
导致Java拒收成功响应三处问题。APN使用标准checksum合并，保留用户编辑/删除。临时
callback日志已移除，正常启动不执行测试探针、DIAG、Terminal UI或KVM witness。

曾有一次rad04启动真实NAS拒绝；保留的LTE03回退也复现相同NAS3/PS2与WDS无服务。
只读确认自动选网、非本运营商小区forbidden、无有效拒绝原因码。用户确认未改位置/
SIM/天线，一次正常20秒飞行模式策略恢复后NAS1/PS1与HTTPS200恢复。未改NV/APN/射频，
不宣称已确定或修复该偶发注册问题；完整证据在LTE-RADIO，不能抹去这次失败。

同一份未改动rad04随后独立启动boot75246691，约61秒建立原生LTE，实际HAL58aa52c2、
SIM LOADED、HTTPS200、原音频/EL2保护通过。提升为默认后，又从普通默认入口启动
boot1e6156a5，原生LTE自动连接、HTTPS200、Wi-Fi优先、HAL实际inode/hash及ALSA声卡/
AUDIO_READY/音频HAL774、三remoteproc attached、screen_onlyY/nohibernate再次通过。

当前载荷：initrad04 `368d4f213d3f5bd6c8d6be159443c569cc82c846da2a88d111b5f416edcc4d05`；
Radio HAL `58aa52c2b602a6be172c71a93315d8adf7826cb7a434f5a5f2dba55dc7156fcc`；
EFI `a50e65c904d73d87d86dd43ef6fe25298fe87eb1f32ded33c3efe3d39d0ab93a`；
默认菜单 `adc0afab1f569580388cee950ebd448eab08e177fc2a66ca42a51b381f1d13e2`，直接
chainload `/EFI/SQ2LTE/rad04-once.efi`，不再读取once env。菜单仍恰好Qualcomm默认/Mesa
两项；LTE03载荷、原默认957/85b/DC7、共享固件和Mesa原清单均校验保留。

仅单USIM/LTE IPv4数据；语音、SMS、IMS、eSIM、5G、PIN管理未实现，IPv6/漫游/长期
移动切站和耗电未验证。captive_portal_mode=0原策略未改，VALIDATED不代替真实HTTPS证据。
专项测试通过；全库旧脚本参数读取与触摸测试仍有失败，详见LTE-RADIO。私有镜像/FSG/
APN/身份/密钥和原始日志截图保持在ignored out/private，公开提交只含源码、配置及文档。

## 2026-11-03：LTE 数据已可用，完整启动与默认部署

以下 11-02 的 FTM、Malformed 和 DIAG 失败记录是历史状态。最新实现与证据见
[LTE-DATA.md](LTE-DATA.md)。原机 FSG 被接受，FTM 自然为 0，DMS online、NAS LTE
注册/PS attached、QMAPv4/RMNET 真实数据通过；没有清 FTM、移植 NV 或重置 adopted modem。
Android 发布 metered CELLULAR 网络并由系统按既有策略标记VALIDATED，Wi-Fi 关闭后 Chrome 正常加载 HTTPS，
恢复 Wi-Fi 后默认网络回切。数据开关、飞行模式设置的低功耗1/online0回读与重连均通过。

首次独立候选 init636167fd…已完成整机重启，boot535134dc：FSG精确哈希、IPA/RMNET、
Android CELLULAR VALIDATED、LTE HTTPS200、user10、三remoteproc attached、物理声卡、
AUDIO_READY及实际HAL774cd7e7均通过。原默认init957/DT85b/launcher与Mesa仍保留。
发现多个 zygote PID 的启动环境选择问题，已限定 PPid=1 主 zygote并通过双顺序夹具；
同时让 native helper 忽略 SIGPIPE，防止 Java 关闭 stdout 后打断其 QMI 退出清理。
最终候选 init783cdacf…在boot6b05f435完成独立启动：有效helper6ff6606a、QMI数据abc580c0、
jar d49a219b精确匹配，Android LTE VALIDATED、HTTPS200、数据关/开撤下与恢复通过。
原声卡/实际HAL774与三个attached remoteproc再次通过。已将同一载荷提升为默认，菜单
SHA883cd0a5…恰好“Qualcomm / LTE / 60Hz / GPU700”和原Mesa两项，直接指向已验证EFI，
不再依赖once env；旧默认957/85b/DC7、原Image、共享固件及Mesa有效清单全部校验保留。
提升后已从普通默认入口再次整机重启至boot fc66b6e8：cmdline实际initlte03、菜单883cd0a5、
有效FSG/运行native与jar哈希全部一致，LTE自动连网、HTTPS200、Wi-Fi优先及全部上述音频/
EL2/屏幕保护检查通过。当前默认已可用；不是只把未经启动的镜像写到ESP。
该默认启动后再关闭Wi-Fi，原Chrome成功加载带新时间戳的HTTPS页面、蜂窝计数增长，
随后Wi-Fi恢复为默认；结束时wifi_on=1、airplane_mode_on=0、mobile_data=1。

正常 EL2、screen_only=Y、nohibernate、原密码SSH addon最后及无Terminal/KVM自启保持。
当前仅单卡私有APN IPv4数据，电话/SMS/原生SIM UI、IPv6、漫游策略和长期移动测试未完成。
原系统captive_portal_mode=0保留，VALIDATED不代表Google探测通过；实际联网由HTTPS/Chrome独立证实。
私有FSG、原始日志、截图及带登录资料镜像留在out/private，不入Git。

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

**2026-11-02 LTE 原版参考分析恢复，未做 SQ2 硬件变更**：已切到
codex/lte-support；原工作分支 fee67b2 与未提交改动保存在本地 stash/忽略快照，
没有把旧 LTE 默认构建部署到现机。严格核验既有 SSH 主机后，只读采集 SQ1
Windows 22631 的 21 个相关包、8 张 ACPI，两批共115个 manifest 文件哈希全过。
SQ1/SQ2 保存的两个 MPSS 固件均同字节；IPA/GSI DSDT资源经限定 WMI 及实际
注册表参数核对。qcremotefs 原目录存在 bootmodem_fs1/fs2，各2MiB；哈希被共享
锁拒绝后改为目录元数据，没有绕过锁或取得/移植单机NV。REA分析本地驱动副本，
未将启发式xref当真实调用证明。mica vendor缺rild/netmgrd/ipacm等关键可执行件，
仅有库/声明不能称LTE可用。25项采集选择测试、原QMI/online模拟测试及四角色
字节级回滚通过；原video collector保持。详见LTE-WINDOWS-REFERENCE.md及
out/lte-reference-20261102-75886ecf。下一步离线追RemoteFS/SoC分区后端和IPA
端点映射，不重复online请求，不停Windows驱动，不部署EFI草稿。LTE未实现。

**2026-11-02 LTE 按用户要求暂停并保存工作**：单变量DMS online请求返回
result1/error52(DeviceNotReady)；恢复原factory-test模式2成功并回读确认，
boot836903c9…、Android1/guardY/wlan0保持。有效RMTFS指向全零2MiB dummy EFS，
真实本机NV/校准尚未接入；仅凭error52不认定其为唯一根因。MPSS地址更正为
4080000（8300000是CDSP）。新增手动online实验工具的模拟回滚测试、静态构建
和实机失败/恢复路径通过。只读EFS EFI导出草稿仅编译通过，QEMU/实机未测，
未部署ESP、未更改启动包/菜单/固件、未重启。接续必须先验证真实EFS读取来源，
不要重复切模式或把EFI草稿当可部署成果。详细记录及资料见docs/LTE.md；用户
明确恢复之前不继续开发或硬件实验。

**2026-11-02 LTE 开发首轮（codex/lte-support）**：从73ea6ee创建独立worktree，
原main目录未提交工作保留。目标192.168.8.194，boot836903c9…、user10、guardY。
复用只读QMI工具增加NAS和准确DMS模式解码，协议异常/隐私过滤测试及ARM64实机
运行通过。MPSS attached，SIM present；DMS原值2=factory-test（不是offline），
NAS未注册且PS detached，WDS disconnected。当前有效DT没有IPA硬件节点，
已打包ipa.ko无SC8180X匹配；Android未发现标准Radio HAL/rild，vendor含noRil
排除文件。没有切射频、修改DT/固件/属性/启动包、拨数据或重启MPSS；LTE未实现。
原始证据在忽略out/lte-baseline-20261102；开发状态、构建及下一步见LTE.md。

**2026-11-02 后续：EL2停播爆音已由用户确认修复，正常开机取消终端弹出与KVM测试。**

原包d643ea0e…的有限跟踪证实：HAL停播后再次prepare，PA仍为0xfc时就执行
sdw_disable_stream；最终关闭也晚于RDAC/SDW掉电才静音。仅恢复SDM845两处
停流前静音，WSA881x保持原b165ca2c…；模块92852a7b…、试验包17843d5f…，
boot059265e0-ce1a-4a2a-bab2-6138da6343bf通过。跟踪读到两侧先0x7c、再断流、
重建后由原prepare解除静音至0xfc。未改增益、配对端口、HAL或音频transport。
用户确认单次播放“有声音，结束后没有爆音”，随后确认10秒间隔重开、停止及
原应用暂停/恢复均正常。三次AAudio均528000帧/零active xruns；听感以用户反馈为准。

默认内核生成使用`patch-sq2-audio-sequencing.py --machine-only`，若源码含失败的
WSA881x PRE_PMD旧补丁则拒绝，避免静默混入。两启动项共用已验收音频运行包，
pins同时约束SDM845/WSA881x；旧组合补丁仅供历史EL1使用，不在EL2默认中执行。
失败组合为何无声的内部机制未另作实机试验，不能扩大为已证明所有关机时序问题。

用户要求停止调试脚本：正常sq2.el2=1下，el2-terminal-autostart保留显式无密码
用户10登录，但登录后直接退出，不等待镜像、不打开Terminal UI。AVF/SoundWire/
网络等必要准备保持；KVM witness仅在experimental/debug启动运行。手动终端仍可用，
本轮不再自动启动guest做验收。音频跟踪使用独立instance，退出时撤销kprobe和instance，
未动Android自身bootreceiver/wifi tracing实例。测试播放器已退出；本轮上传的trace、
install-machine、install-final脚本已删除，私有回滚载荷和证据保留。

最终构建`out/audio-pop-20261102/final-v2`，两initramfs相同，SHA256为
320e5076063ff78887e9fa43a9ae83784a732687f094e0af5739122d740b6d69。
除SDM845及两个终端helper外，与此前正常包的有效文件一致；凭据最后member、
Image/DT/launcher/菜单均保持。归档差异、补丁幂等与旧组合拒绝、正常用户只登录不
开应用、正常准备不跑KVM探针测试通过。最终boot67c69766-fc61-416d-a2fe-c637fde1e015，
Android completed1、user10、NexusLauncher前台、guardY、AUDIO_READY及模块hash一致。
无Terminal OPEN/guest启动、无KVM witness、无音频探针或本轮跟踪后台；debugfs未挂载。
Android自带security_vm保留，它不是Linux终端guest或本轮调试脚本。默认项启动后才
同步Mesa回退项；两包/清单回读通过，菜单仍9f1ee01e…且恰好两项，ESP剩1586776KiB。
私有证据在out/audio-pop-20261102；设备备份在data/local/tmp/sq2-pop-20261102/
backup、SQ2Default-before-final与SQ2Fallback-before-final。未额外启动Mesa或重测全部
外设；共同音频字节一致不等于重新验证图形或长期声学。以下“爆音仍开放/终端自动
打开”等为历史状态。

**2026-11-02 默认双启动已部署：EL2 / 高通Vulkan / 60Hz / GPU700，音频爆音仍开放**。
用户要求将本轮成果提交主线，默认纳入终端、视频和音频修复，并只保留新构建与
当前能启动的回退项。现GRUB恰好两项：sq2-default → EFI/SQ2Default（高通Vulkan），
sq2-fallback → EFI/SQ2Fallback（Mesa）。正常参数sq2.el2=1；保留screen_only与
nohibernate、原用户/凭据、FUSE和所有已验收外设策略；不再调用实验日志bind、
kmsg收集或360秒恢复。高通仅为既有Vulkan运行包，GLES仍Mesa，不声称全闭源栈。

两个最终initramfs均为d643ea0ee7a4c06cc84705611c30e07fe9ccde08a9c6e0905d0dcc784022e305。
Image c1ef559f…、DT85b436a3…、qebspile3675e36…保持；两launcher分别为
默认dc7b5ac9…与回退1e6da8d0…。菜单SHA9f1ee01e…；构建入口和固定输入见BUILD。
实际归档测试核对全部生效条目、媒体/固件/音频/高通库/凭据及视频清单，除正常
启动钩子外无变化；私有password addon仍最后。默认构建额外固定能出声的模块对。

回退boot8beb943f…通过Android/user10、声卡/AUDIO_READY、video0/no-TZ、
KVM witness42、实际60Hz/GPU700与AAudio528000帧/零xruns。Debian曾正常关机，
重新打开后真实droid@debian提示符可用；不将guest退出误记为board/framework崩溃。
高通默认boot35308ea7…系统vkjson实际枚举Qualcomm Adreno Vulkan Driver，
HAL4dee0a10…、实际音频HAL774cd7e7…；H2641080p自动选硬件解码300输入/输出/渲染，
EOS/STOP/RELEASE通过、9963ms，system_server/SF PID2507/2542不变。原终端
实际返回aarch64、uid1000(droid)、HTTPS200。更多EL2编解码/guest内部硬解未重测。

用户随后报告两项均停播爆音。发现EL2重编音频模块遗漏旧静音补丁，但单独恢复
sdm845提前静音与wsa881x PRE_PMD后（包7c6a4ee0…、boot753b0eb1…），用户反馈
完全无声，虽两次PCM均528000帧/零xruns。该候选失败，已回退为d643ea0e…，
最终bootdd91a353-05ac-4027-8280-5b93d79b7cbe，用户明确确认“已恢复声音”。
**停播爆音未修复，不得将寄存器/PCM成功写成听感通过，也不得默认启用失败补丁。**
声卡模块恢复sdm845 e1d1be6b… / wsa881x b165ca2c…；Image/配置/Module.symvers不变。
下一次先核对原始完整启动/解除静音路径，不能仅凭遗漏补丁推断全部根因。

磁盘身份：BC711512GB/CYA5N050012105J77，ESP B027-0512，userdata4f46d2ec…，
root loop0来自p3。旧ESP完整备份在data/local/tmp/sq2-boot-backups/
before-default-20261102/esp，并有逐文件SHA清单与全部通过的回读记录。
最终菜单原子替换并回读后，删除八个旧实验EFI目录、旧boot载荷、ESP日志及旧安装
资料和过时once环境文件。保留两个载荷、共享GRUB/按键引导器/firmware及磁盘身份
文件；它们不是第三个系统启动项。ESP剩1586776KiB。userdata/系统分区未格式化。
私有证据位于out/default-20261102-v2、out/default-20261102-audio-v2；最终正常构建
为out/default-20261102-final-v2，保留固定Image对应的基线kheaders，避免重编时的
头文件归档漂移。最终重建两initramfs、两launcher及菜单hash均复现已部署版本，
实际归档测试和失败音频补丁隔离测试通过；无需为相同字节再重启。
以下早期“重启待验/默认Mesa/回退计时”记录为历史状态。

**2026-11-02 EL2音频无声已定位并在线恢复，持久包重启验收中**：用户报告无声，
boot仍为3a8da95c…。/proc/asound/cards无声卡，audio-framework日志只有driver
initialization失败。audio-drivers-start在加载任何音频模块前只接受ADSP state=running；
EL2接管的17300000.remoteproc实际为attached。修正为同一ADSP的running|attached，
offline/detached/crashed等仍拒绝并打印原状态；不重启或卸载remoteproc。

只替换RAM helper并运行原framework助手一次，物理card0/三组PCM注册，实际HAL
inode/hash为774cd7e7…，RX7/8仍84，原DMIC/端口/增益/transport未改。AAudio双声道
48kHz播放channel-continuity完成528000帧、active xruns0/exit0，ALSA hw_ptr持续
推进；用户明确确认“已恢复正常声音”。board boot及system_server/SF保持。

按用户要求检查类似问题：源码及设备shell helpers未找到第二处把attached拒为
未启动的判断；EL2 RMTFS正确要求attached。FUSE卷已挂载，触控/键盘发布、蓝牙ON
及桥接、SSH、视频、资源监视日志就绪；CPU schedutil及1.7664/3.1488GHz上限，
CPU80C/5C、GPU75C/5C热策略读回正常。GPU700档为此前已部署基线，不回退为旧661。
module-load另有dummy模块缺失，配置确实未启用；当前未见其影响已验证网络功能，
不为消除告警扩展内核。此检查不等于蓝牙传输/热触发/所有外围设备重新实测。

共享helper及EL2完整packer已修正；测试覆盖running/attached、五类非活动状态、
错误ADSP、缺状态文件和实际packer刷新。仅追加helper的启动包SHA256
9401ea98de94268e563e2ca546479b9a08d3245b0880cafba0b583a78aa46caf，181144746字节；
最终有效条目仅bin/audio-drivers-start变化（f696da16…），凭据末member逐字保留。
已按磁盘/UUID/root/旧包核验、备份、gzip/hash、sync与原子替换部署EL2载荷，默认
Mesa未改。旧包在data/local/tmp/sq2-boot-backups/before-audio-attached-v1。
正在一次性EL2重启验证，证据位于out/el2-android/audio-attached-v1。

**2026-11-01 EL2 Android硬解与Linux终端共存，持久启动验收通过（H264范围）**：
新boot3a8da95c-5451-4216-8039-d68c69938be6，EL2启动包8ef93a9e…首次加载即成功，
无先行PAS失败/热重试。启动清单全部通过，sq2_no_tz=Y、sq2_boot_watch=N，
SID2342/5MiB映射及CPU启动成功，/dev/video0自动发布。实际strip后core SHA
b9b9d83582a548ad2d00c78e55c67723111ade882996d7493f341597f65db35e，decoder SHA
3ccf818ada0d7eebf44a9f1dbface4032aeb90536303b19f978f385d985a824c；helper SHA
a7102e17c061f5ca23e3bfe68bc92cbd0bca4ce07a656684064f9b0a37a34087。

重启后Android High+B六帧Surface/CSD输出与独立NV12参考再次逐字节一致。
普通APK通过MediaCodecList自动选中c2.sq2.avc.experimental，报告hardware=true、
software=false，1080p High300输入/输出/渲染，EOS、realtime、STOP/RELEASE均通过，
elapsed9963ms；两次截图有真实变化。两轮shell exit0，框架PID2942/2982保持，
有限kernel日志无fault/WARN。Debian PID6989从自动启动一直保持，播放后实际原终端
shell显示aarch64、uid1000(droid)、HTTPS200。用户10自动RUNNING_UNLOCKED，
KVM witness42通过，三个remoteproc attached，guardY/nohibernate、pstore空。
本轮只证明Android宿主H264硬解与guest共存；未证明guest内部硬解，也未将EL1的
HEVC/VPx/4K结果转记为EL2验收。长时间、多格式及第三方应用兼容性仍待验证。

正常默认仍Mesa；一次性EL2入口已消费为none，本机EL2载荷已持久更新。
/run/el2-control-keep已建立，360秒恢复助手已确认“Recovery canceled after operator validation”。所有证据位于
out/el2-android/no-tz-boot-v1/{runtime,media,player}及guest-verified.png。
不再把“缺固件SID资料/PAS认证失败/无video0”当作EL2硬解当前阻塞；保留下面历史
实验记录以说明定位过程。后续维护不得热卸载已发布Venus/VIDEOCC/SMMU提供者。

**2026-11-01 no-TZ v4已通过真实帧、Android Surface和1080p播放；持久启动重启中**。
boot841647e4…驻留Venus上，原V4L2 H264 baseline六帧逐CRC匹配、mask3f、输入返回6、
drain/LAST/close成功。Android原有Codec2服务/XML不改，既有High+B帧探针
e3f88650…经Surface/CSD输出6张AImage及EOS；与high-bframes-vector/ref.nv12的
691200字节逐字节完全一致，不能误用baseline的参考文件。1080p High Profile
实验APK普通UID1010254播放300/300输出/渲染，elapsed9960ms、EOS、STOP/RELEASE
全部通过，两次截图确认图案变化。有限kernel采集无fault/WARN；system_server2990/
surfaceflinger2948保持。播放器shell stdin末尾被PowerShell追加CR，打印全部通过后
报“sh: \r: not found”退出1；这是传输尾部错误，保留原结果，不重复解码掩盖它。

Debian旧PID6878在MediaCodec试验前已退出，guest日志显示正常systemd关机及
Power down，不能认定VPU崩溃。重新打开原终端后PID5594，实际shell得到aarch64、
uid1000(droid)、HTTPS百度200；截图guest-verified.png已取回。早先SSH连接间歇超时
后恢复，boot ID不变。证据统一在out/el2-android/venus-no-tz-v4/{runtime,decode,media,player}。

pack-sq2-el2-android.py新增显式--no-tz-overlay：仅EL2实验启动，先hfi overlay、
固件SID overlay，再core sq2_no_tz=1和decoder；不带boot-watch，不接PAS观察器。
默认PAS helper逐字不变，真实helper的加载顺序/失败中止及原manifest/runtime测试通过。
新包out/el2-android/no-tz-boot-v1/android-el2.cpio.gz为180505061字节，SHA256
8ef93a9ef232bc655a026b0800e26d2fe25bce696bee17de700a65e441eb0ca1。
与实际旧包有效条目对比，init/所有媒体用户态/固件/凭据末member不变；差异为
video helper/core/decoder/新overlay/manifest、移除PAS观察器及匹配内核的kheaders/
builtin索引。各清单已核验。已识别NVMe/ESP/UUID/root后备份并同卷原子替换EL2
initramfs，正常Mesa默认和Image/DT/EFI均不改。旧包/清单位于设备
data/local/tmp/sq2-boot-backups/before-no-tz-v1；为腾ESP空间，将旧before-terminal-boot-v2
完整复制/cmp/hash/gzip验证到no-tz-v1-old-terminal后才删除其ESP副本。
已设置一次性android入口并请求重启；需取新boot证据并验证首次启动、播放及原终端。

**2026-11-01 no-TZ v4已发布/dev/video0，实际解码仍待验证**：仍为boot
841647e4-cdd1-4d82-bdf0-cfeeaa7de6f7、新SMR内核c1ef559f…、guardY、Android完成、
用户10。v3将显式诊断等待延长到2000次并观察既有状态/队列/SFR，始终为0，
正常复位回收；等待时间不是解决方案。v4仅将NONPIX_START/END从0改为固件span
（5MiB），保留FW/CPA的0..span。原Windows PIL设置是中间状态，不是CTRL_INIT前
最终保护配置。固定Linux42固件的早期ARM指令根据这些边界建立页表；旧组合
令五个固件section descriptor无效，span..span使之有效，与上游no-TZ一致。
tests/test_sq2_venus_boot_ranges.py固定固件hash和实际指令并验证该区别。

v4映射SID2342、IOVA0→8c000000共5242880字节并逐页核对，CPU释放成功；
poll8的CTRL_STATUS=40000001，SFR以“Init SFR msg, NO”开始，qcom-venus绑定并
发布/dev/video0，core refcnt3。core SHA11ae7769d5d240ad6c12076a187b1a8fbd5bd181279919c40bd4a879db5d1d64，
decoder SHAdcbf97dfcacf65aa48c0021d14f65a9406e063eb33a68002d738c7892d9fa8c1。
模块来自/root/.cache/sq2-venus-no-tz-v4，临时加载参数含sq2_boot_watch=1；
该观察器默认关闭，正常100次等待已经足够。有限采集位于/logs/venus-no-tz-v4。

**不要卸载已发布的Venus或其VIDEOCC/SMMU提供者**。启动包仍为旧PAS版本，
v4尚未持久化；本轮尚无输出帧/MediaCodec/应用播放证据，不能宣称EL2硬解完成。
下一步使用驻留driver验证V4L2像素、Android播放和guest shell，然后接入EL2候选
启动包并重启复验，保留默认Mesa和凭据末member。后续SSH一度TCP连接超时，
未取得新的boot ID，不能据此认定崩溃或重启。

**2026-11-01 扩展SMR补丁已实机重启，冲突消失但HFI仍未就绪**：当前boot
841647e4-cdd1-4d82-bdf0-cfeeaa7de6f7，Image为
c1ef559ff8d240de4f9314ba9d1a2586e7dac3b8eae777bff2a90ed72dd57a1c（30794240字节）。
隔离源码/build在/root/.cache/sq2-el2-smr-20261101；配置、Module.symvers和release
与network-20261101逐字一致。只替换EFI/SQ2EL2Android/Image及对应清单；原Image/
清单备份在设备data/local/tmp/sq2-boot-backups/before-smr-20261101。initramfs仍为
c17fa3fd…，DT/EFI/凭据及正常默认Mesa不变，once已消费为none。

首次SSH接回已Android完成、guardY、用户10；KVM witness42通过，Debian PID6878，
三个remoteproc保持attached，网络正常。/run/el2-control-keep已创建取消360秒恢复。
QEMU实际Image的只读guard及freeze/mem/disk拒绝测试通过；旧smoke命令缺少本机
lsm=selinux导致KernelSU在QEMU先崩溃，补齐与实机相同的lsm/enforcing参数后通过，
没有为精简环境的失败修改KernelSU。

no-TZ v2仍只通过/tmp上传模块执行，没有纳入启动包。core SHA
75b985e2f1e81df6666ad7cc2589699d43f0a919e693fa6f36c0289730b306e2，
相对v1仅抽出load helper并按新build重编，映射/寄存器行为未变。SID2342映射与
CPU reset释放仍成功，**本次有限采集中不再有SMCF或其它SMMU fault**；HFI的
venus_boot_core仍在约100ms后-110，没有/dev/video0；当前代码未记录末次
CTRL_STATUS，不能把超时直接记作“所有采样均为0”。
随后reset_assert0、unmap完整5242880、memory_released，Android/用户10/guest进程
继续存活，boot ID不变。采集见out/el2-android/venus-no-tz-v2/runtime。
不能宣称完整固件执行或解码成功。下一步围绕CPU启动后的HFI初始化作有界观察，
区分固件未运行、晚于当前等待窗口、共享队列未写入；不再重复已定位的PAS/EL1/
128组GR0比较。Windows79与Linux42 ELF物理base均f500000，链接vaddr从0开始；
当前5MiB IOVA0映射与MDT relocation方向一致，未替换固件。

**2026-11-01 no-PAS候选首次实机启动：确认SID2342，定位扩展SMR冲突**。
仍为EL2 boot81493270…、guardY、Android完成、用户10；正常默认Mesa未改。
前置只读复位探针成功：power_end0、aae3000=0x10、完整释放/卸载。
模块SHA05592285…；日志在out/el2-android/venus-reset-probe-v1/runtime。

新增默认关闭的venus-core参数sq2_no_tz，固定42版固件和原8c000000内存lease，
独立child SID2342/mask0，IOVA0..5MiB逐页检查，跳过PAS认证/保护；复用原电源/HFI。
首次实机map成功、Windows参考寄存器写入和reset释放读回成功，但HFI超时-110。
实际SMMU报告GFSR80000004/GFSYNR0c/SID2342。随后reset_assert0、unmap5242880、
memory_released；boot ID不变，无/dev/video0。不能记作硬解成功或完整固件执行。
core模块SHA0c5a9c11…，证据在out/el2-android/venus-no-tz-v1/runtime。

GFSR bit2是SMCF（流匹配冲突），不是未知SID。独立只读快照发现ID0=7c017ed4，
硬件212组；测试consumer释放后，**SMR183=80002342、S2CR183=30000373**仍存在。
旧qcom驱动无条件裁剪到128，遗漏该固件已有项。上游5583a55e074b修复同类裸机
视频启动冲突：固定用S2CR127判断bypass quirk，仅有该quirk时裁剪；无quirk时保留
完整固件表，而非清空其它remoteproc项。已准备严格源码hash的移植脚本，待隔离
内核构建/重启及HFI验证。不要在旧内核热重试no-TZ或直接清除高位SMR。
出处：https://patchew.org/linux/20250821-arm-smmu-qcom-all-smr-v1-1-7f5cbbceac3e@linaro.org/

离线验证：同内核模块编译、21项HWIO/9项负例、实际寄存器helper的读写边界/
保留位/复位失败、8PAS+4no-TZ分支、16种reset/unmap失败、原16种固件事务隔离
均通过。快照255组边界/掩码匹配测试通过；快照实机已卸载。未访问TZ阈值读寄存器。

**2026-11-01 CSRT与安全PIL关联到新的固件流候选2342**：沿新采集的
QcSkExt8180追踪到CSRT/QCOM device100c资源组。首个SMMU记录base15000000，
592条、每条10字节；视频附近11条中，2342原始条目为4223010000000c010000，
domain byte=12。PIL PAS9描述符的mem_assign回调9760在认证成功后先4→14，
再将各有效固件段14→12；CSRT规则亦明确包含14→12和12→14。
这是2342作为固件流的交叉证据，不是SQ2运行期取指实测，也不是2302依据。

已确认3=HLOS、4=HYP、14=HLOS_UNMAPPED的字符串/分支对应。3→4先经
CreateSecureSectionSpecifyPages/MapViewOfFile建立Windows安全映射后返回；
4↔14在trustlet中为空操作。14→12调用AssignMemoryToSocDomain，并支持
固件链接地址与物理装载地址不同。不可把这些Windows句柄操作照搬成Linux SCM。
3d20(1)是1微秒忙等，不是缺失的时钟API。视频MMIO四页aae0000/1000/2000/3000
在该扩展CSRT策略中，通过OpenCurrentExtension/MapSecureIo映射，不能据此
认定Linux EL2可直接访问。

新增inspect-sq2-windows-pil-reference.py固定扩展/CSRT hash，核验11组指令、
API import、PAS9回调、流条目、8项内存规则及MMIO策略。真实输入通过，8项
错误hash/校验和/边界/流编号/规则/指令负例均拒绝。详细依据与命令见
docs/SQ1-VIDEO-REFERENCE.md的2026-11-01续接；报告在忽略目录
out/el2-android/sq1-reference-20261101/pil-csrt-verified.json。
本轮没有设备访问、部署、重启或热重试PAS。下一步以2342/domain12为有依据
候选核对Linux固件IOVA与复位访问；仍需厘清TZ认证前置条件和安全回收。
硬解目标继续active，不再将“没有任何固件流资料”当作阻塞理由。

**2026-11-01 新Windows采集已取回，硬解分析取得新的实际实现材料**：用户告知
SQ1材料可作SQ2参考。当前分支已从008e70d快进至8f64975，未提交的tracked diff
hash前后相同。按分卷及完整ZIP SHA恢复到out/el2-android/sq1-reference-20261101；
72个manifest文件全部核验，8张ACPI checksum正确、issues为空。qcdxkm8180、
qcvss8180及IORT均与此前已知参考hash相同；原Windows指令检查6项/映射10项通过。
Windows状态为HypervisorPresent/SecureBoot=true、VBS=2。

关键新增可分析入口是PIL包中的QcSkExt8180.exe，SHA256
da09677b8bc3f35c4217e768f73a1ccc19e112cedf724bd14d393e1545511e22；INF将其作为
async secure PlatformExecute注册。它包含视频PIL启动/保护实现，不能只分析qcdx。
已用字符串交叉引用及真实ARM64指令核对video_bring_up(14000a280)调用
video_init_and_start(14000a1c0)；后者成功路径依次调用a090/b4f8/b930。
寄存器访问helper在140003c34使用32字节表项的mapped pointer+offset；
初始化指令指定RVA1b8a0、177项。十个相关项的base+offset确认为0aae0000、
0aae3000、0aae1028/102c/1020/1024/1030/1034、0aae2010/2000。
五组固定指令及十表项断言通过，见pil-static-audit.json；原始反汇编仅留out。
微软符号服务器对该精确PDB GUID/age返回404，未取得符号，不冒称有PDB函数名。

这解除“缺少可继续分析的Windows启动实现”的材料阻塞，但尚未证明本次Windows
执行了这些分支，也未确定固件SID、Linux EL2访问权限或ffcfffb4含义。下一步应沿
QcSkExt的PIL认证、内存分配及寄存器表映射追踪，而非重复已完成的GR0/EL1实验。
本轮没有设备操作、没有应用Windows寄存器序列或启用SHM，硬解未修复。

**2026-11-01 EL2硬解后续可行性复核，未执行新硬件实验**：只读确认当前仍为
boot81493270…、Android完成、用户10、Debian PID6762、guardY，恢复已取消；
/dev/video0仍不存在。检查“用SMMU未知流故障识别固件SID”的替代办法：当前
arm_smmu_global_fault确实报告GFSYNR1低16位，但制造取指故障首先需要释放
VPU CPU。现用V4 no-TZ路径会写wrapper的FW/CPA/NONPIX阈值和A9复位；已验证
的HFI CTRL_INIT(0xd2048)只是邮箱启动协议，不能替代CPU复位或证明上述寄存器
可访问。因此不能仅凭HFI成功经历就进行此实验。静态源hash/结论保存在
out/el2-android/venus-no-tz-feasibility-v1/audit.json；没有新增寄存器访问、
固件认证或重启，没有修复硬解。缺失的证据仍是SC8180X无PAS启动时的固件CPU
复位/保护访问权限，以及固件取指流和IOVA交接；不能从SC7180示例直接推定。

**2026-11-01 完整initramfs已纳入现场运行钩子并完成离线比对**：正式packer新增
显式--passwordless-user 10及可选--scm-trace，并纳入已部署的360秒EL2恢复助手。
真实构建parity-pack-runtime-v1为180505535字节，SHA256
02ccb2f01a75e5b81abd6beccaef4c0bed7c4726ae1c6cd27109a72c45fbb2aa。
按最后条目审计，与设备现用terminal-boot-v2相比，init及全部bin助手逐字相同，
原凭据gzip末member逐字保留。有效差异严格为venus-dec及其清单、四个builtin
模块索引；两份venus-dec均与此前section审计输入逐字相同，差异仍仅build-id及
源码路径字符串。最终视频清单逐项通过。hook顺序/可选观察器/重复拒绝、恢复
guard/取消/回退测试和原配置/视频清单/16场景终端测试通过。
审计见out/el2-android/parity-pack-runtime-v1/runtime-comparison.json。
未部署、未重启，不将重建包记作新实机验收；launcher/DT生成整合仍待补齐，
EL2宿主硬解认证失败仍未解决。用户10无安全锁，未清除凭据。

**2026-11-01 现用qebspil已从正式脚本逐字节重建**：发现prepare脚本未包含现场
Venus metadata调用及DSP原始结果交接。新增显式--venus-firmware，先核验已验证
MBN的长度/SHA，再按原fw_load_metadata规则生成私有header；不传参数保持原路径。
从固定上游及submodule生成的Makefile和所有顶层src文件统一LF后与现场构建源相同；
实际AArch64构建得到81797字节，SHA e3675e36a749176481742a8ac78b8d505a40e3ea2a87464b123c45dd3080fe95，
与设备ESP现用qebspil完全相同。错误长度/内容拒绝、已存在目录拒绝、默认路径及
已有EFI/Linux契约测试通过。产物/审计在out/el2-android/efi-repro-venus-v1。
只做主机构建，无部署/重启；设备仍boot81493270…、用户10，恢复助手已取消。
这只补齐qebspil生成，不是完整六载荷发布流程：launcher/DT及initramfs中的
EFI视频/恢复hook整合仍需纳入正式构建。EL2宿主硬解认证失败仍待解决。
本轮源码/公开资料复核未找到足以安全启动no-TZ或EFI全启动的新映射证据，未
重复GR0/EL1对照，也未猜SID、修改复位或调用SHM。

**2026-11-01 KernelSU应用授权、撤销均通过，临时实验已收尾**：当前EL2 boot
81493270-dbe5-4f78-b442-2b7026c05e2a，首次SSH接回前已自动切到用户10并打开
原Debian镜像，crosvm PID6762/CID2048；恢复日志明确Recovery canceled。
临时org.surface.sq2.rootcheck仅安装到用户10，应用UID1010269。由官方Next管理器
只开启此应用超级用户后，应用自身执行su -c 'id; id -Z'，exit0、uid0及
u:r:ksu:s0均通过。关闭同一授权后再次执行返回su ENOENT，证明撤销生效；
随后卸载该应用、删除上传APK/临时截图。未给其它应用授权。
截图及清理记录在out/el2-android/root-app-check-v1；这覆盖应用授权路径，
不等于全部模块兼容性。终端已重新调到前台，实际id返回droid uid1000，截图
terminal-id-final.png。末次取图曾SSH超时，下一次连接自行恢复、boot ID不变；
未改网络或重启，不能记为板级崩溃。

**EL1 SMMU差分实际结果为阴性，停止该路线**：boot
f516d8ce-7d26-46da-a2c5-81de24f624d3的首次PAS9 auth=0，但128组SMR/S2CR全部
不变（changed=0）；16项有效，视频数据SID2300/2320/2340前后均为CB9，
没有取得固件SID2302证据。观察器captured1/missed0并正常卸载。
这只排除“通过本次Linux可见GR0前后差分直接找出固件流”的办法，不证明安全
视图无映射，也不能据此猜SID或重复该实验。控制启动Android完成但无wlan0，
未追查这个隔离环境的网络；360秒助手核验后选择EL2并自动返回上述boot，
用户10/终端自动恢复。证据在venus-smmu-trace-v1/runtime-summary.txt。
核验NVMe/ESP/root、原始backup的hash/gzip后，已原子恢复EL1控制addon及清单
（addon恢复1aab58ad…）；完整清单通过，once=none，未安排后续启动。
持久默认仍Mesa，EL2宿主视频认证仍失败，guest硬解仍未验收。下一步须取得
新的固件资源/映射/重置交接证据；不再重跑已完成的EL1认证或GR0快照。

**2026-11-01 EL1视频SMMU只读差分候选**：EL2终端boot600d4d3c…稳定且恢复助手
已明确取消。新假设是成功的EL1 PAS9认证会改变Linux可见SMR/S2CR，差分可能
定位QHEE的固件流；无变化或无认证成功则不推导SID，不继续猜2302。
新增sq2_venus_smmu_trace只允许Surface/SC8180X/EL1，单次insmod调用PAS9前后读
apps SMMU GR0的128个SMR/S2CR，不读CB/TZ/页表，不写寄存器或发起PAS。
同内核模块编译通过，真实读循环ASan/UBSan边界/无写入测试通过；EL2实机加载
拒绝EPERM且无模块残留。使用已成功EL1分段对照的Image/DT/EFI与视频模块，
只替换控制addon，另加到期自动选择已核验EL2终端的回程（失败仍正常默认）。
候选venus-smmu-trace-v1：addon b67cf7e2…，module7366250a…；4项有效差异、
凭据末member相同。停止条件为一次认证或360秒回程，不重复调用PAS或卸载provider。
10:12UTC已部署并发起一次EL1重启，等待差分及自动EL2回程。公开Carina ACPI模板仍只列非安全/secure数据页表，没有固件SID；
原始IORT模板checksum未完成，不能冒称运行设备表；未改现有SID/IOVA。

**2026-11-01 无人操作重启进入用户10及Linux终端通过**：boot
600d4d3c-c0e1-43ac-a075-1ea7af8861bd。实际initramfs为c17fa3fd…；
helper为584a959b…，开机日志依次出现无密码用户启动、user started、
OPEN user=10、Status ok。首次SSH接回前已自动完成切换，未执行手动start-activity。
系统用户10为RUNNING_UNLOCKED，Debian PID7019/CID2048，截图实际显示shell；
输入id/uname后得到droid uid1000和guest 6.12.93，HTTPS百度返回200。
Android完成、screen_only=Y/nohibernate和KVM witness42通过，已写keep marker。
证据位于out/el2-android/android-el2-terminal-boot-v2的runtime.txt及两张截图。

本轮只修init调用及helper。原六载荷中仅initramfs变更；原trace-v2保存在ESP
EFI/SQ2EL2Android/before-terminal-boot-v2。旧before-terminal-autostart已先复制到
/data/local/tmp/sq2-boot-backups/before-terminal-autostart，校验两文件hash及gzip后
移出ESP；无账号/凭据删除。16场景helper测试、2项打包边界测试及真实归档审计通过。
once.env消费为none，持久默认仍Mesa；本次EL2终端入口自动进入原用户已验收，
不是普通默认已切换EL2。EL2宿主视频auth仍-22，完整硬解适配未完成。

**2026-11-01 终端自动切换修正准备**：当前boot304594b1…，用户10已恢复真实
Debian shell（PID10363/CID2048）。旧SSH等待客户端挂起且远端helper已退出，
不计为自动启动成功。用户明确说明未设密码、授权必要时清锁；实际
lock_settings is-user-secure --user 10=false、get-disabled=true，故不清除任何
凭据。此前反复请求手动解锁缺少这项检查，后续先区分用户切换与安全锁。
新helper仅在明确传入用户10且系统报告无安全锁时start-user -w / switch-user，
不改凭据或账号；未传用户保持等当前用户行为。16场景及当前设备直接调用通过。
假设：开机调用遗漏及默认停留用户0阻止原镜像自动打开。唯一变化是当前trace-v2
有效init和helper，保持EFI/视频/网络/恢复载荷；新窄打包器审计两项差异及末尾
凭据逐字相同。候选android-el2-terminal-boot-v2（c17fa3fd…），v1未部署。
判别：重启后无人操作即出现用户10 RUNNING_UNLOCKED、helper启动日志及实际guest
shell；停止条件为一次完整启动或360秒恢复，不在失败时改账号/格式化数据。
ESP旧before-terminal-autostart仅在迁往/data/local/tmp/sq2-boot-backups并验证hash/
gzip后移出，为原子替换腾空间；当前trace-v2另留现场回退。09:58UTC已安装并发起重启，等待实机验收。


**2026-11-01 EFI视频资源查询实机通过，未启动视频固件**：boot变为
304594b1-731b-4ffe-b5e0-73ad6e406646，已回原EL2 Android。当前Clock协议
0x10006；七个clock及三个power-domain名字均返回0和非零ID，不存在名称的
两个负对照返回EFI_PROTOCOL_ERROR(0x8000000000000018)。这证明接口和资源
名字可用，不证明Enable/Reset/PAS或硬解。原payload未换，auth仍-22，shutdown0。
Android完成、guardY/nohibernate、KVM witness42通过后已写/run/el2-control-keep；
once.env已消费为none，默认Mesa仍保留。报告/内核日志/audit在uefi-video-inventory-v1。
当前用户0，已请求用户解锁原桌面；新终端helper正通过SSH有界等待原用户镜像，
尚未作为boot hook部署。不得再为取得同一EFI资源列表重启。

下一阶段还必须解决固件映射与重置交接：本机内核的qcom_smmu_cfg_probe把有效
SMR对应S2CR改为bypass，arm_smmu_device_reset禁用所有未分配context bank；
现有sq2_venus_power_on也会脉冲四个reset。因此不能只在EFI认证然后让现有
Linux loader照常接管。尚未证明视频FW SID/页表存续，未增加通用SMMU旁路或
改动其它设备的隔离；先建立明确的资源、映射和firmware owner交接再做启动实验。

**2026-11-01 EFI视频资源查询实验准备**：基线仍trace-v2/boot1f4a2290…。
公开Carina固件提取物（SurfaceDuoBinaries commit1f458b7914a4b68f787662844bd591b3892303a0）
的ClockDxe协议0x10006包含七个目标时钟和mvsc/mvs0/mvs1电源域，后者寄存器
0xab00814/874/8b4与Linux资源对应。PILDxe的uefipil.cfg没有Venus项，未假定
现成PIL proxy支持视频。公开二进制不是当前设备同hash证明。
假设：当前EFI也提供这些资源的名字查询。唯一改变是在既有EL2 launcher前
运行独立inventory.efi；只用GetClockID/GetClockPowerDomainID及不存在名称负
对照，不调用开关电源/时钟、reset或SCM。精确版本不符时不查；记录到独立
EFI/SQ2VideoInventory/clock-inventory.txt后链回原el2launch，原六载荷不变。
全部成功且负对照失败可支持后续设计EFI资源管理；缺项则不扩展EFI固件启动。
停止条件：一次查询报告或原360秒恢复，不在设备重复试调用。
ABI/列表/版本/空接口/边界主机测试、AArch64编译、实际PE类型和QEMU启动通过。
QEMU目录式vvfat写回异常，换真实FAT镜像后报告逐字正确，保留已有报告及
LoadImage/StartImage下一EFI的完整路径也通过；此为工具验证，非资源实机结果。
源码probe/virtualization/sq2-video-efi-inventory.c，私有产物uefi-video-inventory-v1；
已核验物理盘/ESP/root及六载荷hash，安装独立菜单入口/一次性videoclocks选择，
原默认Mesa/救援保留。菜单1745ab3f…，备份grub.cfg.before-video-inventory-v1；
EFI为ec17f024…，09:38:20UTC已请求Android直接重启，等待SSH及实机报告。

**2026-11-01 终端启动调用遗漏已修源码，未部署新包**：复核有效trace-v2归档，
虽含bin/el2-terminal-autostart，init及其它helper均没有调用它。旧记录中的
“自动打开”不能作为boot hook通过证据。现将helper纳入probe/virtualization，
正式packer同时安装并在实验启动块异步调用；保留原用户已有镜像及保护检查，
删除无用UI dump/启动后诊断采集。隔离shell的11种场景通过，设备上直接执行
新helper退出0，ActivityManager返回Status: ok并交给用户10已运行的Activity。
第一次PowerShell文本管道尾部额外CR导致shell退出1；改用原LF字节后通过，
不把该传输错误算作Android故障。未重启或替换运行文件，boot仍1f4a2290…，
Debian仍PID21870/CID2049。开机自动打开仍需候选部署及重启验证。
实跑packer产物parity-pack-terminal-autostart-v1为180501306字节，SHA256
e1b0871186390917b12c60aae78a9ebc1efe2b56f88633e57ead1765eb76cb6d。
与上一explicit-network包的有效差异严格只有init及新helper；调用恰好一次，
两个有效shell语法通过，凭据末member逐字相同，审计见autostart-audit.json。
配置/视频清单回归通过。它仍不包含运行候选的EFI准备/恢复/观察器整合，不能
直接替换ESP现用镜像；本轮未部署、未提交Git。

视频只读资料核对：Linux v7.0的qcom_tzmem仍列SC8180X黑名单；上游2026-05-13
解释QHEE在auth前准备SHM、auth后建立映射并启动核，与当前EL1/EL2差异相容，
但不能证明本机0xffcfffb4的具体含义。没有新判别证据，不执行SHM/no-TZ/EFI
全启动实验。参考https://lists.openwall.net/linux-kernel/2026/05/13/1494及
https://raw.githubusercontent.com/torvalds/linux/v7.0/drivers/firmware/qcom/qcom_tzmem.c 。
EL2宿主硬解仍未修复，终端共享清理状态保持，不重新做已完成的EL1分段对照。

**2026-11-01 已从EL1对照恢复原EL2终端候选**：核对NVMe/ESP/root/guard、
当前菜单dbda3ee0…和6项EL2载荷hash后，只原子替换一次性env为android并重启；
没有改载荷或持久默认。当前boot为1f4a2290-022f-4760-9592-f6bfb2bddff3，
Android完成、guardY/nohibernate、KVM witness42通过；恢复助手明确记录
Recovery canceled after operator validation。用户确认解锁，活动用户10。
Debian实际shell恢复，HTTPS百度返回200；Debian镜像站guest超时、宿主同站
连接重置，原生转发规则/双向流量存在，未因此改Wi-Fi或防火墙。
guest正常poweroff后旧PID17548退出，重新打开为PID21870/CID2049，Android
boot ID不变。证据在android-el2-venus-trace-v2/return-*.png及return-kernel-live.txt。

临时Documents/SQ共享清理已完成：目录先删导致UI取消共享失败，故恢复空目录，
正常重启guest后从设置取消；UI显示无共享文件夹、terminal_settings.xml已无
该URI，随后rmdir空目录成功。后续测试顺序应先取消共享，再删除目录；不改
运行中偏好文件。此次未复测双向写读（此前已通过）。
首次视频认证仍为原a0=-2/a1=0xffcfffb4，shutdown0/内存释放，观察器卸载。
不再重复EL1分段对照；EL1已成功的事实排除了分段流程本身必然失败。
上游SC7180的ChromeOS式no-TZ路径不是本机已验证方案：SQ2现有启动分支要求
use_tz，直接增加video-firmware节点不能完成适配；未读取TZ寄存器、未启用SHM。
EL2宿主硬解仍未完成，guest硬解未验收，正式EFI构建整合仍待补齐。

打包器已去掉写死旧内核目录，必须明确传入--kernel-build/--venus-build，
并在生成候选前检查已验证的网络依赖。配置缺项逐项拒绝和原视频清单测试通过；
用network内核及venus-preboot-v1实跑产物parity-pack-network-explicit-v1，
180500567字节、342模块。与运行trace-v2相比341模块逐字节相同；venus-dec
仅ELF的build-id和编译源路径字符串不同，其余section均相同，不能写成整文件
hash相同。原凭据末member字节相同。其它有效差异为恢复/自动打开/SCM观察hook、
init、对应视频清单和重建的builtin模块索引，见产物比较JSON。未部署新包；
该修改只消除旧内核输入陷阱，尚不是完整EFI/附加助手可复现发布流程。

**2026-11-01 Venus分段生命周期EL1对照通过认证，缩小EL2故障范围**：假设是EL2切换/QHEE
退出导致同一EFI metadata准备后的Linux认证失败。基线为trace-v2的927a63a9…。
保留同一Image、基础initramfs、qebspil、Venus固件/模块和观察器；只取消Secure
Launch，配套EL1控制标记/初始化分支及EL1 zap-shader状态。复用已测试恢复助手。
EFI stub源码已核对支持多个initrd=按序拼接；0.65MB addon的有效条目仅init、
el2-rmtfs-start、el1-android-recovery，末尾为原凭据gzip member且字节完全相同。
DT语义、完整归档最终生效条目、旧菜单保留、shell/GRUB语法和EFI编译通过。
候选out/el2-android/android-venus-el1-control，源码/root/.cache/sq2-venus-el1-control。
判别：EL1下同一no_init/auth成功则排除“分段流程本身必然失败”，转向EL2交接；
相同认证错误则先回查EFI准备的状态/生命周期。若固件尚未运行到同一路径，结果
不具有该判别力。停止条件：首次认证/关闭结果或360秒恢复，不热重试/卸载provider。
不将此EL1对照当作终端完成，不启用SC8180X SHM。

身份/ESP/root/guard及基线hash校验通过后，已部署独立EFI/SQ2VenusEL1Control，
基础EL2目录未替换；菜单新增独立入口与消费一次选择，原菜单保存为
EFI/BOOT/grub.cfg.before-venus-el1-control。新菜单SHA256
dbda3ee04324a18ed37ffef3b8e73a0bd1ca5cb95f5d4f930bebec098b89ccf0；
addon1aab58ad…、DT199579b5…、launchera61352a1…，完整清单在候选目录。
约08:40UTC通过Android直接reboot发起；用户确认已进桌面/锁屏，但SSH未接回。
随后用户报告恢复，已从正常Mesa取回完整日志，对照boot为
9c428d0d-dad3-4f15-8f2f-2ef0ccfec671。日志证明全部CPU在EL1、Android完成、
efi_prepared_no_init、mdt_load_end0；auth42000205的原始a0/a1均0，外层0，
视频助手输出SQ2_NATIVE_VIDEO_READY_NOT_FRAME_PROOF，观察器missed均0且卸载。
因此同一分段生命周期在EL1可完成认证和视频设备启动，反驳“EFI到Linux分段本身
必然失败”；差异限定在EL2切换后的固件调用环境，尚未确定具体缺失的QHEE服务。
此次未执行解码向量，不声称此对照已通过帧验证。支持查询EL1为0而EL2为1，
进一步说明该查询不能作为视频功能门槛。

对照180秒时无wlan0、无remoteproc sysfs；恢复助手实际记录EL1 control finished。
没有为这个独立对照再修改Wi-Fi，也没有热重试PAS。当前已回正常Mesa，boot
aabcb6f5-e899-40fe-8682-ccf15e327e6a，Android完成、活动用户10、guardY、SSH、
/dev/video0均在，/dev/kvm不存在符合EL1。原SSH客户端未自行退出只是客户端状态。
证据位于候选目录/googlebook-native-9c428d0d-dad3-4f15-8f2f-2ef0ccfec671。
EL2终端配置/镜像保持，正常默认未改；下一步恢复EL2时仍需通过菜单明确选择。

**2026-11-01 最新状态与调试纠偏（优先于下方历史快照）**：用户要求总结Wi-Fi绕路，
避免在视频适配重演。已将完整Android优先、单假设/判别结果、连续两轮无新增判别
证据就切回基线对照的规则写入AGENTS.md；复盘见SQ1-EL2-PREPARATION.md末节。
板载Wi-Fi在v15完整EL2 Android中已通过，精简诊断失败的具体环境差异仍未唯一定位；
Debian转发问题另有完整重启和HTTPS/下载证据，不能把两个问题合并归因。

当前为android-el2-venus-trace-v2，boot927a63a9-9412-4a2f-af31-6f15b07875d5。
相对venus-v1只加入匹配内核编译的现有有界SCM观察器和加载/退出卸载hook；
Image/EFI/DT/视频模块不变。Android完成、用户确认原桌面且活动用户10、SSH、
全CPU EL2及KVM witness42通过；guardY/nohibernate保留，恢复明确取消。
Next临时模块目录已为空。venus-v1和本轮分别重测Debian HTTPS均为HTTP/2 200。

DT合同1/9/0x8c000000/0x500000、efi_prepared_no_init、mdt_load_end0出现；
首次auth实际SMC为42000205/arginfo1/PAS9，原始a0=fffffffffffffffe，
a1=ffffffffffcfffb4，外层映射-22。shutdown原始0/0，内存释放，无/dev/video0；
观察器missed均0且已卸载。这是实际SCM认证失败，厂商状态0xffcfffb4
含义尚未知，不能推断为签名错误或EL2永久不支持。没有热重试或卸载视频provider。
原始证据在忽略目录out/el2-android/android-el2-venus-trace-v2/kernel-live.txt。

只读INFO/IS_CALL_AVAILABLE查询：PAS init/auth=1、GET_RSCTABLE=0、SHM
enable/delete/create=1、无效PAS负对照=0；未执行任何SHM操作。主线v6.18
qcom_tzmem明确将qcom,sc8180x列入黑名单，Surface Pro X已有首次SHM调用挂死报告。
因此“接口可查询”不等于可安全使用，不能盲目回移新平台EL2 remoteproc的SHM方案。
参考https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/firmware/qcom/qcom_tzmem.c
及https://lkml.iu.edu/hypermail/linux/kernel/2404.0/05545.html；查询证据scm-capabilities/runtime.txt。
下一步对照正常EL1/原生EL2首次调用和有效固件生命周期；没有判别证据前不扩大EFI启动。

本轮通过Terminal设置的系统目录选择器，仅授权新建Documents/SQ。Debian普通用户
在/mnt/shared/Documents/SQ读出Android侧写入标记，并写回另一标记，宿主读回一致。
双向文件共享通过，无驱动/权限补丁。证据share-bidirectional.png及share-runtime.txt。
两个标记文件已核对后删除，空SQ目录已rmdir。删除共享授权的UI操作未确认生效：
用户已切到其它应用，terminal_settings.xml仍保留这条已不存在目录的URI；后续在
Terminal设置移除此测试项即可，不要改正在运行的应用偏好或授权整个存储。

用户授权直接操作设备、重启/清理、自动解锁与桌面操作；保留原账号和凭据，未找到
现有解锁凭据，不重置。本次仍由用户手动解锁。当前失败的是EL2 Android宿主的
视频硬解，不能表述为仅Debian不可硬解；EL1已通过六帧硬解，guest内硬解尚未验收。
正常默认仍Mesa，视频候选未晋升；正式构建器尚未接入此次EFI准备。
当前实验initramfs SHA256=7f4cb5a982f863fb0c5771f9fab86365666dcdca7acfefdbff2447fb72ccf604，
凭据member最后且字节不变。ESP仅约99540KiB可用，不盲目再复制185MB候选/备份；
需要部署时先核对重复实验备份后定点清理，保留正常/救援与可用回退。

**2026-11-01 EL2终端联网与Next启动生命周期通过，视频候选准备中**：网络候选
android-el2-network-v1已部署并完整重启，boot cc84d494-ee06-485b-a13a-6c50305ce307，
Android完成、guardY、原用户10、KVM/SSH正常。自动打开Debian PID17434/CID2048。
仅添加CT/FTP/PPTP及GRE共6个内核配置，并在Android前加载sch_ingress/cls_bpf；
原生tetherctrl转发规则实际出现，guest HTTPS deb.debian.org返回HTTP/2 200，
实际下载README并读出内容通过。用户也在终端执行curl百度并获得页面。
未手工放宽防火墙。恢复已明确取消，正常默认仍Mesa。网络Image3b6d3e68…、
initramfs85c59537…；342模块符号检查通过（MODVERSIONS本就关闭，不能称CRC ABI验证），
QEMU只读screen_only及拒绝freeze/mem/disk通过，归档凭据member相同且最后。

Next临时script-only模块通过官方ksud安装，重启后post-fs-data/service/boot-completed
三个文件均为此boot ID、uid0和u:r:ksu:s0；无系统文件覆盖。已通过官方命令禁用并
标记卸载，下次启动应移除。管理器此前显示工作中33129/Kprobes；未给第三方应用
添加root授权。证据android-el2-network-v1/runtime-checks.txt及https.png。

视频仍是PAS9 init -22。新增sq2-venus-preboot.c只让EFI准备已固定固件的认证
metadata和同一0x8c000000/5MiB区间，未在EFI写入/运行固件代码。Linux仅在严格
16字节成功contract和原机型/固件/地址/大小检查通过时使用mdt_load_no_init，保留
原资源时序、auth、video-protect、owner和shutdown/quarantine。EFI匹配完整16KiB
metadata缓冲（由已核验固件生成、尾部零），Linux仍核验完整固件SHA。
实际EFI合同/失败/不重试/发布/幂等及Linux分支通过ASan/UBSan主机测试；匹配模块
完整modpost通过。隔离源码/root/.cache/sq2-venus-preboot-v1及
/root/.cache/sq2-el2-network-20261101/venus-preboot-v1；产物android-el2-venus-v1。
DT递归语义审计仅增加root合同，initramfs有效条目仅venus-core和视频清单；
凭据member最后且相同。正在通过带完整身份/hash检查的安装器部署，尚无实机视频结果。


**2026-11-01 真正EL2完整Android v15已启动，KVM与Wi-Fi同时通过**：按用户加快完整
Android验证的方向，采用v15已通过EL2/KVM的Image/EFI/DT（EFI认证modem，非
v19 EL1延迟认证），配对parity-pack-v4完整载荷及已修复的视频清单。v15 launcher
与v10 efi-source产物hash一致，重新生成Android路径/参数，明确保留sl_install。
DT拒绝EL1控制标记；Android builder移除RAM-only modem proxy实验。只增加360秒
自动恢复和有界终端自动打开助手，后者检查EL2 cmdline/guard/KVM/Android完成/
KVM stage marker及当前用户已有linux/completed，再打开已确认组件
com.android.virtualization.terminal/.new2.ui.MainActivity。不更改镜像或凭据；
启动Activity不是Linux shell成功。实际helper主机测试覆盖作用域/guard/无镜像
拒绝及一次启动，Android调用/计时为stub；shell和GRUB语法、归档有效条目审计
通过。构建时间偏差后重跑已清除，无额外源码变更。
新目录EFI/SQ2EL2Android，菜单备份grub.cfg.before-el2-android-v15-full，现菜单
hash a43d292de384a342cab29f24f0cedae570dcce692a480886d39fe0b7501d71df；正常默认
和EL1控制入口都保留。身份/磁盘/root/固件/hash/gzip检查通过，一次性选择android。
独立terminal-addon插入init及助手，最后凭据member相同，最终hash/gzip检查通过；
已重启，新boot c2d37875-d7fa-4717-af4b-74c37d2a6f69。boot_completed1、guardY、
wlan0及SSH可用，/dev/kvm存在，el2-terminal-stage真实小客体MMIO witness42通过。
早期日志确认全CPU EL2，modem接管后QMI约14秒/HTT约33秒；完整系统网络已通过，
不能继续用精简RAM诊断的结果声称EL2必定没有modem/Wi-Fi。二者具体环境差异尚未
唯一定位。已写/run/el2-control-keep，恢复助手实际输出Recovery canceled；同boot
仍运行，不自动回正常系统。部署initramfs hash86f0f704075f942051e037d3f6b1829ea01a10e02da3d1258d951193836b4c63。

实际VPU加载已到PAS9：固件检查通过，但qcom_mdt_load的SCM初始化返回-22，
shutdown0后内存释放；overlay驻留、无/dev/video0。此boot已尝试PAS，不热卸载或
重试。下一步需补齐PAS9 EFI准备/EL2 Linux接管，保留现有owner/失败隔离规则；
可先研究EFI仅init/mem_setup、Linux在原资源时序下no_init加载并认证的最小路线，
尚未实现/证明EL2 auth9或video-protect可用。不得拿EL1六帧解码替代此项。

用户已确认进入原桌面，系统活动用户10且RUNNING_UNLOCKED。原am启动返回
Activity not found，但相同组件经直接cmd activity start-activity成功；尚未唯一定位
包装/PATH/时序的原因。实际crosvm_debian PID23431/CID2048已启动；屏幕中执行
uname/id/getconf/os-release证明Debian13.7 trixie、aarch64、8核、droid普通用户
shell可交互，DNS解析成功。guest临时文件写读删除通过，/mnt/shared按访问触发
virtiofs挂载；共享目录尚未做跨系统双向写读验收。sudo -n poweroff后旧VM退出，
直接重新打开Terminal又启动PID30726/CID2049，Android boot ID未变。

HTTPS尚未通过：guest的IPv4/默认请求超时，宿主同地址200。Android tethering
创建了DHCP/DNS/NAT/路由，但tetherctrl_FORWARD仅全局计数及DROP，无转发条目；
TC BPF attach返回EINVAL。sch_ingress/cls_bpf已在RAM补载，guest重启后tap端
挂接成功；wlan0启动时未建clsact，手动补建但未把这当作持久修复。另发现
NETFILTER_XT_TARGET_CT、NF_CONNTRACK_FTP/PPTP等缺失；真实iptables-restore
--test警告CT revision0不受支持。新候选仅添加6个内核配置（含自动选择GRE），
已编译成功；正式builder加配置，el2-terminal-stage提前加载两个BPF模块。
待完整Android启动验证系统自身规则和HTTPS，不自行清空或放宽防火墙。

官方ksud（bb231b0f…）和manager-verified.apk（96c2bbbf…）已校验并安装到
/data/adb/ksud及用户10。实际运行hash相同、debug version33129，管理器界面
显示工作中/内置/Kprobes、v3.2.0-legacy33129；模块列表为空。授权及下次启动
生命周期仍待验证，不能从界面状态宣称Next全部通过。
原始证据out/el2-android/android-el2-v15-full/evidence-c2d37875及terminal-*.png、
ksunext.png、tethering-netd.txt，均保留忽略。原v15 ESP尚未替换，恢复已取消。
新候选out/el2-android/android-el2-network-v1，隔离构建/root/.cache/sq2-el2-network-20261101。
终端自动helper已改直接cmd调用，下轮打包包含；完整目标仍需联网、EL2硬解和Next回归。

**2026-11-01 按用户要求提前进入完整Android，EL1 v19桌面/联网及六帧硬解通过**：
暂停新增pre-Linux SMEM解析器，未留下或部署该解析器。以v19已验证modem ready
的Image/327模块/延迟认证EFI为基础，复用完整parity-pack-v3用户态；内核.config
与Module.symvers逐字一致，保留11个定制视频模块、视频用户态/固件及最后凭据
member。DT仅恢复正常EL1 GPU zap-shader状态，其余语义与v19逐项一致；launcher
明确不调用Secure Launch，校验Surface/EL1/defer标记。没有EL2/KVM能力声明。
完整Android init不执行EL2终端gate；只对sq2.el1_preboot=experimental加入有界
启动日志和360秒自动恢复（/run/el1-control-keep存在则取消）。RMTFS保持接管，
不加-s重启modem。恢复脚本作用域/guard/取消/重启fallback主机模拟通过。
打包、自检、shell语法、GRUB语法和菜单原入口不变检查通过；EFI重跑已无clock skew。
设备身份、磁盘/ESP/root、固件及新旧hash/gzip通过，新目录EFI/SQ2EL1Android，
菜单备份grub.cfg.before-el1-android-v19，默认仍Mesa。新菜单SHA256为
570089780b16d0d0f57b67077299e40031238fbf4fab3d22f13d840ff167231e；旧诊断安装
脚本的3e63f6a8菜单guard已过时，勿直接复用。新boot d0efdb3f-51e0-4926-a3eb-f5db9a40a25c，
boot_completed1、guardY、wlan0/SSH可用，用户确认已到桌面/锁屏；日志证明全部CPU
从EL1启动，modem auth0/ready0，约19秒Wi-Fi完成firmware/HTT初始化。已写
/run/el1-control-keep取消此boot的自动恢复，保持当前Android，正常默认不提升。
一次SSH reset后重连boot ID未变，不能记作板级重启。

视频首次启动在模块校验阶段拒绝，尚未进行PAS。发现正式EL2打包器替换11个
视频模块后遗漏更新etc/sq2-video-native/SHA256SUMS；已修复打包器并加入实际
刷新代码的测试（模块更新、helper保持、坏输入/重复/越界路径/缺模块拒绝）。
当前RAM校验15文件与已审计载荷相符后更新清单，首次调用video-decoder-lab-start
成功，/dev/video0出现。随后V4L2真实AVC向量94c7974e…六帧匹配参考CRC，mask3f、
EOS/LAST及关闭通过、exit0；同boot Android仍完成。此为EL1硬件解码证据，
不是EL2或Android MediaCodec各格式的完整回归。KernelSU代码运行有日志，但管理器/
ksud及授权链未验收，不能从日志宣称Next运行时完整通过。
运行证据在android-el1-v19/evidence-d0efdb3f，视频清单修复为video-fix子目录；
已仅向实验initramfs插入855字节addon并保留同一最后凭据member，正常入口不改。
before-video-manifest备份、身份/菜单/旧新hash/gzip及最终有效条目单文件差异均
通过；实际ESP initramfs为0b4e4511dbc47ea26303f2354fc228a87c7654072bbace4486c1a795256982e6，
清单40e60c2e…，未为此重启。当前boot仍d0efdb3f、桌面完成、wlan0/video0、guardY，
恢复helper已输出Recovery canceled。修正后的正式打包器生成parity-pack-v4，
180499854字节/342模块；仅离线，准备供下一轮完整EL2 Android候选使用。
产物out/el2-android/android-el1-v19，生成脚本pack-android-el1-v19.py与
deploy-android-el1-v19.py；当前无/dev/kvm符合EL1预期，终端目标仍未完成。
按用户加快进入Android的方向，下一轮优先用完整Android验证真正EL2候选，
保留本轮已得到的完整系统联网证据；pre-Linux SMEM计划暂缓，不重新扩展诊断。

**2026-11-01 v20 EFI直接64位认证对照完成，modem仍未通信**：基于v19内核/模块和EL1 launcher，
EFI恢复v15完整init/mem/load，只对PAS4且root两个明确EL1/direct标记，使用
Linux形式0x42000205/arginfo1/PAS4直接SMC，保存恢复DAIF、保留QCOM a6续接，
中断返回1最多续接64次。原始a0/a1均0才成功；busy/其他错误不假装成功、不重发
初始认证。PAS矩阵modem auth格现在是原始a0..a3，区别于协议包装的1/0/0/0。
其他PAS仍原协议。DT移除defer标记，Linux只接管，不再次认证。init仅标签变化，
固定Wi-Fi权限保留，modem QMP仍无，驱动不加proxy请求。EFI构建通过；实际C
参数/a6续接/64次上限/错误/原始64位跟踪经ASan/UBSan，实际dispatch12组合通过；
交叉编译指令核对。身份/菜单/新旧hash/gzip通过，before-v20备份、原子替换并
一次性启动。已自动恢复正常boot e2e359b4-1118-46de-a78e-1f88386ff85b，
boot_completed1、guardY。诊断a86966f3-8eef-4ddf-b6b9-0471c0bfe754已取回：
modem auth矩阵transport及原始a0..a3均0，但SMEM435仍0条目、无Wi-Fi QMI
版本、无wlan0。因此换成直接64位SMC并未恢复EFI阶段modem启动；不能把协议
包装或EL2切换单独当作根因。下一步需区分EFI阶段未启动与交接期间状态丢失，
拟增加有界只读的pre-Linux SMEM取证，尚未实现或部署。控制组仍EL1，不代表终端完成。
私有源码/root/.cache/sq2-preboot-v20、脚本prepare-v20.py/pack-v20.py/
test-direct-auth-v20.py/test-dispatch-v20.py，产物preboot-ram-v20。

启动路径说明：正常入口由Linux驱动加载/认证/启动固件；EL2实验入口由EFI预先
加载/认证，Secure Launch切到EL2，再由Linux接管。EL1对照只跳过Secure Launch，
仍使用实验Image/DT/initramfs及EFI预启动/接管，因此“关闭EL2”不等于恢复正常入口。
诊断initramfs只采集并自动返回正常系统，本来就不进入Android桌面。诊断菜单现有
EL2名称仍指向EL1对照载荷，不能用菜单名称判断实际执行级别；以启动日志为准。
正常Mesa/Qualcomm/Rescue入口保持，真正恢复需重启选正常入口，不能只去掉EL2切换。


**2026-11-01 v17 QMP单变量对照已启动**：基于v16仅移除modem节点qcom,qmp
phandle；原Image/initramfs/EFI/launcher逐字不变，完整DT递归审计确认除此
属性外语义不变。qmp_get缺phandle返回ENODEV，q6v5保留NULL，跳过load_state
消息；PD/XO及Linux延迟认证/ready等待保持。身份/菜单/新旧hash通过，before-v17
备份DT/清单并原子替换后一次性启动。监测exec43545，原boot
9da6b639；待新的诊断boot ID，不可误用v16 df1da1e1。私有pack-v17.py及
preboot-ram-v17；本轮仍为EL1对照，不应当作EL2可用候选。
v17已自动恢复2db36ddb-b346-46fc-adcc-6a2b88dc8fc2，boot_completed1/guardY。
诊断6cfef505-3af1-4b3c-a154-c742a078d2f2取回：auth0/ready0，modem通信及
Wi-Fi QMI版本均与v16一致，仍无wlan0。故QMP load_state不是本轮成功必要条件。
曾怀疑qcom,msa-fixed-perm为EL2专用，但v18后检查正常系统也有该属性，
该假设错误。此属性跳过Wi-Fi MSA权限分配；
v18仅移除此Wi-Fi属性、试用Linux动态权限分配流程，其余保持v17；完整DT语义
审计通过。身份/菜单/新旧hash通过，before-v18备份、原子替换、一次性启动完成。
v18已自动恢复b6bdab15-3c1b-4729-8c9c-8accdc336a83，boot_completed1/guardY。
诊断36443ccd-9991-4f85-948a-1f1d652c6eef：modem auth0/ready0，但MSA权限
分配-22，无法到达QMI版本读取；正常系统确认同样带fixed-perm，故该属性须保留。
v19以v17内容为对照，只取消独立EL1模块的proxy PD/XO等资源请求，并移除
失败路径无资源可释放的handover调用。q6v5_prepare/认证/ready仍保留，QMP仍无。
实际C的顺序/失败清理测试、模块完整modpost及cpio单模块差异审计通过。
v19已从v18恢复v17 DT并部署单模块诊断：身份/菜单/新旧hash/gzip通过，
before-v19备份后原子替换并一次性启动；等待自动恢复，原boot b6bdab15。
v19已自动恢复6dc57cc9-812d-42a3-b769-85c516df2791，boot_completed1/guardY。
诊断37a6de43-e8fc-4d8f-a3c3-1c30085d5c0a：无proxy votes，auth0/ready0，
modem通信及Wi-Fi QMI版本正常，仍无wlan0。故Linux新增的PD/XO请求和QMP消息
并非此控制组成功的必要条件，不能再将缺少它们当作已确认根因。
下一步隔离EFI协议包装与Linux SMC调用方式；仍保留启动时机/其他Linux资源差异。
正常默认不改。私有prepare-v19.py/pack-v19.py/test-attach-v19.py；隔离EL1
源树当前为v19无proxy版本，v16原源备份q6v5-before-v19.c，勿误用为生产模块。



**2026-11-01 v16 延迟modem认证EL1对照**：v15的正常系统boot仍为baba9ea7，
guardY、boot_completed1；一次SSH超时后重试恢复。隔离EL1源树增加严格限定
Surface/SC8180X modem、两个EL1 root标记及proxy-test属性的延迟认证路径。
EFI只对PAS4做init/mem/load，完成状态写2（prepared，非运行成功）；Linux先
q6v5_prepare及原PD/XO/aggre2/regulator votes，再auth4、最多等待ready5秒。
其他DSP保持状态1；成功票据只保留到本轮诊断重启，失败释放，不重复reset。
当前只是资源时序控制组，不是EL2最终方案，不宣称网络或Terminal已修复。
EFI/Image/modules编译通过；实际publisher状态2/缺标记/错DSP/重入/64位trace
及实际attach谓词/调用顺序/失败清理经ASan/UBSan通过。新Image的QEMU只读
screen_only与freeze/mem/disk拒绝测试通过。327模块配对打包、其他载荷审计。
私有源码/root/.cache/sq2-preboot-v16及sq2-el1-preboot-control，脚本
out/el2-android/prepare-v16.py、pack-v16.py、test-publish-v16.py、
test-attach-v16.py、smoke-v16.py；产物preboot-ram-v16。准备脚本只用于从
v15 EFI和v14内核源重新生成，不可在已改的源树上重复运行。
身份/菜单/新旧hash/gzip检查通过，before-v16备份后原子替换并一次性启动。
v16已自动恢复9da6b639-79ec-4996-aa16-314bf04ffa3d，boot_completed1/guardY，
正常wlan0/video0存在。新诊断df1da1e1-11d5-40e5-af1a-15714ba34d3c日志取回：
modem auth=0、ready=0；SMEM435有效条目3/slave-kernel状态6；modem IPCRTR已
绑定，ath10k读到QMI芯片/固件版本。仍无wlan0，不能称Wi-Fi通过。EFI PAS4
trace的auth格未调用；EL1无KVM符合预期。首次证明EFI加载的modem可由Linux
延迟认证后进入运行/通信状态；尚不能把改进唯一归因PD/XO/QMP或特定时序。
下一步用同一v16内核/EFI，仅撤掉modem qcom,qmp属性作load_state对照，
其余预认证资源准备保持；正常默认/硬解/KSU及凭据保持。


**2026-11-01 v15完整PAS返回取证已发起**：主机按实际三固件解析，EFI与Linux
认证metadata字节一致，所有PT_LOAD重定位基准一致，页对齐span与DT保留区
均一致（MPSS0x9600000、ADSP0x1a00000、CDSP0x800000）；未发现参数差异。
v15恢复v12的EL2 Image/327模块/launcher与DT（撤掉EL1控制许可），只追加
EFI端三PAS×四调用×五UINT64矩阵（transport+4返回），未调用格全FF。最终DT
根属性480字节，Linux复制到日志pas-trace.bin，保留同一SMEM快照。实际C的
12格、64位保留、非法ID/op边界经ASan/UBSan通过，EFI编译通过，打包逐项审计。
身份/菜单/新旧hash/gzip通过，before-v15备份并原子替换，已一次性启动与重启。
恢复监测exec38347运行中，原boot14b967cc；待新日志判定初始化/mem_setup返回，
不能只凭最后auth成功认定完整调用成功。私有准备/打包脚本prepare-v15.py、
pack-v15.py，源码/root/.cache/sq2-preboot-v15，产物preboot-ram-v15。
v15已自动恢复baba9ea7-e99a-4976-99a7-f0c5a43c3021，guardY。诊断cc4d7077
日志已取回：三PAS的init/mem/auth全部transport0、结果1/0/0/0；stop格全部FF，
表示未调用。KVM PASS，Wi-Fi仍无。ESP与正常Linux的三固件SHA256逐一匹配。
因此前序返回失败与设备固件错配均未得到支持；下一步检查认证前资源时序与
执行状态，不再用仅看最终auth、重复proxy GUID或EL2切换作为根因结论。


**2026-11-01 v13无proxy接口，v14 EL1预启动对照已发起**：v13自动恢复原系统
296e693d，guardY；诊断8684568b日志取回，proxy EFI状态800000000000000e
（NOT_FOUND），未执行Vote；Wi-Fi仍缺失，KVM PASS。不能假设SC8180X有该接口。
完整Android parity-pack-v3已构建180499826字节、342模块，视频用户态/固件保持，
末尾凭据保持，包含el2-rmtfs-start；仅离线，实际VPU/终端未验证。
v14隔离/root/.cache/sq2-el1-preboot-control源树/构建；仅3处接管EL2判断增加
Surface专用DT root microsoft,el1-preboot-control许可，原EL2树/正常默认不变，
配置逐字一致。启动器不调用sl_install，仍加载qebspil，故保留EL1而运行同一
预启动/接管。这是区分EFI初始化缺失与EL2切换影响的控制组，不是终端方案。
Image/modules构建通过，时钟偏移后重跑无偏移警告；实际Image QEMU的只读guard
及freeze/mem/disk拒绝测试通过。327配套模块逐项打包，其余只改init标签，沿用
v12只读SMEM快照及EFI（不含v13无效proxy试验）。身份/菜单/新旧hash/gzip通过，
before-v14备份后原子替换，已一次性启动并Android重启。等待新诊断结果；本轮
无KVM是预期控制条件。私有脚本prepare-el1-control.py、pack-v14.py及产物
preboot-ram-v14。原系统默认不变，终端目标未完成。
v14已自动恢复14b967cc-720a-43ed-86e8-9b604d476bbc，guardY。诊断c761712f
日志确认全部CPU从EL1启动、无/dev/kvm（预期），三DSP attached，modem435
仍有效条目0、SMEM421缺失、Wi-Fi缺失；ADSP/CDSP保持正常条目。故此失败
在不执行Secure Launch/EL2切换时也存在，应定位EFI modem预启动与正常Linux
初始化的差异，不能继续归咎EL2切换。下一轮需恢复EL2诊断配对后再验最终功能；
当前诊断菜单的EL2名称实际引用EL1控制组，不能把它当作正常EL2候选使用。


**2026-11-01 v12结果与v13预启动proxy对照**：v12自动恢复至b056efde，guardY。
诊断495f8047日志已取回：SMEM421不存在，modem入站435头部有效但条目0，
AP出站428有master-kernel/ipa；ADSP/CDSP各有smp2p/slave-kernel且状态6。
正常对照modem入站有3条，状态6，正常DT的SMEM编号与诊断一致；故不是
编号交换错误。没有证据将残留SFR文字、无中断或PAS返回独自等同为崩溃。
v13私有候选基于v11 EFI，仅在PAS4 prepare时发现PIL_PROXY_MODEM_GUID，
校验Revision0x10000及函数指针后申请原固件Vote(1)，缺失/不符/失败如实记录，
不伪造成功。返回EFI状态8字节经最终DT根属性传入Linux的modem-proxy-status.txt；
后续保留同一SMEM快照。实际C六个发现/投票模拟通过，EFI构建通过。
此GUID来自固定qebspil头文件，尚未证明SC8180X提供它；这是实验而非正式适配。
仅诊断EFI/DTB/initramfs更改，模块/内核保持；身份/菜单/旧新hash/gzip通过，
before-v13备份和sync原子替换后一次性启动，Android重启已发起。监测exec
session49012正在运行，下一步取本轮新日志，勿按ESP mtime排序猜测。
源码候选/root/.cache/sq2-preboot-v13，私有生成/打包脚本out/el2-android/
prepare-v13.py、pack-v13.py，产物preboot-ram-v13。正常默认保持，终端未完成。


**2026-11-01 v12 只读SMEM对照已发起**：新增独立诊断模块sq2-smem-snapshot，
仅Surface兼容设备、读取固定7项SMEM，每项最多128字节，无分配/写入/重置。
实际C主机模拟覆盖非目标拒绝、缺项、零长/短/长边界；匹配内核完整modpost通过。
正常设备符号导出、vermagic、无MODVERSIONS及模块hash核对后加载/取回/卸载通过，
原始快照在out/el2-android/smem。正常SMEM421也含SFR Init wdog/kernel error
文字，不能将其独自判为EL2崩溃证据。正常modem IPCRTR和GLINK中断活跃，
而v11 modem中断0；EFI启动前没有proxy投票，Linux接管后补投票曾无效，
尚未验证是否是启动时资源时序。
v12仅在v11诊断initramfs加入该模块和一次加载/日志/卸载，逐项差异审计通过，
内核/EFI/DTB保持v11。身份/菜单/旧新hash及gzip核对通过，before-v12备份后
sync原子替换，一次性启动并Android重启已发起。等待自动恢复和smem-snapshot.txt
对照；恢复监测exec session26059正在运行，不能因观察超时重复启动。
原boot63af0d16，正常默认/硬解/KSU载荷不变；终端目标未完成。


**2026-11-01 v11 PAS 返回取证已发起一次性启动**：四个 PAS 包装函数添加
transport/raw 返回日志与未写入哨兵；认证启动最后返回的前三字低32位写入既有
DT status 字段（阶段11），随已有最终DT发布/ESP日志持久化。仍未确认 PAS ABI，
不能把完成位当作固件服务成功。实际生成的C包装函数模拟传输失败/非零固件
返回/完整载荷保留/未写入哨兵检查通过，EFI交叉编译通过；跟踪生成检查通过。
v11沿用v10 Image/initramfs/launcher，仅更新EFI与移除modem proxy-test的DTB。
设备原boot d0c85809、guardY、NVMe序列/ESP marker/UUID/root来源、菜单和原新
文件hash已核对；诊断目录before-v11保留旧两文件/清单，sync原子替换后一次性
启动已设置。通过PID1 mount namespace的Android sys.powerctl发起重启。
正常默认与硬解/KSU载荷未改；正在等待自动恢复和v11日志，终端未验收。
产物/部署位于忽略out/el2-android/preboot-ram-v11及pas-response-v11。
v11现已自动恢复原系统boot63af0d16-566b-40ee-bfdb-6c1e3d72d86c；boot_completed1、
wlan0、video0、guardY已读回。新诊断cbfd518f-8697-4988-8a2d-77a7ed448903日志
已取回：三PAS认证启动的低32位返回都是1/0/0，KVM guest PASS，Wi-Fi仍缺失，
modem仍只有rpmsg_ctrl而无IPCRTR。不能把PAS返回判定修正称为网络修复。
ESP目录mtime不可靠；本轮早先按排序取到808f旧日志，不得用它代表v11。
下一步定位modem运行/GLINK服务及EL2切换影响，再接Android完整终端验收。


**2026-11-01 当前工作快照补充**：v10 已自动恢复原系统，诊断日志确认 modem
proxy 资源请求成功，但仍无 modem IPCRTR/Wi-Fi，KVM witness 仍 PASS；该实验
没有修复网络。RAM 构建器目前仍保留这一临时测试，下一轮应撤掉；Android
预启动构建器会剔除其 DT 属性。新增 el2-rmtfs-start 避免对已 attached 的
modem 再执行 rmtfs -s，主机分支测试通过，尚未重新打包或部署此 helper。
进一步检查发现 qebspil 的 PAS 封装仅返回 EFI transport status，未校验 SCM
Results；因此此前 PAS Success/完成位不能单独证明固件服务成功。返回值
校验修复尚未实施。Wi-Fi、EL2 下实际硬解、KSU 和 Linux Terminal 的完整
运行验收仍未完成；本次提交保存当前实验状态，不代表功能已交付。

**2026-11-01 EL2/终端推进及用户要求清理 ESP**：详见
`docs/SQ1-EL2-PREPARATION.md` 末节。原默认 Mesa、Qualcomm、Rescue 的
Image/DTB/有效 initramfs addon 均保持原字节；用户明确要求完整保留硬解和
KSU Next，不能用桌面或 `/dev/kvm` 代替终端与功能一致性验收。
首次 Android EL2 的 Wi-Fi 失败有 PAS/RMTFS -22 日志，旧候选已撤出菜单。
当前完成 EFI 预启动 + Linux 接管的实验构建，KSU/media 模块匹配构建通过；
硬解 VPU 在 EL2 下的 PAS 生命周期仍待解决。新的 RAM 诊断通过一次性 GRUB
自动选择并恢复默认；v1 遗漏 lsm=selinux，QEMU 复现 KSU 启动崩溃，v2 已补齐
原系统参数，实机八核/NVMe/KVM guest 通过，但预启动完成位全0，Wi-Fi未恢复。
v3照片定位EFI误判：Qualcomm的tz_common_rsp_t首项1表示完成、次项0表示成功，
不能按Linux qcom_scm_res首项0判断。r5修正为严格检查完成1+ret0，六组返回值
测试及EFI构建通过；只替换诊断EFI为ce407362…，原默认不变。v4照片确认RMTFS
Success，随后打印到ADSP Starting并停住，尚不能区分ADSP调用或后续EL2切换。
v5逐个PAS调用返回日志+EBS之后直接帧缓冲阶段码已构建。用户恢复原系统后，
boot6c0e84de下身份/原菜单/哈希校验通过，已替换两个诊断EFI并发起一次性重启，待结果。
v5用户反馈all PAS Success、阶段31数值2，确认EL2返回后停住；原系统恢复到
62ff95c5，未留下v5 Linux日志。v6仅改启动器，在late EBS回调后、SL_AUTH前
再清理原允许类型的内存缓存，检验交接状态丢失假设；已部署并发起一次性测试，待结果。
v6仍停31/2，缓存补清理未解决。用户恢复53c9d620后部署v7：Linux EFI stub
增加40/41/42/50阶段码，诊断命令行开启early info/initcall日志；仅诊断Image与
launcher变更，原默认及模块/固件保持不变。已发起一次性重启，等待v7实机结果。
普通initramfs reboot未改变板级boot ID，随后用Android sys.powerctl reboot发起。
v7用户反馈41 F81F100，停点位于post-EBS结果复制函数内，具体故障指令未定位。
v8移除该post-EBS解析：EFI stub在EBS前提供最终DT地址，qebspil在EL1完成
PAS后校验四组真实完成位并直接写最终DT、清理缓存。主机真实DT复制/拒绝失败/
重复回调及ASan、UBSan通过；内核及EFI编译通过。用户恢复707c599b后身份、
菜单和旧新hash通过，已仅替换诊断Image/DTB/qebspil并发起一次性自动重启。
等待v8 kernel DT handoff、42/50或Linux/Wi-Fi/KVM结果；不能据此称已修复。
v8日志已取回：27e329d1下八核/NVMe/RMTFS/KVM guest MMIO42/EXIT0，三DSP
attached及四完成位1。Wi-Fi仍无接口；pd-mapper报告no pd maps available。
确认remoteproc sysfs对ATTACHED统一输出unknown，pd-mapper由固件路径找jsn。
v9仅对Surface/EL2/三个指定PAS/显式完成1且名称匹配DT的情况显示真实firmware；
其它外部固件仍unknown。增加映射/QRTR/进程日志并已一次性启动，等待自动恢复。
原系统恢复boot955804b6，原video0存在。完整Android候选打包器现保留全部11个
原生视频模块的匹配重编版本、视频用户态/固件和末尾凭据；342模块包生成通过，
out/el2-android/parity-pack-v1仅构建未部署，VPU EL2生命周期及终端仍未验证。
v9自动恢复4938b520，a88ad43e诊断日志确认三固件名正确、pd-mapper驻留且不再
报告缺映射；Wi-Fi仍无。ADSP/CDSP有IPCRTR及GLINK中断，modem无IPCRTR、
GLINK irq481计数0，KVM仍PASS。v10仅对RAM DT modem节点启用proxy资源对照：
沿用正常adsp_start的PD/XO/aggre2/regulator请求，失败回收已取得的请求，不重置/
重载固件；成功仅在此次诊断至重启期间保留。加入SMEM421打印。构建复查无时钟
偏移警告且包内全部模块逐项匹配，已部署并一次性启动，等待自动恢复日志。
Android预启动打包器已完成，android-preboot-v1是与v9匹配的构建快照、未部署；
后续构建器会剔除modem临时proxy-test属性。不得将临时资源保持当作正式电源管理。
不能先称Wi-Fi修复或Linux终端可用。

清理前完整 ESP 已备份到忽略目录 `out/esp-cleanup-20261101/esp-before.tar`，
SHA256 `7d4570756ba652c221be3285d13714243fac72dc2fdeae2535788957b09deedc`。
旧候选/历史日志/旧菜单与重复备份移出设备；ESP 使用率21%，余约1.6GiB。
保留上述三个启动项、四个 EL2 诊断入口及关机；Mesa 仍为默认。
SQ2Buttons 的旧菜单改为转入当前主菜单，避免引用已退休文件。
主菜单 SHA256 `3e63f6a8829178d91742532f08cfa2eb1cb9b8781992cb4f19accddeeb382524`。
后文历史 ESP 路径可能已退休，不应直接按旧部署步骤复用。

**2026-10-31 首次校准加入可选拇指优化并部署**：用户明确要求在首次启动
测试中发现问题后才启用。默认配置SizeMax=2.0；42点校准+5点检查后加入边缘
单拇指持续慢拖3×5秒，最后轻点/拖动/双指人工确认。提前UP仅提示“提前抬起或
意外断触”，不自动归因或开补丁；用户可重测或选择2.4cm，启用后必须重测再保存。
另有“仅检查拇指”入口，沿用原坐标，不必重做42点。跳过保留原选项，取消/超时
恢复原结果；已启用仍异常时不继续扩大尺寸。ThumbCheck纯Java负责手势状态。

profile增加可选ContactSizeMax，仅允许2.0/2.4；旧7字段结果默认2.0。新neutral
显式2.0，root校验后追加Contacts段。新保存同时绑定设备与坐标；改变尺寸不改变
坐标映射、检测阈值、AspectMax、连续性或热图补丁。实际shell预览/取消/超时/
保存重启、Java手势与坐标不变性及native非法值测试均通过，共10项相关测试。
APK安装包含dex编译，实机一次超过旧15秒导致helper退出；安装等待单独放宽到
120秒，普通Android查询仍15秒，且未保存试用的180秒恢复机制不变。

本机先前2.4cm已获用户实测确认，因此将b20281a7…的坐标完整保留，只追加
ContactSizeMax=2.4，迁移profile为59f305f0…，旧结果另备份；当前有效配置2.4，
通用base配置2.0。完整构建6581cd0e…，触控新构建ee383f30…，但当前运行
iptsd仍40a5f0fd…，本轮未替换运行二进制或重启。仅部署4文件（config、APK、
profile工具、helper），addon da735bdd…，最后凭据member完整保留。默认构建
含另一agent同期4KB相关文件，本轮不部署这些文件。两基线入口追加
touch-firstboot-thumb-v1.cpio.gz，Mesa默认/回退不变；菜单2cce07b4…，备份
grub.cfg.before-touch-firstboot-thumb-v1。硬件盘/ESP/data/root身份、gzip/hash、
原子替换均通过。helper安装Success、当前context/profile及有效2.4配置已核对；
实机界面检查了欢迎页→仅检查拇指→已启用说明→模拟提前抬起提示→返回取消，
没有保存模拟结果。此为注入事件的界面检查，不能替代真人触控回归。
启动自动弹出、新增向导的真人完整流程及本轮重启尚未验证。
产物/审计在忽略out/touch-thumb-20261031/firstboot-{full-v2,deploy}。

**2026-10-31 拇指边缘快速重复点击：一次尺寸判定中断已定位，2.4cm实测通过**：
用户最初称两个触点，首次单进程采集后澄清“按了很多次，最边缘大拇指会有
快速临近的两次点击，只有一个触点”。本轮是自由复现，不能按原计划8手势
标注，更没有已确认的双指对照。原始iptsd_045E_0C21_1793440444.bin共2387帧，
最大raw contact数和实际MT槽均1；53次按下、22个小于250ms的UP→DOWN间隔。
多数间隔raw contact消失、热图峰下降，尚不能区分用户抬手/出界和传感器丢失。
明确一处f2071–2079在同一连续热图触点中size超2cm（最大2.356cm），valid变0，
随后恢复形成约100ms的重复按下；aspect未超2，因此不是两个接触簇或校准复制。

离线对照SizeMax2.4与2.5结果相同：恢复47个上报帧，53次按下变52次；不应称
已解决所有重复点击。旧13562帧4份录制完全不新增上报，含历史空闲3610帧0触点。
阈值40/36、AspectMax2、连续性、即时起落、四点热图及已保存校准不变；没有
增加释放延时/防抖。随后用户确认临时2.4cm“慢拖正常，轻点和双指也正常”。
试验录制1809帧，22次按下，无小于250ms的UP→DOWN、无无效接触；双指区间
230帧原始双指、223帧上报双指。实际手势不完全对应原计划次数/时长，不扩大结论。
原服务40a5f0fd…已恢复，show_touches恢复null、测试APP关闭。先安装config-only
touch-size24-v1（29278e88…），随后按用户新要求改成上节按设备保存的可选项。

忽略目录out/touch-thumb-20261031保存录制、analysis/gaps/size-comparison。
session-size24.sh准备/tmp/touch-thumb-size24，临时单进程诊断用现有eaabb2f7…
binary，只追加Contacts SizeMax=2.4，最多1800秒/stop后wait回收并恢复/bin/iptsd。
测试已结束，当前没有临时采集或第二个HID读者。4KB工作由另一agent推进，
不自行重启设备或覆盖其文件。

**2026-10-31 首次启动校准已安装，用户确认检查通过、拖动正常、已保存**：用户要求按
不同屏幕在首次启动校准；通用默认profile已改为恒等，不再分发本机v2数值。
新增probe/touch-calibration（APK、Java拟合/逆变换、native严格profile工具）、
touch-calibration-boot、构建集成和两份测试。现有连续性/热图/阈值不变。
向导42次（3组×7点×2次）+5点检查+拖动确认保存，已有补偿先逆变换，避免
重复叠加。保存前仅临时应用，取消/180秒超时恢复；完整文件按硬件摘要绑定。
新设备可跳过，之后在应用列表打开“触屏校准”。Android未完成启动或原触控
初始化未结束时等待；不读取第二个hidraw、不改锁屏/网络、不持续采集输入。

Java拟合/逆变换及非法profile、native校验/设备隔离/安全发布、实际shell沙箱
首启/保存/再次启动/超时/跳过测试通过；默认触控及完整包d9d2b1fd…构建通过。
Meson偶发WSL zip时间戳未来1–2秒，构建器仅针对Clock skew detected显式重试。
实际addon6f75ff8f…与完整包有效条目逐项一致，仅8个预期触控/启动条目变化，
凭据member仍最后。仅两基线入口追加touch-firstboot-v1.cpio.gz，原可用及
救援入口保留；新菜单da8e624c…，原菜单备份grub.cfg.before-touch-firstboot-v1。

已将用户确认的v2参数迁移到/data/vendor/sq2-touch-calibration/<identity>.profile，
本机profile b4a3e2e4…（摘要在忽略out/touch-calibration/deploy/audit.json）；
运行态helper已安装/启动，当前iptsd仍40a5f0fd…，合成配置ea6584a7…保持原
曲线，APK3ba2215f…已安装到user10。正式新包iptsd为eaf7c38e…，下一次启动
才加载；本轮未重启，以免打断另一agent的4KB告警排查。不要重复修改4KB通知。
boot仍a384f5c1…、Mesa、guardY。向导欢迎页截图已检查，界面完整、context正常；
用户已选择“现在完整测试”。随后实机收到preview和save并返回OK，profile
更新为b20281a7…，合成配置001f5045…；两者所有校准字段逐项一致。iptsd
PID28796仍正常，日志确认加载/run/iptsd-calibrated.conf，无待处理请求，boot
未变。用户随后明确反馈“检查通过，拖动正常，已保存”，本轮完整向导实机
操作与保存验证完成；尚未做重启后自动加载及新设备首次自动弹出实测。原v2参数
保留在忽略deploy/migrate.profile及设备/tmp/touch-firstboot/migrate.profile。
若反馈异常，先恢复原v2，不让用户反复重做42点。代码与产物未提交Git。
部署、测量和截图位于忽略out/touch-calibration；尚无本轮重启后的自动加载实测。

**2026-10-31 校准v2已持久部署，Mesa重启后用户确认正常**：用户反馈
“左侧明显改善，其他位置和拖动正常”。将已测试v2曲线提升到默认配置，
--touch-calibration默认on，连续性+四点热图仍默认on。默认触控交叉编译通过，
iptsd40a5f0fd…、配置9943809d…；完整默认构建4f223373…，所有有效条目与
现机基线+蓝牙v2+校准v2组合完全相同。新addon eacf319c…仅覆盖bin/iptsd和
etc/iptsd.conf，私有SSH凭据member仍最后。原标准构建首次遇WSL时钟偏移，
重试通过；整包构建须显式--edp-edid out/dual-boot-65-700/input/sq2-native65.bin
（原默认路径缺文件），显示实际默认仍2.7-60，未启用65Hz。

核对NVMe序列/容量、ESP marker、ESP/data UUID及root loop来源后，gzip/hash、
sync及原子替换已通过。仅两基线入口追加touch-calibration-v2.cpio.gz，
Mesa仍默认；原可用/救援入口和文件均保留。菜单备份
/media/esp/EFI/BOOT/grub.cfg.before-touch-calibration-v2（28806fcb…）；新菜单
9df50955…，addon位于/boot/sq2-baseline60-20261031。临时诊断已结束并确认
原进程2210ac0a…恢复、show_touches恢复null；raw证据已取回忽略目录。
正常重启后boot为a384f5c1-da7e-479b-b562-6412e52ba67f，Mesa/2.7-60、
boot_completed1、guardY。实际/bin/iptsd PID6674及配置hash分别40a5f0fd…、
9943809d…，均匹配构建；show_touches为null，无临时采集进程。蓝牙桥接
65bef26e…、helper6bbe379a…保持，TRANSPORT_READY、保存开关1、crash0；
本轮没有另做蓝牙输入测量。用户随后确认“桌面、边缘点击和拖动均正常”。
此次只在Mesa完成重启实测；高通入口引用相同触控addon，但未另重启测试。
持续空闲误触/长时间精度与其它设备不据此保证；原幽灵修复仍保留。
部署证据out/touch-edge-20261031/deploy/after-reboot.txt及audit.json；完整包
与addon逐项比对full-build-comparison.json。当前默认v2及设备部署已完成。

**2026-10-31 左边校准v2临时测试，等待实际按钮反馈**：用户确认v1右/下
“明显改善，中心拖动也正常”，随后报告左边同样偏移。12次左边测量已完成，
忽略目录calibration-left.csv；第0点明显滑动（X9→263px），排除。左中部
1%位置X约9、目标28.8px；3%位置X41–67、目标86.4px；8%位置基本正常。
左上角X平均83、目标86.4，主要Y偏高约19px，不能整条左边统一平移。

v2增加左侧X单调曲线，并用CalibrationXTop及CalibrationTopStart/End在
Y10%到30%平滑混合，避免左中部修正误推左上角。Y顶部补偿在12%内渐退，
中心恒等，右/下沿用v1曲线（配置改为6位小数，误差小于0.01px）；仅输出
坐标变换，不改检测/连续性/热图修复/阈值。INI行长须小于200字节；首次v2
9位小数配置因行过长被解析器拒绝，已缩短并重新通过真实加载及回放检查。
曲线/非法配置、实际TouchDevice ARM/QEMU单多点与混合区域测试通过；
13562帧回放检测和稳定性完全相同，历史空闲3610帧零触点。训练数据左1%
横向残差约1px、左3%约8–14px，不能代替独立实机验证。

当前已确认SESSION_READY，运行/tmp/touch-calibration-v2/session.sh，最多1800秒或stop恢复
原版；二进制eaabb2f7…、profile3901b0b2…，普通候选02e0ae96…。临时显示
触点圆点，退出恢复原null。v1退出发现子进程未wait导致touch-services误判
旧PID仍活而没有重启原服务；已手动恢复并核对原2210ac0a…，v2脚本已补
终止、wait回收及恢复exe路径校验。不要仅凭ORIGINAL_TOUCH_RESTORED字样判定。
实际PID22372、exe哈希及user10 show_touches=1已核对；已发实际按钮反馈问题。
原/bin/iptsd、配置、EFI未改；Mesa默认及所有回退保留，无板级重启。

默认构建器已接入--touch-calibration on|off（默认on），与ghost模式独立；
当前跟踪profile仍是用户验证的v1，v2仅忽略目录候选，待反馈后再提升。
默认v1触控构建曾通过，后续v2源码更新使其header manifest过期，须重编；
尚未持久部署校准。out/touch-edge-20261031/prepare-deploy.py及deploy中v1
准备产物未上传，不能直接用作v2部署。下一步收实际左/左上按钮、跨区拖动
及右/下回归反馈，再定稿、重编和审计部署，凭据member须最后。

**2026-10-31 用户要求改走校准补偿，v1临时加载等待真实按钮反馈**：用户最新
“通过校准补偿的方式解决”；同时澄清印象中Windows没有偏移，但目前没有可用
同条件Windows对照，不能称硬件故障。原生单点6次对照已完成并恢复原iptsd：
底97%Y偏低36.0/28.2/22.6px，底99%约-2.4px；原生模式不是多点替代方案。
其余side report中0x12有稀疏坐标样字段，但没有底97%对应数据，不据此替换算法。

新增scripts/patch-iptsd-sq2-calibration.py及probe/test2/sq2-touch-calibration.hpp：
配置[Touchscreen] CalibrationX/Y，归一化input:output分段线性单调曲线，空值
恒等、0/1端点固定、斜率0.2..6；仅045E:0C21且metadata46x68触屏生效。
只在TouchDevice发出ST/MT坐标时应用，检测/稳定性/接触连续性/热图修复/触笔
不变。这是校准路径，不是之前未部署的centroid/bounded-edge拟合实验。
v1 profile在忽略目录out/touch-edge-20261031/calibration-v1.conf，由重复测量
拟合；X<=.96、Y<=.94恒等，当前只校右/下，左/上无足够重复标定数据。
底97/99原始坐标重叠，合并校准点作折中：训练数据97%误差约49降20px，99%
约10变-19px；不能称独立验证通过或完全恢复丢失坐标差别。

真实TouchDevice ARM/QEMU录制sink测试通过：单/多点坐标一致、事件序列/即刻
起落/连续性及设备范围不变。曲线解析/单调/非法配置测试通过；13562帧4份录制
回放检测与稳定性逐项不变，含历史空闲3610帧零触点。普通构建4c196a16…、
临时诊断720c5266…、profile3810e28d…；尚未改标准构建器或设备持久启动载荷。
现在/tmp/touch-calibration-v1/session.sh启动临时校准服务，最多1800秒或stop
标记恢复原/bin/iptsd；临时开启user10 show_touches便于看圆点，退出恢复原null。
准备核对SESSION_READY后请用户试原来右/下小按钮；不再要求整轮靶点。
结束时须读取真实反馈、核对恢复/决定持久化；当前默认Mesa及原可用回退未改。

**2026-10-31 原生单点坐标对照准备启动，用户已准备好**：最外沿7点文件已
自动完成并恢复原版；第0中心发生于诊断开始前，排除，第1–6点有同步raw。
底99%原版误差约+9px，centroid候选变为-23至-30px，故不采用/不部署该候选。
右99%原版X约2870（靶点2851.2）。证据touch-edge-limit，raw1793432958.bin。
原始HID录制发现0x40/6字节报告：bit0接触、LE16 X/Y、范围0..32767，描述符
与event7 BTN_TOUCH/ABS_X/Y一致；元数据transform对角408.791/405.756，平移0，
与原(cols-1,rows-1)归一化相符，无证据支持全屏仿射标定错误。0x40在进入raw
模式前出现，raw模式中未伴随输出，不能声称可同时直接替代多点位置。

用户同意并准备原生单点对照。忽略目录edge-native.cpp临时程序读取同一hidraw，
报告5设0，解析0x40并通过现有虚拟触屏发单点DOWN/UP；临时EVIOCGRAB event7
防原始节点重复响应，验证节点名及X/Y范围，退出自动释放。Runner仅在独立缓存
中放行0x40；native解码真实13报告（含release）逐项对照通过，AArch64二进制
1b3e337f…，不是默认iptsd替换。脚本/tmp/touch-native-control/session.sh，最多
1800秒，APP points-native-control.csv为底97%3次+底99%3次，完成6点即自动
恢复原多点服务。原bin/config/EFI未改，screen_only仍Y。下一步核对SESSION_READY
再提示用户开始；此对照没有热图和正常多点语义，绝不能提升为默认功能。

**2026-10-31 同步采集已成功；边界候选仅离线，最外沿7点采集待完成**：用户
在window30采集期间完成剩余8点，时间戳与190帧热图、getevent对应；完成后
自动恢复/bin/iptsd hash2210ac0a…、配置c7f31ec9…并核验，APP已先关闭。
确认无空闲FRAME不代表失败，此传感器本次从真实点击开始输出；不要再只凭
READY后无连续空闲帧判定采集故障。证据out/touch-edge-20261031/touch-edge-window30，
有效raw为iptsd_045E_0C21_1793432315.bin（680356字节），较早1076字节文件无效。
points.csv第0点发生于原版，排除；第1至8点有同步热图。底97%三次输入热图
第44/45行逐列相同（抽查10帧均68/68），原拟合y约44.8、报告屏幕Y1910附近。
Android坐标与该拟合一致，尚不能将重复数据的来源断言为固件或硬件故障。

离线zero-border卷积候选仍偏低30–34px且减少valid/stable帧，未采用。
离线centroid候选仅对46x68、触点边界到最后行、末行强度达原activation且
末两行完全相同的情况，用去掉重复末行的原neutral图局部质量中心替代Y；
不改X/形状/阈值。该录制底97%三次偏差由约48px降至3.65/9.42/9.52px，
有效稳定帧14/12/9不变；两中心、三下92%点全部不变。但该方法可能导致真正
最外沿不可达，尚不能部署；当前代码只是忽略目录的离线假设，也还没加产品ID
范围保护，绝不能直接进默认构建。现在准备/tmp/touch-edge-limit原算法录制：
7点（中心1、底99%3、右99%3），APP文件points-edge-limit.csv，1800秒或完成
7点自动恢复。用于评估候选边缘可达性及右边是否同类，未部署坐标修正。

**2026-10-31 中心一次发生于5分钟窗口结束后，采集等待延长**：用户报告从
1/9变2/9，但down_time_ms=5600854，前一诊断在约5202秒已结束并恢复原服务；
因此仍没有该点击的同步热图，不能据此称采集可用或触控失败。与用户说明窗口
错开，保留APP当前2/9进度，启用/tmp/touch-edge-window30：原算法和配置不变，
最多等待1800秒，APP累计9点完成即复制points.csv并自动恢复原服务。ready-uptime
和points-before.csv区分原版首点与后续诊断期间的数据；后续只做剩余8点，
不要重新索要整轮。真实点击期间能否出热图仍待验证；若不响应，touch目录stop
立即恢复。show_touches为原null，原默认与启动载荷未改；不需设备重启。

**2026-10-31 离线排除四点热图补丁直接改变已录底缘触点**：同一份有效四边
原始录制分别用当前算法与仅移除repair_sq2_heatmap调用的本机构建回放，按帧
匹配底缘触点（忽略新增的上角噪声触点和tracking ID），31个底缘有效帧的
坐标、大小、aspect、stable/valid逐项完全相同。noquirk-bottom-comparison.json
记录31/31；不代表其它触点或全部录制相同，也不证明其它算法步骤无问题。
无补丁回放重新出现额外上角触点，未向设备部署或关闭幽灵触控修复。

**2026-10-31 同步采集入口正在等待一次中心点击验证**：补充初始化后延迟3秒
的诊断程序仍仅READY、无空闲FRAME，自动恢复原服务；但这一观察本身不能证明
实际点击也无输出，须排除空闲停流。已告知用户，并启动一次明确的中心点击
验证：/tmp/touch-edge-live-check，原算法/配置不变，仅单进程录制，先只点一次
看APP从1/9变2/9，已发异步问题，尚待回答。最多300秒或points-edge-synced.csv
达到9点后自动恢复原服务；未改/bin/iptsd或EFI。此文件本轮重新开始，上一轮
无同步热图的9点已备份为calibration-repeat9-unsynced.csv，不能混淆。
若无响应应及时touch该目录stop并核对恢复，勿让用户重复做整轮。

**2026-10-31 9点已完成但不具备同步热图，勿误用**：再次收到用户“已完成”。
应用时间戳显示这9点在第二次采集失败并恢复原iptsd后发生，仅可作原版坐标复测。
底97%三次Y误差+48.135/+46.252/+49.229px，再次复现偏移；index3虽然显示
下92%靶点，实际坐标仍在中心，属于落点/切换时机不匹配，不能作为92%映射样本。
私有文件calibration-repeat9-unsynced.csv。已向用户说明同步采集未成功、暂不
继续点击，关闭测量APP恢复原界面；临时show_touches已经恢复null。采集器两次
仅DIAG_READY、无FRAME，均已自动恢复原服务，运行hash2210ac0a…核对。正在
验证仅诊断程序的打开设备后等待复位稳定，不改触控算法/配置/启动载荷。

**2026-10-31 18点重复测量确认下边缘非均匀偏移，尚未修复**：用户已完成。
下边85%/92%/97%三个位置各3次，平均Y误差分别-2.96/+10.59/+48.82px；
97%位置三个误差48.535/48.901/49.034px，实际触点稳定靠到Y1911附近而靶点
Y1862.4。右边同三档X均差-17.06/-13.65/+38.63px，最外档单次误差
+72.98/+54.90/-11.99px，变异明显，不能据此统一缩放全屏。
私有证据calibration-edge-repeat.csv。先前热图离线回放显示底缘拟合有外移，
但没有当时准确靶点位置，仍不足以证明算法根因。用户已同意并准备再做9点
靶点+热图同步（中心、下92%、下97%各3次）。首次单进程采集初始化有READY
但无FRAME，已停止并恢复原服务，未要求用户在该状态点击；正在通过原服务
同样的check-device预检重试，成功标准增加持续FRAME而非仅READY。
同步APP文件points-edge-synced.csv含down_time_ms可与CLOCK_MONOTONIC匹配；
会在9点完成或300秒期限后恢复原服务。诊断启动问题不能记为设备漏点结果。

**2026-10-31 边缘点击调查转向坐标偏移；重复靶点测量待用户完成**：当前仍为
Mesa boot ee9daa19…，原双补丁 iptsd 2210ac0a… 与配置 c7f31ec9… 均已恢复并核对，
未改默认构建、启动载荷或重启。用户从“小按钮无反应”进一步确认触点圆点偏离
按钮，点偏下按钮时圆点出现在最边缘。10点全屏靶点采集已完成：下方92%位置
平均Y偏低38.1px；其它点偏移不一致，两次中心X误差分别-33.5/+2.4px。尚不能
据此证明统一缩放、拟合外推或边界截断是根因，未盲目应用坐标修正。
Android触屏raw 9600×7200到2880×1920、rotation0、affine identity、无IDC；
源码拟合坐标按(cols-1,rows-1)归一化，uinput输出前clamp到[0,1]，这是路径事实，
不是此问题的因果证明。现在 org.surface.sq2.touchtest 临时测量APP在前台，测
下边和右边85%/92%/97%位置各3次、共18次；只记录应用坐标，不另开hidraw。
结果在Android用户10的该包files/points-edge-repeat.csv；首轮points.csv保留。
show_touches原值null已恢复（删除临时设置），本轮APP也不显示红点以减少追点。
测量结束后应停止/移除该临时APP；其keep-screen-on只在前台有效，未改锁屏延时。

此前四边采集须按修正事实解释：首次并开hidraw诊断扰动触控，连中心也无响应，
原服务恢复后用户确认正常；禁止再次以第二个hidraw读取器并行采集。后续单进程
诊断成功，用户实际操作为3次中心预试+20次四边，未做最后5次中心；共23次应有
点击，基线形成22次（3中心+19边缘），不能把未做的中心5次记为漏点。
同录制40/32轮廓阈值离线恢复漏掉的左边一次、历史确认空闲样本仍无触点；临时
实机40/32仍被用户报告小按钮漏点，已撤销。该实机轮后用户继续操作按钮，不能
称作30秒空闲误触验证。AspectMax放宽会放行启动边缘噪声，未采用；正常40/36、
AspectMax2、即时起落、连续性与四点热图修复继续保留。源码/录制/测量证据均在
忽略目录out/touch-edge-20261031，不纳入Git。优化尚未完成，不宣称已修好。

**2026-10-31 蓝牙启动v2在Mesa重启后ON，用户确认连接和输入正常**：新boot
ee9daa19-aaa9-4e62-8bdb-ba5b322cf8a4，cmdline仍Mesa/2.7-60，guardY、completed1。
bridge实际运行exe65bef26e…、helper6bbe379a…、watcher7f73748b…、iptsd2210ac0a…
核对。TRANSPORT_READY及PTY建立，ready=1；BluetoothManager从SYSTEM_BOOT
BLE_TURNING_ON至ON用12.896秒，crash0，原3个配对及bluetooth_on=1保留。
重复调用helper后bridge6840、watcher817、boot均不变且仍ON；无强制用户开关。
完整默认构建669d0475…与已部署基线+addon所有有效条目完全一致；蓝牙5项、
source-boot5项、USB4项测试通过。用户随后确认“已回到桌面”及蓝牙“连接和输入
都正常”，本轮启动、连接和实际输入验证完成；保持Mesa默认及原可用回退入口。
不把一次恢复解释为所有QCA超时的根因已经证明或长期稳定性通过；未验证其它
蓝牙profile。证据after-runtime/idempotence.txt及用户反馈，未追加重启。

**2026-10-31 蓝牙启动v2候选已安装，重启验证待恢复连接**：用户要求先实际修蓝牙。
原Mesa boot fbd3cb3d…持续两小时仍ready0、bridge退出EBUSY。一次受控HCIDEVUP
probe再次QCA版本超时；180秒timeout后结束，未卸载transport/清配对/强制开关。
候选去掉bridge绑定前无条件HCIDEVDOWN，改为直接等待HCI_USER接管内核AUTO_OFF
控制器（EBUSY/EAGAIN最多300次）；helper新建/复用桥接统一等待420秒并明确输出
退出/超时，不再复用尚未建PTY的桥接时直接放弃。尚不能证明这已解决QCA超时根因。
bridge65bef26e…、helper6bbe379a…，真实C启动的mock-syscall测试和helper迟到/退出/
超时fixture、旧输入watcher测试通过。source-boot新增从源码静态编译bridge，避免
后续只刷新脚本却继承旧二进制；完整默认构建669d0475…通过，保留60Hz及双触控。
身份/ESP/data/root/guard核验后安装c86610dc…独立addon，仅覆盖这两个文件，原凭据
member仍最后。仅高通/Mesa基线initrd追加addon，原可用/救援入口未变；Mesa仍默认。
菜单28806fcb…；备份EFI/BOOT/grub.cfg.before-bluetooth-startup-v2为b395bcd1…。
svc power reboot未改变boot，随后setprop sys.powerctl reboot请求后SSH断开；正在
等待用户屏幕状态与新boot验证。勿把断线当成功，也不要宣称蓝牙恢复。
私有构建/审计/安装/回退菜单位于out/bluetooth-recovery-20261031。

**2026-10-31 双触控基线保留Mesa默认；蓝牙初始化失败待修**：高通与Mesa均由用户
确认桌面、单击和拖动正常。按“保持在mesa版本”将持久默认设为sq2-baseline60-mesa，
菜单SHA b395bcd1…，原SQ2 USB storage and audio及文件继续保留回退。当前Mesa
boot fbd3cb3d-8896-4556-a8f7-3ac49e8671ea未再重启；运行iptsd2210ac0a…，
60Hz/270000×4、GPU700档、audio READY/HAL774cd7e7…、双FUSE及guardY已核对。
这仅确认已检查项目，不代表蓝牙等全功能通过。

用户随后报告蓝牙不可用。对原可用入口固定device-base与新基线按last-entry-wins
比对：蓝牙helper、input watcher、bridge、HAL/rc、QCA固件及蓝牙模块均相同；
init蓝牙启动调用保留。当前实际helper/bridge/HAL/三个核心模块哈希也匹配。
本轮不是此前BLE HID输入权限修复漏包。现场QCA读版本失败(-110)、0xfc00命令
超时，重试后hardware error0x40；bridge初期阻塞于HCIDEVDOWN -> hci_dev_close
-> cancel_work_sync，随后进入HCI_USER等待setup。/dev/sq2-bt-h4未建立、ready=0，
Android BLE_TURNING_ON。helper等待150秒后已退出；bridge的120次bind重试不覆盖
此前阻塞的ioctl，且helper退出后没有自动发布迟到transport的机制。这是已确认的
失败链与恢复缺口，尚未确定控制器为何超时，不能宣称硬件损坏或Mesa导致。
部署时遗漏蓝牙实机回归；本轮仅采集/核对，未卸载transport、强制开关或重启。
证据out/baseline60-on-20261031/bluetooth-{payload-audit.json,runtime.txt}。

**2026-10-31 两项触控补丁版高通通过，Mesa验证中**：旧基线四载荷已校验备份并
原子替换，原菜单另存grub.cfg.before-baseline60-on-20261031，当前回退文件未改。
高通boot2e5abfab-4e73-4025-b4fa-f4739f96f7a1，用户确认桌面/单击/拖动正常；
iptsd实际进程exe2210ac0a…、270000×4/2880×1920@60、GPU700档、guardY和
completed1通过。第一次查询在音频helper完成前看到stock HAL fd219b10…，随后
同boot AUDIO_READY及HAL774cd7e7…确认切换完成，不能将首次早期快照当音频回退。
qcom-runtime.txt/qcom-audio-ready.txt保存证据。已由agent正常reboot进入Mesa
一次验证，待用户及SSH结果；持久默认仍原入口，尚未提升新高通默认。

**2026-10-31 按最新默认重建双入口，触控两补丁版部署中**：用户授权重新编译并
替换之前高通/Mesa默认构建，保留当前可用版本回退。无参数触控构建on通过，
iptsd2210ac0a…；主线source-boot默认2.7-60/700MHz，采用本机固定device-base，
initramfs706c76a4…；与前次edge-only基线解包逐条比对，仅bin/iptsd变化，音频等
其他有效条目完全一致。Image/DTB仍已验证e7c7a3e5…/6db562c2…，救援514保留。
本机身份/UUID/root/guard、原菜单aac75804…、原EFI d39ea7f8…及旧基线四文件
哈希均核对，旧基线本地副本已验证备份。将原子替换/boot/sq2-baseline60-20261031
四文件，不触及当前回退/boot/sq2-android1；保留原六入口及原GRUB，不启用按键
候选。新一次菜单23097a39…、最终高通默认菜单e2445025…，上传中尚未重启。
审计/构建/安装脚本out/baseline60-on-20261031；待高通/Mesa实机核验后提升默认。

**2026-10-31 默认构建恢复接触状态连续性，未部署设备**：用户最新要求重新在
默认构建加入连续性补丁。build-test3-iptsd.py和build-source-boot.py默认选on，
同时应用patch-iptsd-sq2-touch.py与优化后的heatmap补丁；build-touch-fixup.sh默认
回到out/test2/iptsd。edge-only/off显式选项保留，未改现有连续性实现、阈值或增加
两帧等待。无参数触控交叉编译及QEMU --help通过，真实on产物通过source-boot
模式/源码指纹/二进制/config校验；选项专项及5项bundle单测通过。日志与构建元数据
留out/touch-default-on-20261031。此轮没有连接、替换或重启设备，现机仍为此前
用户确认恢复的原默认版本；不自动重新启用60Hz新基线或GRUB按键候选。

**2026-10-31 已重启到基线部署前原版本，SSH核验完成**：boot
75c7d013-813c-4ef9-b768-cf2969443670，Android completed1，原路径/sq2-android1/
参数，无grub_buttons测试标记；菜单aac75804…、EFI d39ea7f8…均匹配部署前。
原iptsd文件及运行进程exe均e4610813…，config c7f31ec9…、msm f69c91e7…，GPU
最高可用661MHz（不是新基线700），guardY/nohibernate保留。原默认仍sq2-usb-test。
已完成用户要求的恢复/重启；本轮基线和GRUB候选不再活动，无后续测试待启动。
用户随后确认“已恢复原桌面”，与SSH核验一致；日志rollback-runtime.txt。

**2026-10-31 用户要求恢复基线部署前版本，已恢复默认并重启中**：GRUB实体测试
boot6bf37446-f4d9-487e-9ffd-b9d419f7a9d6、cmdline含sq2.grub_buttons=test1、
Android completed1，证实独立候选实际加载；用户报告“电源键正常，音量键无效”。
尚未实现全部按键功能，不提升EFI、不继续原始键码采集：用户随后明确要求恢复
默认并重启到基线之前版本，该请求覆盖继续开发计划。按身份/guard和12个原启动
载荷哈希核验后，恢复菜单aac75804…（默认sq2-usb-test/原SQ2 USB storage and
audio）、原EFI d39ea7f8…保持；原c2/USB/GPU700及buttons once均0。菜单切换
前另存grub.cfg.before-user-baseline-rollback。已sync并由agent正常Android请求
重启，原桌面和新boot待核验。60Hz基线与GRUB候选文件暂留磁盘但不在活动菜单，
勿自动再启用；源码更改未回滚/提交。恢复脚本out/baseline60-20261031/rollback-original.sh。

**2026-10-31 GRUB实体按键候选一次性启动中**：新脚本固定Surface GRUB commit
71402fac…/gnulib9f48fb99…；console仅在Unicode=0时映射0x80/0x81/0x102为
Up/Down/Enter。实际新旧C翻译器契约检查通过，普通键行为不变；重新交叉编译
EFI41c4239a…/1458176B，ARM64 QEMU直接启动和原EFI chainload两项通过。
设备原EFI d39ea7f8…未改；备份BOOTAA64.before-buttons.EFI及grub.cfg.before-buttons
（菜单69607b83…）。独立/EFI/SQ2Buttons含候选及30秒测试菜单721c857d…，每个
Linux命令附sq2.grub_buttons=test1用于确认路径。主菜单6541512b…、一次env
d7f842ef…先消费再chainload，持久默认高通基线；已从bootd1c5ece3…由agent请求
正常Android reboot，用户实体按键结果和新boot待确认。成功后方可考虑替换默认EFI，
失败可恢复原菜单，勿声称软件键码测试已证明固件输入。详见BUILD及忽略的
out/grub-buttons-20261031/部署脚本、审计、两个QEMU日志。

**2026-10-31 60Hz基线部署完成，转入GRUB实体按键工作**：默认高通再次正常启动，
bootd1c5ece3-db45-4394-b90e-49a36fbf94e4、completed1、guardY、cmdline基线路径/
2.7-60/closed、iptsd63f0eed6…、菜单69607b83…、闭源READY匹配。此前高通/Mesa
两轮均已由用户确认桌面和触控正常。原文件/救援均保留，没有默认65Hz诊断。
用户新增要求GRUB音量上下选择、电源确认；当前BOOTAA64.EFI为原linux-surface
fedora-39-1 SHA d39ea7f8…/1449984B，下一步核查EFI键码翻译并独立候选验证，
不要用Android按键映射冒充GRUB支持。

**2026-10-31 高通/Mesa 60Hz基线均用户确认正常，高通默认已安装**：Mesa boot
d292913d-e102-4739-b547-09abc033feea，用户确认桌面和触控正常；实际60Hz、
270000×4/2880×1920，iptsd进程exe63f0eed6…、msm f69c91e7…，Turnip实际
runtime0b36eeba…且闭源helper DISABLED，completed1/guardY，GPU700档可见。
仅用户0已挂载FUSE（此轮未宣称用户10活动）；日志mesa-runtime.txt。正常菜单
69607b83…已原子安装，默认sq2-baseline60-qcom；Mesa一次性env确认已消费为0。
当前从Mesa由agent请求正常Android重启，待核验默认高通新boot。原六入口及文件
完整保留，另有新高通/Mesa/514MHz救援和关机，ESP余356MiB；未执行旧入口清理。
65Hz/高速研究按用户要求停止，所有失败诊断候选均未并入本基线。

**2026-10-31 60Hz高通基线通过，Mesa一次性启动验证中**：高通boot
845f7d56-9f62-4422-9fd0-3848518e8de6，用户确认桌面/单击/拖动正常。实读
dp_debug与DRM为2880×1920、60Hz/355500kHz、270000×4；原msm f69c91e7…，
运行中iptsd进程exe SHA63f0eed6…与本轮构建匹配，config c7f31ec9…。GPU提供
700MHz，CPU schedutil及1.7664/3.1488GHz上限、guardY/nohibernate保留，Android
completed1，用户0/10 emulated FUSE均挂载，resource monitor READY，闭源Vulkan
wrapper4dee0a10…/READY。仅验证启动/用户短时操作，不替代游戏、音频及长期测试。
原菜单已在ESP备份为grub.cfg.before-baseline60-20261031，原入口文件保留。
菜单a1e38b28…持久默认暂仍原入口，Mesa env a381cde3…原子安装后由agent请求
正常Android reboot；待Mesa实机核验后提升高通基线默认。日志qcom-runtime.txt。

**2026-10-31 用户终止高速链路路线，部署60Hz/2.7基线中**：用户明确要求放弃
65Hz/5.4排查，按60Hz/2.7编译含触控改动的基线并部署本机。不要继续历史建议中的
高速实验。主线构建默认已改2.7-60；edge-only重新交叉编译SHA63f0eed6…，原
touch.hpp哈希仍与上游相同，仅热图补丁793074f9…，正常阈值/即时接触释放保留。
启动包fb5f064f…、原Image e7c7a3e5…、GPU700/432 DTB6db562c2…、救援514
DTB7a4d1c73…；音频/麦克风/USB/蓝牙/视频/SSD与凭据last-entry-wins已核验。
out/baseline60-20261031含新构建、部署审计和旧菜单备份。显示/触控bundle/视频
helper/USB/GPU共23项单测通过；触控选项专项脚本通过。首次误用discover运行
专项脚本、及不存在的USB测试文件名已纠正，未改代码掩盖测试调用错误。
独立目录上传四载荷；不部署65Hz addon，不改原入口文件。一次性菜单a1e38b28…
保留原默认，候选高通/Mesa/救援+关机；待实机启动核验后才提升新高通默认。
已核对512GB BC711序列CYA5N050012105J77、ESP B027-0512、marker、data UUID
和loop0->p3、guardY。原菜单aac75804…已下载备份，当前尚未重启。

**2026-10-31 V19结束，原桌面恢复且正常HPD对照完成**：用户确认已恢复原桌面；
SSH实读boot02ccf481-2983-402c-ad1c-4c45db838302、原msm f69c91e7…、原菜单
aac75804…、Android completed1、screen_only=Y/nohibernate全部匹配。使用同一
4ee6e2ad…只读GPIO采样模块，1018840us内20000次全部high，low0/transitions0，
最大采样间隔261us；采样后卸载成功，boot未变且completed1。日志位于忽略目录
out/dual-boot-65-700/hpd-sample/original-control.txt。正常运行与V17失败后395次
跳变存在差异，但两者所处阶段不同；不能将停流后脉冲证明为首次黑屏原因。
V17/V19均未并入默认，无候选待启动，原可用入口/文件保留。65Hz仍未修复；
下一轮应在首次出流期间采样HPD并核对视频/TU/MSA，尚无厂商固件硬限制证据。

**2026-10-31 V19仍黑屏，IRQ事件未在首次停流前出现，恢复原入口中**：boot
4f79ed92…/msmbdc79154…匹配，用户确认仍黑屏；17.263秒IRQ_SERVICE enabled，
17.374秒stream与40.605秒idle前HPDstatus40000000/mask2，但没有IRQ_SERVICE
event或预算耗尽记录。不能声称现有handler已实际处理脉冲，亦不能把停流后的
脉冲直接当首次黑屏根因。读取已修正：0x100/16=00840001010101100110010000000000，
0x110/6=000000000006；前后相同，BW0/lane84/TP0/ASSR1/rate_slot6符合预期，
0x200/8仍0100777781001111→0100777781011111。原菜单aac75804…恢复，日志已
下载irq-v19-4f79ed92…；sync后agent SysRq请求回原日常入口，新boot/桌面待确认。
V17/V19未并入默认。下一步须提前到首个stream期间采样HPD与视频流/TU状态，
区分首次无画面与之后的HPD恢复死锁，不要继续将二者当同一个已证实原因。

**2026-10-31 V18训练状态未丢锁，V19接通有预算的IRQ_HPD处理路径**：V18 boot
4655fabe…/msm0fe406bb…匹配，用户仍黑屏。DPCD200/8首次出流0100777781001111，
停流前0100777781011111：service IRQ vector0、lane77/77且align1，LINK_STATUS_UPDATED
置位；sink status从0变1。不能据此称持续无误码/画面正常或固件锁定。配置0x100
单次22字节因驱动native AUX最大16而返回-EINVAL，是诊断代码错误，已改16+6字节。
原菜单恢复，日志下载。V19基于V16+修正快照（无V17），仅Surface Pro X eDP且
internal_hpd=false时，在成功plug/training后启用IRQ_HPD bit，在unplug前mask；
不启用PLUG/UNPLUG/REPLUG，复用现有ISR/event/attention处理。实验仅允许32次
有效处理，超额mask并清队列，防持续风暴；这不是默认长期方案或已验证修复。
候选msmbdc79154…/PHY47e435ce…，614imports/归档delta/gzip/GRUB/shell通过，
V6缓存还原。menu228cc3cf…、addon3ccf1196…，备份before-irq-v19；sync后agent
SysRq启动中，实际boot/画面待核验。原可用入口/文件完整保留。

**2026-10-31 V17重启HPD无效，但只读GPIO采样抓到持续短脉冲，V18读取接收端状态**：
V17 boot0154b70e…/msm5973f4a3…匹配，用户仍黑屏。40.674秒rearm前HPD2，
rearm后0再2，随后hpd_wait/-110，故单独HPD enable重启无效，不并入默认。
原菜单已恢复。新增probe/graphics/sq2_hpd_sample.c只读TLMM GPIO10：固定验证
Surface Pro X/chip3100000.pinctrl/base544/ngpio191，既不request也不改mux/方向，
20000次50us间隔，周期性cond_resched，一次约1秒；模块4ee6e2ad…、14imports及
vermagic核验后临时加载并卸载。实际1022096us内high16959/low3041、395边沿，
最长high4443us/low820us、最大采样gap658us，边沿约每5.2ms一组，低约0.8ms。
采样前后HPDstatus均20000000且boot未变。不是示波器，不保证捕获所有短脉冲，
但已否定“GPIO持续高”的前一假设；应检查接收端为什么反复触发短脉冲。

V18 patch-sq2-edp-sink-status.py基于V16（无V17），仅在HPD已经ready时，于
stream_enable_end和bridge_before_idle读取DPCD0x200/8及0x100/22；最多4组，
不写寄存器/ack，不跳过HPD。额外AUX读取可能改变接收端状态/时序，需承认局限。
模块0fe406bb…/PHY47e435ce…，总614 imports、归档delta/gzip/GRUB/shell通过；
addon33109534…、menubbaf7471…，备份before-sink-v18。初次安装命令因SCP尚未
完成而在打开脚本处安全退出；等传输退出0后重跑并全部核验成功，再sync+agent
SysRq启动。结果待核验；原可用入口和文件保留，V6缓存还原。

**2026-10-31 V16发现TLMM输入高而HPD状态pending，V17候选启动中**：高速boot
0086b5e5…、msmfac0a792…/PHY47e435ce…实际匹配，用户确认黑屏。第二次PHY init
完成，所采集配置与低速一致（pd7d/modefc/aux1=13/aux2=24/aux5=26/aux8=37/
bias17），HPD ctrl1/ref10013/time0=03e986a0/time1=61a807d0/aux11也相同，但status
20000000不恢复。重要补充：DT将GPIO10复用为edp_hot，pinctrl-msm debug代码读取
的是TLMM输入位；现场两侧采样均gpio10 in high func1，gpio130 enable out high，
panel runtime active。核验live DT AUX基址ae9a200、AHB/AUX时钟开启后，单次只读
devmem32读取ae9a200/204/218=1/20000000/10013，并重采GPIO仍高。不是示波器
连续波形，不能排除毛刺或内部采样问题，但不支持简单“面板一直拉低HPD”。
原始证据已保存reinit-v16-0086b5e5…的hpd-gpio-live/gpio-final及启动日志。

V17 patch-sq2-edp-hpd-rearm.py隔离实验：仅Surface Pro X/eDP/保存rate>=540000/
PHY reinit后status2时，把已开启HPD_CTRL bit0清除后读回，再恢复原值并读回；
保留其他位、计时器、HPD等待、PHY、电压及初次/低速行为，不是绕过就绪检查。
精确生成C的scope/寄存器位保持检查通过，模块5973f4a3…，同观测PHY47e435ce…，
总614 imports/ABI/归档delta/gzip/GRUB/shell核验通过；V6全局缓存还原。
菜单163040aa…、addon147fdb34…，before-hpd-rearm-v17备份原菜单。已sync后由agent
SysRq发起候选启动，不是自动回退；实际新boot/结果待核验，未提升默认。

**2026-10-31 V16低速对照通过，启动高速对照**：boot0b2a8b6c…，实读低速msm
e26f7741…/PHY47e435ce…，用户确认正常桌面；实际270000×4/60、两次train0。
HPD ctrl1/ref10013/time0=03e986a0/time1=61a807d0/aux11全程保持，mask0。
PHY reset后参数回默认，下一init完整恢复pd7d/modefc/aux1=13/aux2=24/aux5=26/
aux8=37/bias17；HPD恢复4并成功出流。日志下载reinit270-v16-0b2a8b6c…。
原菜单恢复后部署相同观测的5.4对照reinit-v16（仅速率cap不同），msmfac0a792…，
同PHY，menua5997094…、addon45028abc…，备份before-reinit-v16。正常Android
reboot已发出，高速结果待核验。当前原入口/文件完整，未改默认或刷固件。

**2026-10-31 V16重新初始化寄存器对照开始**：用户要求继续研究厂商固件/高速
链路恢复问题。新增patch-sq2-edp-reinit-trace.py，基于固定V14及原PHY源码，
仅添加有上限的HPD控制/计时/status/mask/AUX/reset寄存器读取，以及PHY成功
init后、power_off前、exit前配置读取；不写寄存器、不增加AUX或延时，删除标记
块逐字还原输入。读取前控制器core时钟有效，PHY仅在init成功后/exit之前读取。
日志可能改变时序；这不是修复。PHY模块47e435ce…两组共用，低速msm e26f7741…，
高速fac0a792…，仅rate cap270000一行不同；总614 imports均匹配记录的设备导出，
vermagic/归档delta/gzip/GRUB/shell核验通过，缺原Module.symvers仍非完整modpost。
V6全局msm缓存已还原。先启动reinit270-v16低速对照，menu6cbea50f…、addon98969a48…，
原菜单before-reinit270-v16、六入口和文件保留；ESP余700.9MiB。正常Android reboot
已发出，结果待核验。高速reinit-v16包仅准备在本地，尚未部署。

**2026-10-31 本组测试结束，原日常桌面恢复已双重确认**：用户确认已恢复桌面；
SSH实读booteaab734c-c2d1-4eb0-ac81-7c819fdd8d61、原启动参数、Android completed1、
guardY/nohibernate、原msm f69c91e7…、原菜单aac75804…全部匹配。当前不运行V15
诊断版，无新测试待启动。V13/V14失败未并入默认；V15仅作2.7对照，不提升默认。
本组关键结论：SW reset后HPD pending在正常2.7同样发生，差异是5.4之后无法
恢复ready；MX单项修改无效。65Hz根因/修复仍未完成，后续应比较重初始化后的
HPD控制/计时与PHY AUX状态，见DISPLAY-REFRESH顶部，不直接删reset或跳过HPD。

**2026-10-31 V15用户确认正常，排除“SW reset后pending本身异常”**：boot
7f593dce-e3fd-492a-986c-20590550e39b，实读msm71adb0b9…、270000×4/60、两次
train_ret0、completed1、guardY；用户确认正常桌面。39.220477秒mainlink关后4，
39.221510秒SW reset后2，其余时钟/PHY关后均2，与失败V14相同；39.273394秒
下一plug仍2，但39.361566秒read_sink_caps前恢复4，39.587188秒stream再次成功。
因此SW reset后的4→2是正常对照中也存在的瞬态，不能删除reset作为已证实修复。
差异是5.4无法在重新初始化后恢复HPD就绪。仅凭软件字段不能区分HPD采样/PHY
配置/面板状态，更不能定论面板体质或物理带宽上限；首次画面也尚无证据证明可见。
原菜单aac75804…已恢复，日志保存off270-v15/off270-v15-7f593dce…；由正常
对照桌面发起Android reboot返回原日常入口，新boot待核验。所有可用入口/文件保留。

**2026-10-31 V14后原链已核验，启动V15低速同粒度对照**：恢复bootdc65080b…，
原msm f69c91e7…、菜单aac75804…、Android completed=1。V15在V14基础只增加
此前验证过的rate cap270000一行，dp_panel.c逐字等于旧rate270-control，其他
显示源相同；没有V11/V13修复叠加。目标检验SW reset后HPD=2是否正常瞬态，
避免把正常复位副作用误判成根因。msm71adb0b9…，addon f7eb6f10…，menu6fb96591…，
608 imports/归档delta/GRUB/gzip/shell验证通过，V6全局缓存还原。备份
before-off270-v15，日志off270-v15-BOOT。已从原入口正常Android reboot，
结果待核验；保留原可用入口，不宣称65Hz已修复。

**2026-10-31 V14将首次HPD状态变化定位到SW reset，恢复原入口中**：实际boot
2f333dd2-291c-40fa-93f9-c3112c294836、msm9694f4c6…、completed=1，用户仍确认
黑屏。38.165597秒after_mainlink_off为4；38.166623秒after_sw_reset为2，后续
pixel clocks/OPP/link clocks/phy_power_off均2，PHY exit前后也2。下次plug为0，
caps时2，继续hpd_wait/-110。排除了“必须等PHY断电才开始状态变化”的解释；
但正常SW reset也可能暂时令HPD pending，尚无同粒度2.7对照，不能把这一状态
变化直接等同根因，不能声称面板物理掉线。需查复位后的HPD配置/恢复流程。
日志已下载off-v14/off-v14-2f333dd2…；原菜单aac75804…已原子恢复，原文件保留。
agent再次sync后SysRq请求回原入口，实际恢复boot/画面待核验。没有屏幕开关测试。

**2026-10-31 V13后恢复已核验，V14细分关闭步骤诊断已部署**：用户确认回到桌面，
boot7d0cc62b…实读原msm f69c91e7…、原菜单aac75804…、completed=1、guardY。
V14回到V12配置（不带V13 MX、不带V11状态修复），在dp_ctrl_off中分别记录
mainlink关闭、SW reset、pixel clocks、OPP归零、link clocks、phy_power_off后，
以及phy_exit前HPD状态。只读现有HPD accessor，最多96条，不增加AUX/延时/写寄存器；
去掉诊断标记块逐字还原V12输入。此关闭路径保留core/AUX时钟。诊断不是修复。
msm9694f4c6…，addon aa2875ae…，菜单8e14fee1…，608 imports与V12一致，
vermagic/打包delta/gzip/GRUB/shell检查通过；V6全局缓存还原。备份before-off-v14，
一次性env独立，原入口和文件保留。由正常原桌面发起Android reboot进入候选，
日志off-v14-BOOT，实际启动/用户画面结果待确认。ESP剩余734.6MiB。

**2026-10-30 V13 MX对照仍黑屏，原菜单已恢复并请求重启**：boot
a60e2e10-0bec-4063-b422-b06fdcb65035，cmdline=5.4-60-mx，实读msm6597bfd7…；
live DT power-domains确为<0x22 9>，genpd也将aec2a00.phy列于mx，修改实际生效。
用户确认黑屏/异常，Android completed=1。17.404秒stream结束HPD=4；40.319秒
idle前4、40.323秒idle后4、40.341秒disable前4；40.346秒PHY关闭路径后2，
下一plug瞬时0、caps时2，之后AUX仍phase=hpd_wait/-110。因此单独MX映射修正
没有解决本机故障，不提升默认；控制器reset/关时钟/PHY关闭子步骤仍需细分。
HPD字段不是物理引脚实测，首次训练成功也不证明有可见画面。
完整有限日志已下载out/dual-boot-65-700/mx-v13/hpd-phase-v12-a60e2e10…。
硬件身份/UUID/root/guard核验通过，原菜单aac75804…原子恢复；可用入口和文件
保留。随后agent在sync后明确发起SysRq应急重启到原入口，不是自动回退/自主
崩溃。恢复后的新boot/桌面尚待核验。未做屏幕开关或真正休眠测试。

**2026-10-30 V12确定HPD在关闭路径变为pending，V13单项DTB候选已部署**：
V12 boote2f93fb1…、实读msm6597bfd7…；17.20秒stream结束hpd_hw=4，38.337秒
idle前=4，38.344秒idle后=4，38.359秒post_disable/disable前仍4；38.361秒
display_after_phy_off变2，后续plug/caps始终2并卡HPD等待。这是控制器状态字段
从CONNECTED变PENDING，不是测量到物理HPD引脚电平，也未区分ctrl reset与PHY
power-off/exit子步骤。未证实第一次视频实际可见。日志已下载，原菜单已恢复。

核对发现实际DTB/live tree的eDP PHY power-domains=<0x65 0>指向MDSS_GDSC。
上游24e98cb3d5e2c86565680e00008a794b4eac0040（2024-01合入）明确修正为RPMh MX。
scripts/build-sq2-edp-mx-dtb.py仅接受现机700DTB6db562c2…，全量属性比较确认
唯一变化为/soc@0/phy@aec2a00/power-domains→<0x22 9>，不增加电压/OPP/频率，
所有CPU/GPU/温控/设备身份不变。新DTBf68b564e…，此修正是否解决本机待实测。

V13菜单5084081b…、env29b49447…；复用V12 addon cfd1c1e9…及相同msm/PHY、
原生60Hz，唯一功能变量是DTB属性。新DTB独立文件sc8180x-edp-mx-v13.dtb，
原菜单备份before-mx-v13，原六入口/原DTB保留。日志仍是hpd-phase-v12-BOOT，
须按cmdline sq2.display=5.4-60-mx区分，不因日志名前缀误判候选。准备sync后
由agent发起SysRq重启，不是自动回退；结果待核验。

**2026-10-30 V11仍失败；准备V12读取HPD硬件状态**：V11 boot44019a13…日志
证明40.295秒普通post_disable进入CONNECTED，停用后40.550秒从DISPLAY_OFF
重新enable，人工unplug已消除；但41.052秒起训练写地址0x100仍在hpd_wait返回
-110。用户确认黑屏后手动恢复原入口，不是自主崩溃；当前boot32eb7c2f…已核对
原msm/Android completed/guardY，原菜单aac75804…也已恢复。应急SysRq确实
发起过V11启动，但不能把随后用户手动恢复说成自动恢复。V11不并入默认。

V12基于V10（不叠加V11）只增加现有dp_catalog_link_is_connected只读状态访问：
首次stream结束、bridge发送idle前后、post_disable/PHY关闭前后。HPD状态字段
移位后的connected bit为4。目标区分首次5.4视频期间已失去HPD与停流/PHY关闭
导致的HPD变化，不增加AUX读写、不改变HPD检查。此访问读取现有core clock
仍开启路径中的HPD状态寄存器，非原V6完全cached-only的快照；须如实区别。
模块6597bfd7…、addon cfd1c1e9…、菜单e5b64b14…；608导入/vermagic/依赖、
归档delta和shell/GRUB校验通过，V6全局缓存还原。一次性候选目录hpd-phase-v12，
原入口保留，待启动/回读；不声称上述两候选已解决黑屏。

**2026-10-30 V10定位到HPD等待阶段，V11状态转换候选准备启动**：V10 boot
60e64b46…实读模块7a59e2ba…；初次能力/训练AUX均成功（lane7777/aligned），
17.33秒训练0，38.401秒进入unplug handler，38.412秒display_disable，38.58秒
再次read_sink_caps，之后32次500ms超时明确为phase=hpd_wait，尚未进入FIFO。
期间未记录D3请求，故不能将-110等同实际总线不回复，旧D0唤醒解释不符合本轮。
用户未留意首次画面，不能补记“从未显示”。日志已完整下载，V10菜单已恢复。

进一步源码核对：dp_bridge_post_disable对eDP无条件调用unplug handler，使
普通停用变成DISCONNECTED，再enable时先读能力；这条人工路径与日志吻合，
但V10未直接记录调用者/硬件HPD电平，不能说已证实物理HPD掉线。V11隔离
patch-sq2-edp-modeset-state.py仅Surface Pro X eDP取消普通停用中的人工unplug，
仅DISCONNECTED首次plug；保留DISPLAY_OFF→PHY init→强制retrain原路径，
不绕过HPD、不改PHY/时序/电压/频率，沿用V10观测。参考上游2022-06讨论
https://lkml.iu.edu/2206.3/00689.html；原作者未称它修复所有显示损坏，本机需实测。

真实bridge函数mock验证首次连接/普通停用恢复/强制训练/真断开/非目标通过。
首轮测试夹具把clock设到错误结构字段导致断言失败，修正夹具后通过，未为通过
测试改驱动。ARM msm9e942c8c…、608 imports/依赖/vermagic通过，默认V6缓存还原。
addon07e067d4…、菜单c8a0f21f…、env4ea74a04…；原菜单备份before-modeset-v11，
六个可用入口及文件保留。因多轮常规reboot未能从黑屏完成，计划先sync后手动
发起内核SysRq应急reboot进入一次性候选；不是自动watchdog或自主崩溃。
候选仍为5.4×4/原生60，目录out/dual-boot-65-700/modeset-v11，结果待确认。

**2026-10-30 V10启停/AUX阶段诊断已部署，准备一次性启动**：用户要求继续推进。
基于原V6未限速驱动、原PHY、原生60Hz，隔离patch-sq2-edp-transition-trace.py
仅加入日志/计数，删除标记块后逐文件还原固定输入；不含V7/V8/V9行为补丁，
不新增AUX或MMIO探测。记录现有native AUX交易的HPD等待/FIFO/完成阶段、返回值
及至多8有效字节（最多256条），显示状态/启停（128条）、D0/D3请求（128次）。
目标是区分-110来自HPD就绪等待还是实际AUX传输，并关联驱动主动关闭与接收端
失联顺序，不能将此诊断称为修复。日志可能影响时序，需要保留这一实验限制。

ARM msm7a59e2ba…、608 imports与V6完全相同、vermagic/依赖核验通过；全局
V6缓存已还原。addon cb31e78a…，菜单d2582df1…，独立一次性env3fee5ea2…，
原菜单备份grub.cfg.before-transition-v10，六个可用入口/文件完整保留。沿用
V2 common/GPU700，有限日志目录transition-v10-BOOT，无自动reboot watchdog，
无屏幕开关测试。产物/安装恢复脚本在out/dual-boot-65-700/transition-v10。

**2026-10-30 V9后原启动链恢复已核验**：用户报告需要手动恢复后，SSH实读新
boote7e7f5fb…、原SQ2 USB/audio启动参数、boot_completed=1、screen_only=Y、
原msm f69c91e7…与原菜单aac75804…均匹配。失败测试入口已撤下，可用文件完整
保留。不能把用户手动恢复记作自动回退成功；恢复后的桌面尚未再次获得视觉
反馈。此次只做只读核验，没有再次重启或显示开关。显示记录已在DISPLAY-REFRESH
顶部单列本512GB/MP1.0设备，避免误用另一128GB/DV1.0设备的65Hz成功结论。

**2026-10-30 V9传统协议5.4/60仍失败，已发起原入口恢复**：bootba32cc6c…
实读msm c93ca5cf…与候选一致；初始bw_before=10、use_rate_set=0、rate540000，
确认实际选择legacy协议，不是退回2.7。17.43秒训练0，54.47秒DPCD重读-110，
后续pixel/link时钟均关闭，Android completed=1；用户确认仍黑屏/异常。因此
简单保留传统协议不能修复本机5.4输出，此补丁标注失败且不并入默认构建。
它与V6同时改变lane写入顺序，不能视为所有协议时序组合都被排除。

关键对照：同V6代码仅限速2.7/60已由用户确认正常；5.4/60使用速率表或传统
协议均失败。后续重点是5.4主链路运行、流启停/再次训练与接收端状态，而不是
继续改65Hz EDID。不能断言硬件损坏/带宽硬上限。MSA数值43055/65400的比例
与355500/540000一致；这仅排除明显比例错误，不证明所有时钟/TU配置正确。
Unexpected DP AUX IRQ 0x01000000也出现在正常2.7两次训练，不能单独归因。
cached test_requested=512实际是DS_PORT_STATUS_CHANGED，正常2.7最早快照亦512，
后续128；不能将缓存字段名当作面板正在请求测试模式或直接当作故障触发证据。

V9日志已SCP保存到out/dual-boot-65-700/protocol-v9；校验设备后原子恢复菜单
aac75804…并核对成功，23:14左右经Android正常reboot回原入口。此时恢复桌面/
新boot待确认。原入口/文件完整保留，本轮没有关屏/亮屏测试或自动watchdog。

**2026-10-30 2.7速率表对照通过；正在测试V9协议兼容性**：用户明确确认
rate270-control正常桌面。boot4fb88d48…实读msm9998fe2d…、270000×4/原生60Hz、
SF16666667ns、guardY、Android completed；两次训练均0，38.94秒第二次成功，
后续pixel/link时钟均开启，未见上述DPCD失败。有限日志已取回，原菜单已恢复。
这排除了“速率表补丁在任何速率都失败”，尚未区分5.4的电气链路与配置切换。

新隔离scripts/patch-sq2-edp-protocol.py基于上游2025-12-18 v2协议选择：读取
初始LINK_BW_SET，非零保留legacy协议、零保留LINK_RATE_SET；不再强制清零
BW_SET切协议。保留能力表最大值/PHY原实现/原生60Hz，非table路径原样。读取
失败回到原能力路径，写入短传输/错误均检查。真实C reader/writer mock测试
覆盖选择、slot6、lane顺序、短写/超时、fallback/非目标；ARM构建、608导入闭包、
vermagic、依赖与最终last-entry-wins审核通过，非硬件修复结论。
上游来源https://lkml.iu.edu/hypermail/linux/kernel/2512.2/03295.html；其中传统
方式虽保留，5.4仍来自面板速率表，传统MAX_LINK_RATE仅声明2.7，是否兼容
本面板需要实测，不能预先保证。V9不会提升默认，也不修改全局V6缓存模块。

V9 msm c93ca5cf…，addon2565bd5a…，菜单2fb04f7c…；路径protocol-v9，独立
一次性env、有限cached日志，无关屏/亮屏、无自动重启定时器。原菜单备份
grub.cfg.before-protocol-v9，原六入口及文件保留。测试结果待核验。

**2026-10-30 23:03 同驱动2.7×4/60速率表对照已发起**：用户要求继续排查。
隔离stage sq2-edp-rate270-control从V6复制，经逐C/H文件比较，唯一源码变化为
dp_panel.c在解析速率表前将limit限制270000；保留LINK_RATE_SET协商，不引入
V7/V8或65Hz EDID。模块9998fe2d…，608 imports与V6相同、vermagic/依赖恢复
通过，速率表边界C测试通过；全局缓存msm-edp-rates.ko已恢复V6原件。
addon a736ead0…、临时菜单68d89387…、独立一次性env6111c0d0…；原菜单
aac75804…备份为grub.cfg.before-rate270-control，六入口与所有文件保留。
基础包/700DTB/PHY同上轮5.4×4/60，仅显示驱动限速；有限日志使用cached-only
debug，无额外诊断AUX读取。上次watchdog实际未恢复画面，本轮不启动该定时器，
只启动有限collector；不声称自动回退可靠。23:03正常重启已发出，等待画面/SSH。
本地产物与安装/恢复脚本在out/dual-boot-65-700/rate270-control。

**2026-10-30 带宽对照失败，已恢复原入口**：本轮boot38481d04…日志已完整取回
out/dual-boot-65-700/bandwidth60。实测为原生2880×1920@60、355500kHz、24bpp，
5.4Gbps×4（rate table index6），不是65Hz覆盖残留。16.97秒训练返回0，初期
pixel355.5MHz/link540MHz时钟开启；54.43秒DPCD读取-110，后期时钟计数均0，
train_attempts仍1，Android boot_completed=1。用户确认仍黑屏，不能将训练成功
或DRM active=1视为画面正常。原2.7×4/60正常，而5.4×4/60与65均失败，说明
故障不以65Hz时序为必要条件；高链路速率/速率表协商/驱动切换路径仍需区分，
不能据此认定面板体质差、5.4Gbps硬件不支持或已定位PHY根因。

按24bpp像素时钟预算，60Hz为8.532Gbps，65Hz为9.24288Gbps；2.7×4在8b/10b
编码后理论8.64Gbps，5.4×4为17.28Gbps。65确实超出原链路预算，但本次更高
链路的60Hz对照也未通过，故不能解释为单纯容量不足，更没有验证跑满带宽。
下一步若继续隔离，应先保持候选代码相同、限制2.7×4/60，以区分速率表协议
改动与高频率效应；不要继续叠加猜测性65Hz补丁或自动发起相同黑屏测试。

watchdog记录到240秒调用reboot syscall，但用户仍黑屏并准备手动恢复；不能
称自动回退成功，也不能仅据SSH中断认定内核崩溃。恢复后实读boot26c59326…、
原USB/audio启动参数、boot_completed=1、screen_only=Y。已校验设备序列号、
ESP/data UUID与root来源，原子恢复菜单aac75804…，撤下本次测试入口；可用
六入口/文件均保留，没有继续清理EFI或提升失败候选。恢复后的画面尚无用户
再次确认；没有执行关屏/亮屏或新候选重启。

**2026-10-30 用户授权带宽验证：5.4×4/原生60Hz对照已发起**：先实读当前正常
boot bfeb9fff…：2.7×4、2880×1920@60、355500kHz、24bpp，pixel/link时钟
均开启，SF周期16666667ns。新一次性sq2-bandwidth60沿用V2 common0673bacb…、
GPU700 DTB6db562c2…与V6 msm5fcd28a9…，PHY保持原1abbb9bd…，去掉65Hz
EDID/软件覆盖，不包含失败V7/V8补丁。与原V2/65候选主要显示变量是恢复原生60
时序；不是在原661默认上仅改一个文件。原菜单所有入口/文件保留。

仅msm+有限诊断助手/init覆盖，私有凭据仍最后；addon f0ae0eae…，临时菜单
ca76847d…，原菜单aac75804…另存grub.cfg.before-bandwidth60。最后条目精确
delta、无EDID、shell/GRUB及设备身份/hash/gzip审计通过。独立诊断watchdog延至
240秒，dry-run通过，不声称实际自动重启验证。22:48发出正常重启，结果待核对。
日志目录logs/bandwidth60-BOOT，本地产物out/dual-boot-65-700/bandwidth60。
目标是分离链路与刷新率影响；即使60Hz通过也不等于跑满17.28Gbps有效容量，
更不代表65Hz已经恢复。禁止在本轮重复关屏/亮屏故障采集。

**2026-10-30 V8后已恢复连接并取回日志**：用户报告正在启动后，只读SSH确认
boot bfeb9fff…、boot_completed=1、原菜单aac75804…、guardY。上轮phy-v8-939f566b…
日志及watchdog记录已SCP下载到out/dual-boot-65-700/phy-v8，均exit0；watchdog
confirmed elapsed=115，证明本轮已取消，不把后来连接重置归因为此自动回退。
此次没有显示开关、候选部署或重启。用户补充另一台SQ1可以65Hz；既有EDID记录
分别LGD_DV1.0与本机LGD_MP1.0，同产品ID/型号不等于面板版本完全一致。
面板/固件差异是合理待验证因素，仍不能断言个体体质或硬件故障；链路与时序
尚未在本机独立对照，不把另一台成功推广到本机。

**2026-10-30 V8也未修复，当前等待恢复连接**：本轮boot939f566b…，实读PHY哈希
b7966ee5…及SQ2_EDP_PHY_SC8180X_V2 enabled，确认候选实际加载。用户仍报告
黑屏；54秒DPCD重读-110，训练次数1，eDP pixel/link时钟计数0，Android启动完成。
上游信号表回移不能解释为本机黑屏已修复，不并入默认构建。V7/V8两候选均失败。

已远程取消本轮回退计时以采集状态（确认文件仅用于接管，不是画面通过），保存
Android/SF/DRM/clock/DP快照到ESP logs/phy-v8-939f566b…；**尚未下载本轮日志**。
已校验并原子恢复菜单aac75804…，原六入口/文件全保留。随后在guardY下仅做
cmd power sleep→input KEYCODE_WAKEUP的显示路径采集，临时drm.debug=0x100，
SSH连接被重置；此时没有另发整机reboot。不能把中断直接记作整机崩溃或自动回退。
采集shell可能未完成，所以debug参数恢复和screen-cycle.txt落盘均未确认；参数
仅RAM，下次正常启动恢复原值。已请用户若仍黑屏手动关机再开，选原USB/audio。
下一步先确认boot/菜单/guard并取回ESP日志，不重复屏幕开关或同条件65Hz启动。

**2026-10-30 PHY V8候选已启动测试，结果待确认**：V7撤回后已SSH核验恢复boot
f5c4ab3f…，原入口/菜单aac75804…、boot_completed=1、guardY。进一步发现6.3
phy-qcom-edp的SC8180X match data为空，set_voltages直接返回；上游2026年专门
补充SC8180X独立v2摆幅/预加重表及v3 LDO（低速0x81/HBR2 0x41）。当前隔离
patch-sq2-edp-phy-sc8180x.py将该支持回移，仅Surface Pro X+sc8180x-edp-phy
启用；不改变芯片身份、外部供电、PLL、AUX、65Hz时序或其他设备配置。
上游依据见linux-phy 2026-April/030558.html、030505.html，以及Torvalds commit
3a2976df778a9af95e91f7ff88008b4517ddc658的phy-qcom-edp.c；不是SC8280XP参数。

V8仅替换PHY（b7966ee5…），恢复原V6 msm5fcd28a9…，不包含失败D0补丁。
两项测试通过，其中真实setter的模拟MMIO覆盖16组合、非法拒绝、2/4lane LDO及
非目标路径；ARM构建、32导出闭包/vermagic、原依赖为空和last-entry-wins通过。
未完整modpost、尚无实机成功结论。与原65候选相比有效差异为PHY及有限诊断助手，
原凭据仍最后；addon ae1ebf7c…，临时菜单68a05769…，独立phy-v8一次性env。
保留六个可用入口及默认；约22:34发出本轮重启，后续需先核验画面与实际时钟，
失败即恢复原菜单。日志目录为/media/esp/logs/phy-v8-BOOT；watchdog继续复用
V7二进制/确认文件路径，不能误把文件名中的V7当本轮显示驱动版本。

**2026-10-30 后续V7早唤醒实验仍失败，已发起原入口恢复**：用户要求继续排查、
保留可用启动项。核对源码发现流关闭会请求D3，而重启链路先读能力、后请求D0；
旧drm_dp_dpcd_read还会先probe REV。隔离patch-sq2-edp-wake.py仅对Surface Pro X
已发现过的eDP接收端，在能力重读前直接发送D0，最多三次，错误正常传播。
主机编译执行真实C helper的成功/重试/超时/短传输/设备范围测试通过；ARM模块
f775fc8c…构建、608导出闭包及仅dp_panel源文件变化校验通过，不是完整modpost。

一次性V7 boot a2e76356…中用户确认仍黑屏，并补充从未看到系统画面。
约84秒SQ2_EDP_WAKE attempts=3 rc=-110，提前D0也未恢复AUX。后续SSH可达、
boot_completed=1；DRM软件状态2880×1920@65、SF周期15384815ns，但实际eDP
pixel/link时钟enable/prepare计数均0，训练次数仍1。刷新率/初次训练返回成功
不能证明物理输出。三次底层超时会延长启动；此实验不并入默认构建，也不标修复。
当前证据未确定首次黑屏根因，不能直接称面板不支持65Hz；下一步需隔离链路与
时序因素，避免继续叠加猜测性补丁或重复同条件启动。

原六入口和所有有效文件保留，仅追加V7 addon/menu。旧全局sync前置回退改为
独立静态诊断watchdog，2秒dry-run日志通过；实际运行到178秒因SSH恢复后远程
接管而取消（confirmed只表示取消计时，非画面通过），未执行自动重启，故仍不能
宣称自动回退实测成功，也未查明上一轮定时器失效原因。有限采集增补Android、
DRM、clock及SF日志，均下载到out/dual-boot-65-700/wake-v7。

已在失败boot内核验并原子恢复原菜单aac75804…，撤下V7入口；SSH发出正常
Android重启，连接被重置。用户随后报告“正在启动”，恢复桌面/新boot仍待确认。
没有删旧启动项、没有提升65Hz或触控候选默认。V7源码与测试仅供复现失败假设。

**2026-10-30 双65Hz候选再次黑屏后已手动恢复，原菜单已核验还原**：以下早期
“等待恢复”状态已过期。当前boot为5c0b4e92…，Android boot_completed=1、
screen_only=Y，原菜单SHA aac75804…恢复成功，默认仍SQ2 USB storage and audio。
失败的65Hz入口不再出现在菜单；候选文件仍保留，EFI空闲946440KiB（约924MiB）。
未继续删除任何已引用的旧启动文件，未执行双入口finalize，Mesa65尚未测试。

60Hz/700MHz对照boot6ed551eb…启动成功，用户确认恢复，GPU最大频率读回700MHz，
SurfaceFlinger周期16666667ns。65Hz第二轮使用修正视频helper覆盖问题的v2 common
0673bacb…和原display-addon ba0fa829…，boot037ecada…仍被用户确认为黑屏。
自动重启未被用户观察到，最终由用户手动恢复；不能把该定时器当可靠回退保障。

已取回该失败boot的32份有限内核快照（至约138秒）及启动助手日志，证明黑屏期间
内核继续运行，不是已证实的内核死锁。约17秒eDP训练540000×4、像素时钟385120000
及video_ready的返回码均为0，但约55秒重新读取DPCD超时-110；成功的60Hz采集中
未见该错误。SurfaceFlinger已启动，bootanim约96秒退出。不能由寄存器返回成功
推导画面正常，也不能由DPCD超时直接断言面板不支持65Hz或已经找到根因。
失败boot附近的system_server ANR已取回，主线程在包签名证书解析，不是显示死锁
的直接证据。原始日志留out/dual-boot-65-700，不提交。自动回退失败原因仍未确认。

构建中另修复一个独立问题：video.integrate映射的video-runtime-stage曾被通用
helper刷新循环用同名旧video-codec-manifest-stage覆盖。现在排除video.HELPERS，
最终verify_helpers校验实际路径/权限/源码。3项针对性测试通过，v2失败启动日志
显示SQ2_RUNTIME_BOOT_STAGED_DATA_UNTOUCHED，证明新stage已运行；不是65Hz修复。
当前恢复的旧启动项仍运行原IPTSd；edge-only已经是源码构建默认及候选内容，
不能声称已提升为当前设备默认。下一步先隔离显示模块/链路与时序因素，并补足
可留存的Android显示日志及回退故障证据；没有新证据不要重复同一黑屏启动。
out内早期deployment.json/finalize.sh仍引用v1哈希，后续若部署必须重新生成并核验。

**2026-10-30 双65Hz/700构建已上传，高通候选黑屏，等待手动恢复，禁止继续清理**：
用户要求edge-only设默认，并只保留高通/Mesa两个65Hz/700入口。builder/启动打包/
touch-fixup已默认edge-only，on保留双补丁、off全关。主线仍6337fae（已fetch），
本地变更尚未提交；此前commit因未配置Git作者失败，未伪造身份。现机已有完整
ESP备份out/dual-boot-65-700/backup/esp，810文件/2073711233B全部与远端SHA匹配。
六份旧菜单未引用的capabilities/experimental镜像逐个再核验后删除，释放
1189519452B，EFI空闲从65728KiB到1227384KiB；原默认入口文件全部保留。

通过主线build-source-boot新增固定现机基线732f23fe…入口，构建base40012129…、
display-addon ba0fa829…、Image e7c7a3e5…（复用已验证内核）、DTB6db562c2…
700/432。edge-only aff15109…，USB/BT/音频/硬解/凭据审计通过。V6显示模块
重新编译5fcd28a9…、dp_panel.c7d87f794…、608导出闭包通过，无完整modpost；
现机原EDID0feb26c6…生成65Hz daf892fa…。Turnip重编同原0b36eeba…；高通
专有库/原Mesa GLES保留，不声称全内核或闭源源码重编。主机触控5、显示5、GPU6、
USB4、视频XML2/runtime3等测试通过。

文件已原子部署/media/esp/boot/sq2-dual65-700（四文件），设备端SHA/gzip通过。
原菜单追加两个候选及sq2-dual65.env一次性选择，旧default=sq2-usb-test保留，
先选择qcom并主动Android reboot。重启后SSH192.168.8.194持续超时，用户明确
报告黑屏或画面异常。已请用户长按关机重开，若有菜单选原SQ2 USB storage and
audio。一次性env设计为加载候选前消费，但尚未重新读回，不能断言已消费或已恢复。
当前真实boot/画面/服务未知；没有再尝试Mesa或删除剩余旧启动文件。

下一步先恢复原启动并核对boot/guard/旧服务；恢复后撤销候选菜单默认实验选择、
取pstore/可用日志。不能将另一台LGD_DV面板65Hz成功推广到本机LGD_MP1.0，
也不能仅凭源码/符号一致判定重编msm实机可用。最终双入口菜单和finalize.sh仅
在out/dual-boot-65-700本地准备，**未执行**；不要在两候选未通过前执行。

**2026-10-30 用户授权仅边缘热图的单次对照已执行并恢复**：新增IPTSd builder
实验edge-only模式，独立新源码只打优化热图补丁；touch.hpp实际/上游SHA均
ea2ce098…，保持原配置c7f31ec9…与AspectMax2。完整AArch64编译/QEMU --help及
选项测试通过，候选aff15109…上传并实读运行PID10320/hash，Android输入节点
enabled。启动check-device第一次短报文错误，经既有一次重试成功；不掩盖该记录。
独立setsid脚本保留hidraw FD、固定原版/候选hash、guard/boot/设备检查、EXIT回退，
运行120秒后自动恢复。采集仅87行、一段约166ms的单指移动，DOWN/UP与tracking
起止一致，没有本片段中的按钮状态中断。用户随后明确反馈“响应更快”，作为本次
主观对照结果；尚无端到端毫秒测量，多指/长期防误触及断触情况仍未充分验证。
原版PID21289/exe与/bin/iptsd恢复e4610813…，
配置c7f31ec9…、boot fcda7cff…、guardY均不变；ORIGINAL_RESTORED已确认。
未改ESP/启动镜像/默认选项，原始证据留out/touch-latency/iptsd-edge-only-trial-20261030。

**2026-10-30 触控热图优化及完整启动构建开关**：用户要求用当前优化替换主线
补丁，并可选择是否加入。四点修复改为先计算再统一写回，移除整图复制，保留
045E:0C21/46×68、原始邻域和阈值；连续性逻辑不变。build-source-boot新增
--ghost-touch-fix on|off，默认on，与build-test3-iptsd保持一致；不再固定取off
产物。产物清单新增补丁源码SHA，启动构建拒绝旧补丁/错模式/损坏文件/不同配置，
audit记录实际模式和清单。两种完整IPTSd均重新编译及QEMU --help通过，真实
bundle经启动构建选择器验证通过。5项bundle拒绝/选择测试、选项脚本、2项设备
匹配测试及AArch64热图2000组等价/连续性回归通过。尚未部署新IPTSd。

用户先前要求65Hz/700MHz高通与Mesa两启动项并清理EFI仍待完成。已fetch确认
origin/main6337fae，ESP为BC711/CYA5N050012105J77、B027-0512，data4f46d2ec…，
loop0来自p3、screen_only=Y；当前ESP仅约64MiB空闲。完整ESP已SCP取到
out/dual-boot-65-700/backup/esp（传输exit0，但尚未逐文件核对远端hash），没有
删除或改菜单、没有重启。out/dual-boot-65-700/source是6337fae独立检出，未包含
本轮触控更改，后续须更新才可用于新构建。主线build-source-boot所需source-boot、
password-ssh基线及vp8-limits等私有输入本机部分缺失，须按固定hash恢复，不得
绕过输入门槛或用通用SQ1无补丁镜像覆盖SQ2。
**2026-10-31 SQ1 休眠实验未通过，等待恢复取证**：同一 WIP 分支追加 [SQ1-SUSPEND-WIP.md](SQ1-SUSPEND-WIP.md)。首次真实请求虽返回0，但 IRQ172/SSAM 在 noirq 阶段触发，trace 无 machine_suspend，不算睡眠成功。定位到启动脚本强制开启了驱动因未实现 EC 事件过滤而默认关闭的 SSAM 唤醒。第二次脚本拟临时关闭该源、保留 PMIC 电源键后，SSH 断开，用户确认短按仍黑屏；后半段 ESP 日志尚未取回，不能断言进入 idle、唤醒路径或清理完成。已请求长按恢复原默认入口，尚未确认恢复。仅归档移除强制 enable 的脚本副本补丁与主机测试，不安装、不提升默认、不宣称修好。下一步先确认恢复并取旧日志，不自动重测。

**2026-10-30 SQ1 休眠工作另分支保存，未完成**：见 [SQ1-SUSPEND-WIP.md](SQ1-SUSPEND-WIP.md)。已归档精确哈希绑定的诊断内核、QCA NVM 和 HCI_USER 接管补丁；实机 devices/platform 调试返回成功，但真实 s2idle/电源键唤醒未执行。单次同进程测试内核已启动，screen_only 仍开启、许可为零；用户目前不方便现场操作。等待配合，不重复重启、不改默认构建、不把本 SQ1 的镜像用于另一台 SQ2。原始证据与凭据仅留忽略目录。

**2026-10-30 内置扬声器问题已解决，默认 +12 dB**：用户选择 RX7/RX8 原始控制值 84/84（0 dB，相比原72/72提高12 dB），确认听感很好，并在获准重启后明确确认设备正常启动、已播放音频、响度正确，声音问题已解决。仅默认音频路由改变，PA/成对端口/HAL/固件/均衡及其他启动入口保留。已原子部署默认入口增量；首次重启 SSH 观察超时，后续验证依据用户确认，不冒称独立 SSH 读回。默认源码构建已显式纳入并审计该脚本，完整构建通过；不复刻 Dolby，也不宣称 Windows 实际调音曲线完全一致。详见 [SPEAKER-LOUDNESS.md](SPEAKER-LOUDNESS.md)。提交和推送以 Git 记录为准。

**2026-10-30 用户要求 USB/蓝牙提交主线并默认构建集成**：当前工作分支已为 main，蓝牙补丁344feb0原已在主线。新增默认 USB/蓝牙 payload 集成与源码/audit校验，build-source-boot真实完整构建 initramfs e6027440…通过（包含两补丁，私有SSH最后），默认磁盘菜单正常启用USB/救援禁用，并在manifest/实际packed-ESP部署前校验载荷及源码SHA。8USB/默认集成+3蓝牙+5显示+3视频菜单测试、普通文件FAT ESP装入真实新包后的三类生产校验通过；没有制作128GB整盘/烧录/重启现机，现机已测默认8abfe…保持。通用源码构建的既有显示/触控选择不变，不能把此整包当成现机无差异升级。源代码提交仅USB/默认构建及相关文档测试，不混入独立音频、REA或私有产物；具体提交/推送结果以Git实测记录为准。原四角色out/usb-c-20261030保留，default-integration补充源码副本基线USB未接线/修改接线/回滚未接线（蓝牙一直在），原hash恢复、patch重建通过；不把这三阶段副本检查记为三次实机启动。
**2026-10-30 USB 存储与 USB 音频完成默认启动回归**：FAT32 外接盘已进入 vold PUBLIC 卷，用户确认 Files 读写；USB-C EarPods 播放左右与录音回放确认正确。独立 addon46896b1f…先在 boot2bdc…一次性启动通过，再提升默认并在 boot8abfe…正常重启通过，耳机预插仍 card1、内置 card0。标准 APK 不指定 preferred device 时仍自动走 USB（type22）：左右192000帧、录音240000sample/5202ms并回放240000帧；用户确认默认重启后热插拔 U 盘仍正常。guardY、双 emulated MOUNTED、HAL774cd7e7…及C2服务保留，测试应用已停止。只为三 USB 控制器补 voldmanaged portable 规则，成套早期加载USB音频模块/策略/VINTF；不改内置mic/SoundWire/增益或传输，不格式化userdata。DP不在此完成范围。见 docs/USB-FUNCTIONS.md。

默认菜单SHA44461c17…，原四入口和镜像、菜单备份均保留；并发蓝牙输入addon导致旧菜单hash保护中止后，已审计仅两蓝牙helper变化并保留该更新。私有SSH仍最后。新增pack-sq2-usb-functions.py、usb-functions-stage及配置、公开API测试APK源码；源码重建与实测addon同SHA，四组/九fixture场景通过。通用build-source-boot仍需显式保留本USB addon，不能用其覆盖现机非视频配置。最初直接restart vold触发reboot_on_failure/reboot,vold-failed事故，现已查明并禁止重试；其后USB-serial为旧U盘启动分支，NVMe已恢复。原四角色沿用out/usb-c-20261030并扩展storage/boot-v1记录；副本基线/修改/回滚不替代上述实机结果。未提交私有产物，未修改或发布凭据。
**2026-10-30 蓝牙鼠标输入修正已固化（未做本轮重启验证）**：MX Master 3S (046d:b034) BLE 加密成功，但 EventHub 在 devtmpfs 新建 event22 后先于权限就绪打开，报 Permission denied；UHID_CLOSE 后约2秒 ready timeout，Android 主动断链并禁用 HID policy。临时重发布后同 boot db6105fb… 的 EventHub/InputReader 已识别 enabled CURSOR/EXTERNAL、MOUSE 与双滚轮，加密连接保持。用户报告已连接；15秒采集没有操作事件，不能代替移动/按键/滚轮的用户确认。

用户随后要求固化并提交主分支。新增 bluetooth-input-watch，只选择 /devices/virtual/misc/uhid/0005:* 输入节点，按 sysfs 身份去重并在发布前设 root:input 0660 与 input_device 标签；失败不替换节点，重查设备身份避免重用 eventN。bluetooth-framework-boot 在复用或创建 transport 前启动单实例 watcher，不写 bluetooth_on、不清配对、不卸载 QCA。通用 source-boot 与两个既有打包入口纳入新 helper；原 cover/触摸脚本未改。不是持续采集原始输入日志。

现机另已正常启动到 2bdc010d…（此前 USB 测试入口，不归因为异常）。本轮核对 BC711/CYA5N050012105J77/1000215216、marker、ESP B027-0512、data 4f46…、system_a erofs 根和 guardY 后安装 640707B 独立 addon 3ecfc42e…，只覆盖两个蓝牙脚本，私有 SSH member 仍最后。原 runtime+caps、clean、USB 三种组合 last-entry-wins 审计仅此两项变化，init/音频/显示/模块/凭据不变。菜单仅这三项 initrd 追加 addon，默认 sq2-c2-test 不变；原 normal/Mesa 回退不改。菜单新 SHA6b47af12…，原 a4f93c00… 保存在 ESP EFI/BOOT/grub.cfg.before-bluetooth-input-v1；sync/hash/gzip/原子替换通过。未使用另一台显示实验机的65Hz菜单覆盖本机。

同一 helper 已装入现机 RAM，连续调用两次均 TRANSPORT_ALREADY_READY，watcher10218 单实例、bridge6583 不变、boot 未变。没有重启整机/框架、改 userdata 或测试休眠。三项自动测试含真实 watcher 的 root 文件系统 fixture（标签命令仅 mock）、过滤/失败保留/去重、打包精确 delta/凭据/拒绝及构建接线。原四角色 out/bluetooth-mxmaster 保留：BASELINE root:root0600、不处理；MODIFIED root:input0660、重发布；ROLLBACK root:root0600、不处理，均exit0/恢复hash一致。后续仍需正常重启后连接与真实移动/按键/滚轮验证。见 BLUETOOTH-INPUT.md；不宣称其它蓝牙 profile 或长期稳定性通过。

**2026-10-30 eDP 结论提交与主线构建整合**：保留原生 5.4Gbps×4/约65Hz 为用户
选择的实验默认；`build-source-boot.py --display-profile 2.7-60` 可改选原面板
2.7Gbps×4/60Hz 默认。两档始终分离：common 使用原 msm，65 entry 才追加固定
V6+软件 EDID addon；60/救援不带覆盖，SSH 凭据 addon 仍最后。源码跟踪菜单、
boot audit、镜像 manifest 与部署前 packed-ESP 校验相连，不放宽磁盘保护，
不部署/重启现机。详见 BUILD 当前节及 DISPLAY-REFRESH；禁止把后续70Hz异常
或非原生黑屏推导为已测得物理带宽上限，也不把旧 get_modes 失败补丁用于默认。

**2026-10-30 带宽/降分辨率小对照：未测出带宽极限，已同boot恢复65**：用户先要求完整Android70测试。一次性入口实际启动88d5ec48…，5.4×4、DRM2880×1920@70/414750kHz、Android70、guardY均匹配，但用户明确画面/操作异常，不算通过；一次标记消费0，菜单恢复37bdce73…，用户手动恢复后adab52a4…实际65/Android就绪复核。没有自动恢复重启或热卸显示驱动。

用户随后提出2.7×4有效带宽假说，明确选择“固定70Hz降低真实输出分辨率”。已说明原生65按时序预算9.24288Gbps已高于8.64，不能从花屏直接认定硬门槛。新增软件EDID矩阵8c66f21b…，原生65保留首选，另列2560×1706@59.998135（283160kHz/预算6.79584Gbps）、同分辨率@69.999237（330360kHz/7.92864）、2700×1800@69.998662（366170kHz/8.78808）。只需一次矩阵候选启动；原init/base/Image/DTB/模块080cceff及凭据保持，原60/救援及默认65保留，无自动重启。Android cmd实机help支持临时STORE_MODE=false；get-active-display-mode虽列help但实际Unknown command，已改用dumpsys+真实DRM读回，未重复执行切换。

矩阵boot59556813-0cf8-4d11-90d7-056c6928ebc7，Android识别4模式，初始原生65。用set-user-preferred-display-mode 2560 1706 59.998135 0 false切真实KMS：Android2560×1706/59.998135、DRM2560×1706@60/283160、display ON、pixel读回283160000、链路540000×4；用户明确“2560×1706是黑屏”。此负对照预算6.79584<8.64仍失败，不能把黑屏当超2.7×4的带宽证据；本内屏非原生时序/视频传输配置混杂尚未排除，**带宽极限与70Hz最高可用分辨率未确定**。按控制门槛停止，后两个低分辨率70模式仅枚举、未执行，不原样继续逼近。

reset-user-preferred-display-mode 0 exit0，在同boot595…恢复DRM原生65/385120、Android64.99915、临时偏好null、guardY，用户确认“已恢复正常”。system_server/SF仍2222/2030，未重启框架或整机。已按盘身份/双UUID/root/hash/一次标记0校验，备份并原子恢复原5入口菜单37bdce73…，默认65/原60/Mesa/救援/关机保留；多模式仅当前启动驻留，下次默认仍单65。build-sq2-bandwidth-edid.py仅软件EDID、保持原生首选并替换3描述符为测试DTD，不写EEPROM；1项时序单测通过，原四角色control-v5复用，矩阵副本1/4/1模式、原生首选均64.999156、校验0、rollback65 hash/重建通过。out/display-refresh/bandwidth-test含方案/安装/控制失败/恢复证据，未入Git；新建status.json记录此失败控制与下一步约束，不自动重启或继续降分辨率。外部跳帧/物理链路吞吐仍未测。

**2026-10-30 默认65真实重启验证通过（短时）**：用户要求自动重启验证。第一次主动重启到4fb46847…，c754模块匹配/5.4Gbps×4/Android完成/用户确认桌面触摸正常，但实际DRM和Android仍60Hz，不能算65通过。已定位dp_drm.c对eDP不设置DRM_BRIDGE_OP_MODES，内置CONFIG_DRM_PANEL_EDP=y的panel_edp_get_modes直接读取EDID，跳过原MSM get_modes补丁；原源码主机测试不能证明该真实路径。该失败尝试及模块/源补丁保留为历史，不再作为有效65机制。

R2改用已启用CONFIG_DRM_LOAD_EDID_FIRMWARE的标准覆盖：只有默认65入口增加`drm.edid_firmware=eDP-1:edid/sq2-native65.bin`，不写屏幕EEPROM。以原128字节EDID14528fbf…生成软件副本，仅改DTD像素时钟、软件范围上限和校验，共偏移54/55/78/81/127；原生时序总量/极性及其他信息不动。EDID以10kHz表示时钟，385120kHz对应64.999156Hz（标称65），不是伪称精确385125。覆盖文件SHA83101509…，恢复已验证V6模块080cceff…，addon52767797…只改msm并新增EDID文件/父目录，原init/Image/DTB/base c5及凭据最后保持，无自动重启/回退钩子。

R2身份/hash校验、备份grub-before-default65-r2.cfg及原子替换后第二次主动重启到9fac091e-49cf-4015-bd2f-fa4321693745；实际cmdline/参数、固件文件与生效EDID均匹配83101509…，模块080cceff…匹配。DRM活动2880×1920@65、clock385120，链路540000×4，驱动像素读回385120000，Android默认/支持/渲染64.99915Hz，Android完成、SF/HWC running、guardY/nohibernate通过。约275秒复核同boot、system_server/SF PIDs2190/2022未变。新菜单SHA37bdce73bb6c96410579e5099da8adf87acba9614564bf1d935e24927dac1356，默认仍sq2-edp-default65；原四个60Hz/救援/关机入口无EDID覆盖且保留。本轮共两次明确主动验证重启，无自动循环；当前不再重启。**用户随后明确确认修正后这一轮画面、触摸和操作正常；外部不跳帧和长期稳定性仍未验证**。

有效源码/模块回到cached-only V6，原四角色同路径继续：MODIFIED_FILE.c/DIFF为实际部署V6（原错误get_modes为V7_DEFAULT65_UNREACHED补充归档），主机BASELINE/MODIFIED/ROLLBACK诊断AUX3/0/3、速率540000/index6，均exit0，恢复hash及补丁重建通过。default65-r2下EDID_BASELINE/sq2-native65/EDID_DIFF.json/EDID_ROLLBACK.sh/EDID_VERIFICATION扩展同一证据集，副本基线/修改/回退60/64.999156/60Hz校验和0、源hash恢复、逐字节重建通过；非三次物理启动。build-sq2-edp-edid65.py两项主机测试覆盖字节范围/保留原输入/拒绝覆盖及错误EDID，无私有原始EDID入Git。默认构建工具链并未全局启用65，仅此已识别设备的独立入口；未commit/push。

**2026-10-30 用户已要求5.4Gbps×4/65Hz设为默认，菜单已安装但新开机未验证**：用户随后明确尽量减少重启、可手动恢复，后续候选不含自动整机重启钩子。65Hz色条目视正常、130vblank/2.003758秒，原生回60成功，但显示服务恢复中SSH中断，之后415bc01d…原模块/Android复核；不能把此当完整65Hz桌面通过。无自动重启候选597ee8…保持原init，只替换V6模块。在5bbae357…单次接管中70Hz140vblank/2.000437秒/用户目视正常，75Hz150vblank/2.003712秒但用户报告异常；两档原生回60均成功。保留测试FB至Android接管时软件工具exit0、服务/活动60读回通过，但用户桌面仍异常，随后用户手动恢复，9bff4bf2…原msm f69c91e7…、guardY、Android完成复核。不能称桌面恢复修复或75Hz通过，不再自动重启。

最新默认65使用新独立模块c754e4142b796143cc92ddc02bb1ea25879d0029ef952663394fec0c30245798，源码7ec1c028…；只在SurfaceProX/eDP/本机LGD0555+LP129WT112684/至少5.4×4且原始完整60Hz时序匹配时，将get_modes的clock355500改385125并保留preferred。EDID/时序总量/极性/PHY参数不写改。标准drm_probe_helper在get_modes后重算crtcinfo，未放宽原依赖/导入检查，608导出闭包通过，无完整Module.symvers限制仍在。原base c5c64cda…不动；addon7cd97a1b…最终条目只改msm，init完全不变、密码SSH addon最后、无自动重启或联网/持续日志。

已按磁盘序列号/ESP marker/双UUID/root来源/guard及载荷hash校验，备份当前菜单grub-before-default65-manual.cfg后sync原子安装。实读默认项sq2-edp-default65，菜单完整SHA246fa9fdd86c558c9b83586849df7665369f14d220e36fd154f3189a439de9e6，五入口含新增默认65及原Qualcomm60/Mesa60/救援/关机，原四入口字节/行为不改；原始60菜单备份a7cc581f…保留。当前仍9bff4bf2…原模块正常60，**没有额外重启；完整Android65Hz开机、稳定性/跳帧待用户下次手动启动验证**。若异常手动选原Qualcomm入口，不热卸DRM/蓝牙或格式化。四角色仍control-v5同路径，最新MODIFIED/DIFF已更新为default65；真实get_modes主机BASELINE/MODIFIED/ROLLBACK clock355500/385125/355500及七种范围检查均exit0，恢复hash/patch重建通过。菜单副本默认项60/65/60、入口4/5/4及GRUB语法均exit0。out/display-refresh/default65保留安装/回读/审计；不提交私有镜像日志，本轮未commit/push。

**2026-10-30 eDP现机可行性测试与回退已完成**：独立worktree/原诊断分支，现机为另一块Toshiba128GB SSD及LGD_DV1.0面板，不能复用旧512GB固定身份安装器。保留其实际c5c64cda…默认镜像、Image/DTB/凭据与正常/Mesa/救援/关机四入口。V5限速2.7Gbps速率表对照实机BW_SET0/RATE_SET3/训练通过，用户确认正常；自动回退后原msm哈希复核。V6仅去除诊断AUX读，能力/训练读保留；源码7d87f794…/模块080cceff…/现机独立combined45bdb3e9…，608导出闭包和原依赖/last-entry-wins/密码addon最后通过，仍无完整Module.symvers验证。实机5.4Gbps×4、60Hz正常且用户确认，不能据此断言同一改动修复旧机V4。

原生KMS先跑60基线并恢复桌面，再90Hz：533250kHz、180vblank/2.005761秒、驱动时钟读回匹配；用户最初反馈异常，随后明确90Hz持续色条阶段正常，不仅是最后60回退帧。只算短时可行性，未集成Android90模式、未做外部跳帧/长期稳定性。120首次SSH失败且无runner目录，不计有效测试；独立重测eac96f1b…实际711000kHz、240vblank/2.003816秒，但用户持续异常，恢复60的KMS返回E2BIG。持久日志明确uptime93.99测试exit1、139.78独立75秒watchdog因缺恢复标记重启，非定时器提前打断成功测试。最终02596cb6…原msm f69c91e7…完整hash/Android完成/guardY/活动2880x1920@60复核，用户确认桌面恢复；原四入口菜单按身份/hash核验后sync原子恢复为a7cc581f…，默认c5镜像始终未改，无需额外重启。

新增patch-sq2-edp-cached-diag.py重现精确V6/拒绝二次应用；sq2-edp-mode-test.c是显式实验工具，非开机项；时序/参数主机ASan/UBSan测试通过，真实硬件结果独立记录。120原时序禁止原样重试；下一步若用户继续，先查E2BIG驱动拒绝位置/接收端时序约束，并为90补外部不跳帧及Android可恢复模式集成，不自动提升默认。证据在忽略的out/display-refresh，四角色仍control-v5同路径，最新源副本/补丁为cached-only V6，V5源/diff作为补充保留；主机BASELINE/MODIFIED/ROLLBACK诊断AUX调用3/0/3、速率均540000/index6、exit0、恢复hash及补丁重建通过。详见DISPLAY-REFRESH，原始日志/镜像不入Git，本轮未commit/push。

v4回退后补验：推送诊断分支后SSH已恢复，新boot ID、boot_completed=1、screen_only=Y、原msm完整哈希f69c91e7…均已核对，用户随后确认桌面/锁屏恢复，关闭下文“未复核回退”的缺口；v5仍未部署。

eDP v4诊断（2026-10-29）：初始5.4Gbps×4训练返回0，实读DPCD BW_SET=0、RATE_SET=6、lane状态77/77、对齐81，证明初始训练通过；不证明画面或高刷通过。PHY/OPP/像素时钟/视频ready调用均返回0，但vsync计数0/9/9/10/10停滞，后期AUX读取阻塞。已取回原始日志并主动sync+重启；用户最新反馈仍在启动，尚未复核本轮回退。v5仅本地构建：同一速率表路径限制2.7Gbps/index3、AUX仅前三次采样，待设备恢复后做对照，未部署。当前设备调试项仍v4，正常闭源/Mesa镜像未改。新分支codex/sq2-edp-training-diagnostics；详见DISPLAY-REFRESH。

**2026-10-30 独立干净编译并实机测试通过（未提升默认）**：用户要求全新编译并在本机测试。独立kernel-out从空目录编译Image/标准模块，补齐窄media基础模块配置后严格modpost通过，再编译当前Venus、C2和私有mapper。内核源为本机已打补丁归档（无.git，完整文件hash另存）；闭源Android/Qualcomm固件、已调优DTB及非视频专用外置模块复用，不能宣称全AOSP/闭源固件源码重编。保留screen_only、nohibernate、原SQ2触摸/音频/热管理等配置，不使用SQ1通用镜像。

产物out/clean-sq2-20261030：Image SHA12a9b126…，clean-initramfs.cpio.gz SHAca554ed4…/174836845B；ESP空间不足，因此部署10.85MB clean-addon.cpio.gz（f7a95828…），验证其叠加旧runtime+caps后与完整新包最终条目完全相同。私有SSH仍最后，未删旧镜像、未格式化。一次性入口sq2-clean-test，原默认sq2-c2-test不变，菜单备份grub.cfg.before-clean-20261030。

实机boot3150bf4f…确认新Image-clean-20261030，Android1/C2running/guardY/native-ready；新immutable运行目录/data/vendor/sq2-codec2/clean-20261030，service27161e41…、mappera64adec5…与产物匹配，v1不覆盖。四格式1080p60各600帧9979/9981/9978/9978ms，EOS/realtime/pass=true，STOP/RELEASE通过；VP9 SDR4K60既有约4.3Mbps向量600帧10095ms通过。普通APK查询四高通alias均在，VP8 4K60false/4K30与1080p60true。CPUmax1.7664/3.1488GHz、GPU661MHz、FUSE挂载、BAT1属性通过；桌面截图正常，不代替触摸/听音/长期稳定性。一次标记已消费0，下次正常重启回旧默认。HDR未测。

基线首轮在280输出时Surface取消并停止报错，用户随后确认不操作，独立重试四格式通过；错误日志保留。编译依赖与modpost纠正、objcopy改写ELF容器及历史mapper从原tar恢复的记录均在TEST_RESULTS.txt。四角色沿用vp8-limits，新增transaction仅补充菜单基线/修改/回滚副本验证，非三次硬件启动。此次构建/测试记录随本次main提交保存；私有构建产物和原始日志留在out，不纳入Git。

**2026-10-30 硬解收尾：重启回归通过，默认构建/部署已集成**：boot d9339db8…自动启动原生后端及AIDL服务，screen_only=Y，四个高通OEM alias均由普通APK发现。AVC/HEVC/VP8/VP9各600帧1080p60，输入/输出/渲染完整，EOS/realtime/pass=true，9977/9978/9978/9980ms，STOP/RELEASE通过。VP8新鲜查询1080p60/4K30=true、4K60=false；首次切用户后Activity暂未就绪的失败日志保留，未把旧结果当新结果。

当前SQ2只提升已实测runtime8b6c1f9a…+addonfb1831a9…的菜单默认项至sq2-c2-test；原正常/原Mesa入口及镜像全部保留。已核对BC711容量、ESP B027-0512、data UUID、marker、原菜单和载荷hash，sync原子替换；新菜单SHA e1fe29c57b99f8bf8d5502ee8c63b2ee5e241eda7c5bca0eafc44dc950eb4e1f，备份grub.cfg.before-default-video。一次标记已消费0，提升后未额外重启；运行的正是此前正常重启通过的同一组合。没有将含不同SQ1触摸策略的通用f749镜像覆盖当前SQ2。

build-source-boot默认包含硬解；build-source-disk生成正常启用/救援禁用菜单并在manifest写入hardware_video策略和菜单hash。deploy-experimental-usb在磁盘选择之前读取镜像内真实菜单校验，可用--require-hardware-video拒绝旧镜像；旧build-googlebook-ssd --update-boot拒绝硬解manifest，防止退回旧菜单。USB身份/原生NVMe拒绝保持。默认完整boot构建通过；本轮没有重新制作/烧录整盘镜像，公共无私有二进制的完全可复现构建仍非本次交付。

主机部署4/default-menu3/XML2/runtime-boot3/runtime-package3、OEM别名、allocator标签测试通过；P010 copy和V4L2 session ASan/UBSan测试通过（初次缺host g++，安装后重跑exit0）。证据沿用out/video-decode/vp8-limits四角色及reboot-capabilities-fresh.txt、sq2-caps-addon-boot-play-d933-r2、promote-default-result.txt。BASELINE/MODIFIED/ROLLBACK的VP8 4K60=true/false/true，最终false。Bilibili剩余权益问题用户已定位；HDR继续暂停，不宣称任意高码率/长期稳定性。此记录随硬解收尾提交，音频未跟踪文件不纳入；Git合并结果以实际提交记录为准。

**2026-10-30 默认源码构建已纳入硬解，部署/合并未完成**：新增scripts/sq2-video-boot-payload.py固定读取已审计12f1c0d5…候选，白名单导入etc/sq2-video-native、etc/sq2-c2-runtime-boot和固定hash Venus固件，helper从当前源码读取；不导入别的init/凭据。build-source-boot.py默认集成早期stage及post-boot后台hook，新增--output防覆盖。实际完整构建out/video-decode/default-build/native-combined-initramfs.cpio.gz SHA f749c61c7f7b28173497cc211ff44a4cffe1b480d26515fea6ff99bc619883b8，审计hardware_decode_included=true/password_addon_last=true/hardware_boot_tested=false。

build-source-disk.py加入视频载荷/源码XML前置校验，生成grub-hardware-video.cfg（不再直接使用旧未跟踪菜单）；正常入口自动flags、救援不启用，三个启动helper新增sq2.video=0优先退出。default_menu三测试/runtime_boot三组/boot_codec_xml两测试和diff-check通过。尚未生成新整盘镜像或部署。注意通用源码安装流程原有ghost_touch_fix=False等非视频配置属于另一路SQ1安装工作，不能将f749完整镜像直接覆盖当前SQ2而回退其触摸等已验证修复；当前SQ2应基于其现用已审计镜像仅更新视频载荷/开关来做重启回归。

下一步补部署清单对默认硬解菜单/载荷的一致性校验及实测，再以当前SQ2原非视频状态保留的独立候选启动验证VP8能力/四格式自动播放/高通别名，必要正常入口提升并保留救援；完成后审查硬解文件单独提交、合并main。当前仍未commit/merge/push，不混入音频文件。四角色沿用vp8-limits并延续ledger；原Android BASELINE4K60true/MODIFIEDfalse/ROLLBACKtrue已完成，不重复固件测试替代缺失部署验证。

**2026-10-30 用户新增交付要求：默认构建/部署、提交并合并主分支**：完成后仅提交硬解相关改动并合并main；独立音频未跟踪文件保持，不混入私有产物。已git fetch origin；当前codex/sq2-video-decode HEAD81b72a6，origin/main9f4f426，尚未提交/合并。用户新要求取代仅修VP8后收尾的旧边界，默认构建与部署验证也是完成条件。

已将实机验证VP8 XML候选整合configs/sq2-media-codecs-c2.xml；pack-sq2-c2-runtime-boot.py新增boot_codec_xml读取源码配置并固定校验5bfe57fa…，顶层boot XML独立于旧de2d runtime.tar，保持/data/vendor/sq2-codec2/v1不可变文件与hash不变。新tests/test_sq2_boot_codec_xml.py两项校验/拒绝测试、runtime_boot三组、codec_names及git diff --check通过。独立镜像out/video-decode/vp8-limits/boot-vp8-limits.cpio.gz SHA12f1c0d506d5dc654461e07bba96869ad601bf493e2b56d1ffc3ad3c64a299fc打包审计通过，私有SSH仍最后，尚未上传/启动。

下一步将运行包/原生模块/固件与早期注册、post-boot自动启动复用进scripts/build-source-boot.py默认路径，更新build-source-disk.py菜单生成/部署输入检查，正常入口启用、救援入口不启用，并保留opt-out；不削弱USB/NVMe磁盘身份保护。注意源码启动包目前源base651f…，不能简单假设包含实验native bundle；lab-start依赖etc/sq2-video-native清单及venus-5.2/venus.mbn。现build-source-disk.py直接使用out/source-boot/grub.cfg，需避免未跟踪旧菜单漏掉硬解开关。之后构建完整候选、设备安全部署/重启查询和播放回归，再提交合并；不要提前标完成。

**2026-10-30 VP8联合限制真实Android查询通过，尚未持久化**：先确认boot c1c87f3e/guardY、media PID2321、资源管理器无活动codec、设备XML原SHA1e9c4b56…。运行out/video-decode/vp8-limits/device-check.sh仅替换vendor RAM overlay的media_codecs_c2.xml并重启init服务media（不是C2/native/PAS），同一普通capsquery APK执行BASELINE/MODIFIED/ROLLBACK/FINAL：VP8 3840x2160@60依次true/false/true/false，1080p60和4K30全部true，rollback原hash和final候选5bfe57fa…hash通过。整个脚本exit0、boot不变、guardY、C2服务running，未重启固件。四阶段结果和原XML已取回vp8-limits/sq2-vp8-limits-c1c8，四角色VERIFICATION追加字面日志。当前设备overlay保留候选；本机原configs及启动镜像未改，重启会丢失修正，不能标完成。下一步只将已验证XML整合独立启动候选并验证持久化，保留原runtime二进制与userdata不可变包，不修改其清单造成安装拒绝；现pack-sq2-c2-runtime-boot.py从旧runtime.tar取XML，需要有明确且验证过的顶层XML覆盖路径。然后按用户最新收尾边界完成，不扩展HDR/新格式。

**2026-10-30 用户确认账号权益冻结，收尾边界更新**：Bilibili剩余高画质码流问题由用户确认账号权益暂时冻结；停止该方向请求/响应追踪，不绕过权益。此前4K菜单缓存修复及应用自行重检测true保留。仅本机准备过分析依赖，未安装/运行设备注入服务。用户随后表示“解决后已经可以视为完成”：按当前剩余VP8联合能力误报修正并验证后收尾，不自动扩展新格式/8K/HDR专项；不能将尚未部署的候选算完成。

VP8 XML候选out/video-decode/vp8-limits/MODIFIED_FILE.xml仅为原配置副本，增加16x16 block-size、block-count1-36864、blocks-per-second1-1036800、frame-rate1-240，原configs/sq2-media-codecs-c2.xml未改，其他codec/高通别名/HDR未改。来源caps-bcd0.json完整解析中VP8 decoder capabilities，不使用encoder值。初版证据脚本误读其他property的entries导致KeyError max/exit1，已限定capabilities后修正。同一check.py主机验证BASELINE/ROLLBACK缺联合限值exit1，MODIFIED边界算术exit0，4K30/32允许、33/60拒绝，1080p60允许；rollback hash及patch重建一致。四角色同目录DIFF_FILE.patch/VERIFICATION.txt/ROLLBACK.sh均回读。仅XML算术验证，尚非Android解析或实机播放验证。下一步验证Android读取候选后VP8 4K60=false且4K30/1080p60=true，再整合部署/持久化；现runtime/native不重跑PAS、不卸载。

**2026-10-30 HD剩余4K播放问题已缩小到解码前码流选择**：回读13:03切换日志，SetUserQn=120，但IJK收到的HEVC列表仅quality80/64/32/16，streaminfo videos=4/cur_video_id80，输出size1920x1080；并未看到4K码流进入解码器。13:06正常VIEW重新进入同BV，play view request success后仍同四档，自动选64/1280x720。不能用这轮推断4K硬解失败，也不能仅凭IJK规范化列表断言服务器没有返回4K。APK PlayViewRepository通过q13.a构造fnval，后者静态代码在isSupport4K时OR128；单类反编译部分不完整，不代表实际请求bits已确认。下一步区分原始PlayView返回与客户端过滤，限采集qn/fnval/codec/尺寸等元数据，不提取账号密钥/绕过权限。boot c1c87f3e及guardY保持，本轮未改设备配置；证据cache-refresh/stream-selection.txt、reenter-log.txt及延续VERIFICATION.txt。

**2026-10-30 Bilibili HD 4K菜单恢复，实际4K播放未确认**：tv.danmaku.bilibilihd 2.0.1/2001100，BV1NGZtBwELa同机Chrome显示4K而HD最高1080P高码率。已安装APK静态检查定位IjkOptionsHelper.isSupport4K：已有pref_key_is_ijkplayer_support_4k时永久复用BLKV缓存，只有缺键才检测HEVC 3840x2160/6Mbps。设备缓存该键false、HEVC enable最新true；普通APK能力查询HEVC4K为true且HD实际使用硬解。备份原75c4583b…到/data/local/tmp/sq2-bili-4k-cache-backup-c1c8/baseline.blkv，仅将这一键等长重命名为old__key…使缓存失效，保留其他键/账号/下载；不是强制写true。停止并重开HD后应用自行重新检测写true，截图确认4K超高清选项恢复。boot c1c87f3e/guardY，未重载固件、改启动盘或测试HDR。

证据out/video-decode/bilibilihd/DIAGNOSIS.txt及cache-refresh四角色MODIFIED_FILE.blkv/DIFF_FILE.patch/VERIFICATION.txt/ROLLBACK.sh；同一文件检查BASELINE=false/MODIFIED=MISSING_REPROBE/ROLLBACK=false均exit0，rollback hash一致，binary patch重建通过；这是副本测试不是三次实机播放。实际重检测REPROBED.blkv=true。随后点4K，日志请求qn120，但后续截图仍1080P、视频size1920x1080，伴网络-1001/timeouts；不能称4K播放通过，尚未隔离请求/流选择/自适应回退/解码重配环节。PID5794 UID1010250资源名c2.sq2.hevc.experimental，播放器遥测名称OMX.qcom.video.decoder.hevc。下一步追踪qn120实际流与回退原因，再继续联合能力限制和完整SDR适配；保留当前runtime，不重跑PAS。原始APK/账号相关日志/BLKV只留out不入Git。

**2026-10-30 runtime-v1 VP9 4K60 SDR普通应用通过**：同boot c1c87f3e/guardY，直接使用启动镜像自动运行的服务/native，不临时替换任何后端或声明。新增独立sdrnames.vp9fourk APK（UID1010261），FFprobe实际Profile0/yuv420p/3840x2160/60fps/600帧，容器平均码率4303921bps（不是高码率压力流）。普通APP报告format_supported/hardware=true/software=false，自动选c2.sq2.vp9.experimental，输出3840x2160，600输入/输出/渲染、EOS/realtime/pass true、elapsed10112ms（沿用10秒+500ms门槛），STOP_OK/RELEASE_OK。

600帧copy_sync2625536us（DMA_SYNC784940us），IMPORT_RELEASE4/50184192B、SESSION_CLOSED0；boot不变、服务继续running、未重新PAS。证据out/video-decode/vp9-4k60-runtime（fixture/APK、sq2-vp9-4k60-runtime-c1c8结果/logcat/截图、timing.txt）。此结果补齐VP9 profile0当前测试流的4K60组合，不能推出任意码率/分辨率/长期稳定性；VP8高分辨率、联合限制及未实现格式仍需继续。正常默认入口未提升，HDR继续跳过。

**2026-10-30 runtime-v1一次性启动与四格式自动播放通过**：重新核对BC711/1000215216、ESP B027-0512/marker、data UUID4f46…及loop0/p3，ESP余323916KiB足够199149026B候选。上传独立native-c2-runtime-v1.cpio.gz.new，设备SHA8b6c1f9a…/gzip通过后sync原子改名。旧菜单/env备份grub.cfg.before-runtime-v1、sq2-c2-test.env.before-runtime-v1；仅实验入口换新initrd并加sq2.video.autostart=1，菜单SHA28ab2b494d5fc5f7e0750162b5d229ee08d737ff6a7a23cf002c8b9411f200f8，默认sq2-internal保持。once1校验d762后原子设置并正常reboot；初次SSH超时后再次检查恢复，没有重复重启。

新boot c1c87f3e-d69d-4e8a-9d33-e40e4f867a5b、guardY/boot_completed1、实验服务running。早期stage全部校验OK且DATA_UNTOUCHED，后台自动lab-start清单/固件校验通过、SQ2_NATIVE_VIDEO_READY、/dev/video0存在、PAS marker为新boot；未手动装载模块/替换服务。一次标记消费为0。切换user10/唤醒后直接运行既有四APK，均hardware=true/software=false/format_supported=true、自动选中对应C2、600输入输出渲染、EOS/realtime/pass true、STOP_OK/RELEASE_OK，AVC/HEVC/VP8/VP9耗时9979/9979/9979/9976ms。此次测试不再创建/撤销临时媒体XML，也不替换运行服务；验证的是镜像自带路径。

证据out/video-decode/runtime-v1：deploy.txt、boot-stage.txt、auto-native.txt、sq2-runtime-v1-boot-play-c1c8。补充四角色MODIFIED_FILE.cfg/DIFF_FILE.patch/VERIFICATION.txt/ROLLBACK.sh对菜单副本执行同语义检查，BASELINE autostart=False/MODIFIED True/ROLLBACK False均exit0，patch重建及恢复hash一致；不是三次实机启动。原qcom-names四角色保留。默认正常镜像尚未提升，当前已启动的实验服务/native保持运行；不要重跑PAS。下一步检查正常启动提升的范围与回退并继续能力联合约束；完整能力和长期稳定性仍未完成，HDR跳过。

**2026-10-30 runtime-v1独立启动候选已打包，未上传/启动**：实查实验initramfs，早期video-codec-manifest-stage发生于userdata尚未挂载时，不能在此写/data。设备init.rc确认post-fs-data先于on boot/class_start hal。新增video-runtime-stage只在vendor RAM overlay布置不可变运行包/安装器、原AIDL default1 rc/VINTF及媒体XML；vendor wrapper在HAL启动时先运行安装器，要求实际/data ext4挂载及guardY，校验镜像内固定hash，已有v1必须逐项匹配不可变manifest/拒绝符号链接，不覆盖未知版本；缺失才暂存校验后原子改名。无格式化/主动挂载userdata。

新增video-runtime-framework-boot在retained initramfs后台等待Android boot_completed和服务running，只有experimental及新增sq2.video.autostart=1同时存在才调用既有lab-start；rescue/no flag直接退出，once目录和既有PAS guard保留，不卸载/重试固件。未增加网络、密码、休眠或全局mapper变化。pack-sq2-c2-runtime-boot.py以已审计e519实验镜像+de2d运行包为固定输入，生成out/video-decode/runtime-v1/boot-v1.cpio.gz，SHA8b6c1f9a83aee85cf86aca4342147ee50bd43fb37e17d8f3a9750a759761484b。完整解压last-entry-wins审计仅13条运行包/两个helper/init变化，其余内容保留，私有密码SSH addon仍最后；含凭据的镜像保持out，不提交。

tests/test_sq2_runtime_boot.py三组主机测试通过，实际执行安装器fresh/existing、guardN/未挂载/损坏tar/篡改既有版本/父目录symlink拒绝，以及启动helper缺flag/rescue无操作。非真实启动证明，early-stage和首次启动时序仍待设备验证。本轮没有写ESP、重启或PAS。下一步重新核对设备磁盘/ESP/当前菜单与空间，上传独立候选，实验入口添加autostart旗标并保持默认入口/回退，再一次性正常重启验证自动注册/native启动/普通APK；不能把打包通过记为重启通过。

**2026-10-30 版本化SDR运行包已实机验证，未接入开机**：新增pack-sq2-c2-runtime.py及sq2-c2-runtime-launch.c，将已测owner27161e41fb7aaba13a851ee14d9168da75568dc98b3ec21e2a14db954c602ee5、私有缓存库eb31d402…、四格式高通Alias XML和启动器打成独立tar。输入服务/库固定SHA拒绝未知版本，拒绝覆盖输出，逐项回读归档；两次构建得到相同SHA de2d825b34e09158076cf6b14f5777d0f843d6f5cc782d3961d7b2a6531b7f50。三项主机拒绝测试通过。包不含密钥/密码，不操作设备、VINTF或固件。

设备root专用/data/vendor/sq2-codec2/v1已暂存此包（父目录0700），start.sh每次校验全部载荷、guardY，启动器建立私有mount namespace、绑定包内mapper、设置MAP_CACHE=1、清除P010_DIRECT/LD_PRELOAD并exec包内service。--names-test实机8名称通过/4secure拒绝。随后同boot50ec已有native，用包内路径临时启动服务/提供XML，VP9普通APK自动选中硬件，1080p60/600输入输出渲染/9978ms/EOS/realtime/pass true、STOP_OK/RELEASE_OK；mountinfo确认来自版本化目录。原service4e7d和全局mapper c1a57082恢复/保持，XML_REMOVED，无PAS重载。

产物out/video-decode/runtime-v1/runtime.tar及同目录设备检查、播放证据；原qcom-names四角色ledger继续扩展，原host BASELINE1/MODIFIED0/ROLLBACK1不变。该包解决运行时依赖散落在临时测试文件的问题，但未修改initramfs/默认启动：下一步需把版本化运行包与早期VINTF/媒体声明、一次性native启动时序整合为新的独立启动候选并审计回滚，不能直接宣称重启后可用。现有v1目录已存在，后续必须验证/复用或换新版本，不能盲目覆盖。

**2026-10-30 四格式自动选择及HEVC一分钟循环通过**：同boot50ec、已驻留native，未重载固件。复用前轮四个普通APK/1080p60/8-bit素材，改codec=auto：AVC High、HEVC Main、VP8、VP9均报告hardware=true/software=false/format_supported=true，AUTO_SELECTED分别为对应c2.sq2.*.experimental；各600输入/输出/渲染、EOS/realtime/pass true、STOP_OK/RELEASE_OK，耗时9978/9980/9977/9979ms。显式高通别名和自动选择两条路径均有证据，但自动选择返回Codec2 canonical名，不谎称已将canonical改成OMX。

随后同会话HEVC Main以loops6重复10秒片段，3600输入/输出/渲染、59980ms、EOS/realtime/pass true，STOP_OK/RELEASE_OK，原实验服务4e7d98a7…恢复、全局mapper c1a57082…未变、XML_REMOVED、guardY。此为60秒循环/时间戳拼接验证，不是独立长片、小时级稳定性或多会话压力认证。脚本按严格结果断言退出，未放宽门槛。证据out/video-decode/qcom-sdr-matrix/sq2-qcom-auto-matrix-50ec、sq2-qcom-hevc-loop-50ec和auto-kernel.log/loop-final.txt；原qcom-names四角色ledger延续，主机名称BASELINE1/MODIFIED0/ROLLBACK1保持。接下来需补可重复部署/正常启动持久化及联合能力边界，不再重复已通过的同一短片检查充当完整适配。

**2026-10-30 四种高通原版名称的普通应用1080p60验证已齐**：同boot50ec/native驻留/guardY，不重载固件。新增独立包sdrnames.avc/hevc/vp8，实际UID1010258/1010259/1010260；FFprobe逐帧核验素材均1920x1080/60fps/yuv420p/600帧，分别AVC High、HEVC Main、VP8 profile0，签名验证及user10安装成功，保留原包和数据。首轮WSL内联shell变量传递丢失导致意外生成默认baseline素材，后续期望High文件不存在的ffprobe exit1；已改用保存的Python subprocess参数数组逐项构建，错误素材未用于本轮测试。

collector8621确认READY、独立脚本8915复用私有eb31映射缓存和新名称owner，三个普通APK显式使用OMX.qcom.video.decoder.avc/hevc/vp8：各600输入/输出/渲染、EOS/realtime/pass均true，分别9978/9979/9979ms，STOP_OK/RELEASE_OK、三个CODEC_PASS及TESTS_FINISHED。结合上轮VP9 9981ms，四种原版名称均已完成普通UID的1080p60测试APK播放；不是所有第三方APP、4K边界、长片或完整生命周期认证。恢复原service4e7d98a7…、全局mapper c1a57082…不变、XML_REMOVED，native仍驻留，禁止重复PAS。

证据out/video-decode/qcom-sdr-matrix：三个fixture.json/APK、sq2-qcom-sdr-matrix-50ec中的结果/logcat/截图及final-state。原qcom-names四角色保留并扩展ledger，主机名称BASELINE1/MODIFIED0/ROLLBACK1不变。后续优先将已验证服务/私有启动器/声明纳入可重复实验部署，补App枚举/自动选择、长播放和联合能力边界；正常启动持久化仍未完成，HDR继续跳过。

**2026-10-30 VP9高通名1080p60实时通过（私有映射缓存对照）**：同boot50ec/guardY/native驻留，不再PAS。重新核对私有库eb31d4022c97e1ac70e4b396eeb7c5599d1f904730bbdd241704a6929bc967a8与历史已测缓存版本一致，全局库c1a57082…未改。仅复制现有sq2-map-cache-private.c并将exec目标指向已验证的新命名owner，NDK构建私有启动器；不改变缓存算法、预算或同步。独立collector26307确认READY后运行相同APK/1920x1080 VP9 profile0 60fps码流/高通名称，服务26575 mountinfo确认私有bind而非全局替换。

普通UID1010257以OMX.qcom.video.decoder.vp9显式播放：600输入/输出/渲染，EOS/realtime/pass均true、9981ms、STOP_OK/RELEASE_OK。与上一轮无缓存13984ms失败保持同一门槛。600帧copy_sync由8756477us降到662502us，其中DMA_SYNC193052us、Y319645us/UV148325us；IMPORT_RELEASE count7/22020096B、SESSION_CLOSED result0。不是“零复制”，也不证明4K60/所有APP/其它codec。全局库原hash、原实验服务4e7d98a7…和临时XML清理均核验，未重启、未重载固件。证据out/video-decode/qcom-vp9-app/sq2-qcom-vp9-cache-50ec及cache-timing.txt，源原件未改。下一步把已验证私有启动条件纳入可重复部署，并依次验证AVC/HEVC/VP8高通名称的普通应用播放及更高尺寸/长播放；HDR继续跳过。

**2026-10-30 高通VP9名称普通应用真实播放：功能完成，1080p60实时性失败**：同boot50ec46b7、guardY，独立包org.surface.sq2.videotest.sdrnames安装user10，保留原测试包/数据；素材为既有VP9 profile0/yuv420p/1920x1080/60fps/600帧。独立collector19347 READY及smoke确认后本boot仅一次lab-start成功（core a5f2bcc5…/dec66e9c26a…），没有重载固件。data wrapper新命名服务+临时XML窗口，普通UID1010257显式CREATE OMX.qcom.video.decoder.vp9并START同名，实际输出1920x1080，600输入/输出/渲染、EOS true、STOP_OK/RELEASE_OK；elapsed13984ms、realtime=false、pass=false，不得记1080p60通过。截图已查看，完整彩条测试图，不代替逐像素验证。

OUTPUT_TIMING frames600：fetch694203us/map88905us/copy_sync8756477us/unmap42821us；PIXELS y5739553us/uv2875018us，输出复制是主要已观察开销。此窗口未使用旧私有map-cache wrapper，只用了data可执行路径；不能把既有4K优化结果直接移植为本轮性能结论。下一步核对既有缓存映射候选与当前后端CPU访问属性，再做同码流对照，仍保留600帧/实时门槛。原实验service4e7d98a7…恢复、临时XML移除、boot不变，native当前驻留，禁止再次lab-start/PAS。证据out/video-decode/qcom-vp9-app（stages/result/logcat/frame/native/kernel/final-state），原qcom-names四角色ledger已追加；主机名称基线1/修改0/回滚1不变。其余三个高通名称普通应用播放尚待验证，HDR仍跳过。

**2026-10-30 高通别名已通过真实MediaCodec创建/释放，尚未播放**：重新核对磁盘/ESP/菜单及镜像后，备份一次env到sq2-c2-test.env.before-sdr-names，原子设置once1；首条重启因PowerShell末尾CR导致路径reboot\r不存在（exit1），随后纠正命令正常重启，非自主崩溃。新boot50ec46b7-22ac-4efc-ae2a-853d5596237a、guardY、boot_completed1、实验AIDL服务running，一次标记已消费为0。本boot没有执行lab-start/PAS，也没有视频节点。

名称窗口a把新ELF直接放vendor，因linker namespace缺libicu.so服务停止，创建超时124；主动终止脚本并恢复原service hash4e7d98a7cbb4621cfada47bacd9f75250d542aab40d1543df369d532ff640b89。窗口b纠正为vendor shell wrapper执行data中的同一候选，并在测试前确认服务running；原XML四别名未改。真实AMediaCodec_createCodecByName对AVC/HEVC/VP8/VP9的c2.sq2旧名和OMX.qcom原版名共8项均nonnull、delete0、exit0；logcat对应8次CCodec分配正确canonical组件，非旧OEM OMX实现。无configure/start/视频帧，尚不证明普通UID应用播放、MediaCodecList别名展示或吞吐。两轮都持久日志及trap恢复，窗口b收尾原service hash一致、XML_REMOVED、PAS_NOT_ATTEMPTED、guardY，boot不变。

证据out/video-decode/qcom-names/framework-a.txt、framework-b.txt、framework-b-logcat.txt、final-state.txt；原四角色已重新打开，VERIFICATION.txt追加本轮证据，原host BASELINE1/MODIFIED0/ROLLBACK1不变。下一步复用此boot实验注册环境，先独立collector再仅一次lab-start，使用data wrapper新服务做显式高通名的普通APK SDR播放；不可重复走vendor ELF链接失败路径，不启用P010专项。

**2026-10-30 高通原版名称兼容候选**：用户要求所有已验证解码器名称与原版高通一致。实查设备vendor XML的非secure AVC/HEVC/VP8/VP9名称分别为OMX.qcom.video.decoder.avc/hevc/vp8/vp9。现有实验XML已列出四个Alias，但store此前只接受c2.sq2.*。现sq2-codecs.h增加sq2CodecAlias/sq2CodecMatches，store的Traits.aliases、createInterface、createComponent均接受对应高通名；返回接口保留Codec2 canonical名及旧测试名兼容。不新增secure/MPEG/HDR能力，不把此自研后端称为OEM OMX实现。

服务构建通过；设备独立--names-test验证四组共8个名称均能创建interface/component，4个secure名称拒绝，NAMES_PASS/exit0，无Binder注册、无固件启动、无视频解码。尚未验证MediaCodecList公开别名、createByCodecName全链路及不同APP，不能称部署完成。tests/test_sq2_codec_names.py从现有XML读取别名，交叉编译实际header解析逻辑，覆盖旧主名、别名、错codec和secure拒绝；同测试BASELINE1/MODIFIED0/ROLLBACK1，patch重建及还原字节一致。补充四角色out/video-decode/qcom-names：MODIFIED_FILE.tar、DIFF_FILE.patch、VERIFICATION.txt、ROLLBACK.sh（目标为包含两个源文件的副本目录）；原角色保留。下一步在已审计实验入口使用新service与既有XML，验证Android名称发现和显式高通名播放，再继续SDR吞吐。

**2026-10-30 用户更新目标：跳过 HDR 相关能力**：后续硬解适配继续推进 SDR，不再推进 HDR/HDR10、P010直接输出及Main10性能专项；已有实现、失败证据和回滚保留，不删除或宣称修复。10-bit与HDR不等价，本次暂停专项不代表修改已存在的profile声明或硬件能力。此条优先于下方历史记录的P010“下一步”。

**SDR接续启动审计与runner已完成**：只读重新核对BC711/1000215216、ESP B027-0512/marker SQ2-INTERNAL-512-20261027、data UUID4f46d2ec-323e-486e-b473-fc2adc0849ba、loop0 backing p3及一次env=0。设备菜单d1ae9af6…未变；取回正常镜像5ba59bd91c14208442a15792dedff236b0f1789c4d8fe10d00ee50f5e0a4953c及实验镜像e519556b0d5e5aa7ccb98772ef58fceee6fad2647f905cf85a914cab58a60b7b，完整hash/gzip及last-entry-wins比较成功。最终差异仅23个新增视频目录/文件和init的一处video-codec-manifest-stage调用，其余正常功能（含私有SSH文件）不变。证据out/video-decode/sdr-boot-audit/comparison.json；镜像仅私有out，不提交。没有写ESP、没有重启、没有PAS。

原test-c2-player.sh保持不变，副本out/video-decode/sdr-vp9-1080p60/MODIFIED_FILE.sh新增vp9-1080p60允许标签及600帧实时/EOS验收分支，不把它套用300帧判定。该目录DIFF_FILE.patch/VERIFICATION.txt/ROLLBACK.sh为补充四角色，原venus-transport及p010-backpressure角色保留；同sh -n检查BASELINE/MODIFIED/ROLLBACK均0、patch重建和还原hash一致，非硬件解码验证。下一可执行步骤：再次确认设备当前boot/一次env后，备份并原子设置既有一次性实验入口，再正常重启进入已审计镜像；启动后先核对模块hash、guard及独立collector，之后每boot仅一次lab-start。复用NV12/原allocator，不恢复P010专项。APK仍未安装，须先核对签名或沿用独立包名，不能卸载原应用数据。

本轮只读实查设备仍为5e25567a-6282-42ac-a81e-d9e5c01773b3、screen_only=Y、nohibernate，正常启动未含sq2.video.c2=experimental，无video节点及实验C2服务状态；未重新启动固件或改启动盘。SDR接续先补VP8/VP9高分辨率与吞吐验证（既有普通APK证据仅320x240），随后覆盖生命周期/长播放及正常启动集成；不能把AVC/HEVC通过替代VPx验证。MPEG和联合尺寸/帧率约束仍未完成。已用现有构建器生成VP9 SDR APK，ffprobe完整计帧确认Profile 0/yuv420p/1920x1080/60fps/600帧，签名验证通过；SHA256为88c08757af6bbc178423204a9624488dc04d1ca05970f6255f76a971fa60160b，产物out/video-decode/player-app-1920x1080-vp9-60fps/sq2-video-test.apk。尚未安装或实机播放，不代表硬件吞吐通过。

**2026-10-30 直接输出背压离线修复候选，未实机**：按固定frameworks/av源码核对C2IgbaBlockPool与C2BufferQueueBlockPool，未可用槽/等待fence可返回C2_BLOCKING，不能统一当物理ENOMEM。组件现在C2_BLOCKING映射-EAGAIN、C2_NO_MEMORY保留-ENOMEM、其余-EIO，并保留原始C2状态日志；这修复错误分类，但上轮真实C2状态未记录，不能反推六帧失败必为BLOCKING。

sq2-v4l2-session.h新增refill_逐槽状态：DQ交付后不复用客户端owner；分配新lease用局部临时对象，失败不污染槽；EAGAIN保留空槽，后续每次pump重试一次，不重复交付、不自旋、不重排入队客户端像素。QBUF成功才清空refill；收到空槽的重复DQ返回EIO；close清空待补槽。NV12固定复用模式保留，真正ENOMEM仍sticky失败，LAST/EOS不要求补回已结束槽。首次configureCapture分配遇阻塞仍fail-fast；组件尚无独立输出事件线程、长阻塞调度/outputDelay联合协商未完成，不能宣称完整背压已解决。

扩展实际头的syscall mock：NV12/P010暂阻3轮后恢复、持续阻塞时close、EAGAIN后真实ENOMEM、空槽重复DQ、阻塞时EOS、客户端保留lease及释放计数均通过，旧固定100帧/4K池预算/部分分配回收也通过。四角色out/video-decode/p010-backpressure：同新增测试、同无参数输入，BASELINE/ROLLBACK在暂阻断言处SIGABRT（Python returncode -6），MODIFIED0；patch重建和rollback源hash一致。禁core输出，不是实机三态。默认服务构建SHA55099296565fc4b731827c3b683a32f3785575c23668ae1321ec5a6b6ecb6889，未部署。本轮未操作设备、未启动固件。下一步在重新核对当前启动入口/设备状态后，通过原始C2状态与池额度日志验证直接输出，仍保留原allocator及row owner恢复路径。

**2026-10-30 P010直接输出候选：布局匹配，6帧后分配失败；用户确认随后外部重启**：独立C2分配器4K探针实测P010 3840x2160及3840x2176均25079808B、stride7680、UVoffset16711680/16711682，3840x2208为25448448B、UVoffset16957440/16957442；三尺寸10bit写入/重映射读回均通过，PROBE_EXIT0后原allocator恢复。证据out/video-decode/p010-layout-4k/live.txt。原生4K size25067520/UV7680*2176，与2160高gralloc实际对齐容量相容，但不代表任意布局可直接进口。

sq2-c2-v4l2-component.h新增默认关闭的SQ2_P010_DIRECT=1分支：fetchGraphicBlock后map等待acquire，逐项校验P010深度/采样/stride/UV偏移/plane0offset，scope结束解锁；核实两fd同inode/dev、实际容量及池预算，dup fd并用Lease持有graphic。session reuseCapture=false，DQ后交付share，再另取新lease，禁止覆盖客户端持有帧；NV12及默认copy路径保持。基线C2AllocatorGralloc源码unmap使用同步GraphicBufferMapper::unlock；实际平台/复杂fence及失败矩阵仍须继续验证，不是通用零复制保证。替换lease/close/client ownership等现有session QEMU测试通过。

显式试验owner1dbc6c64…（仅本地生成服务源码注入env，常规builder不注入）在旧b621同boot测试：自动选中并START/OUTPUT_FORMAT，C2日志OUTPUT_TIMING frames=6、copy_sync_us=0，随后SESSION_ERROR errno=-12。应用STOP_ERROR/RELEASE_AFTER_STOP_ERROR，未达到600帧或实时性。底层两次session close记录result0及capture-released；完整stage记录RUN_EXIT1、RESTORE_EXIT0和OWNER_RESTORED，旧row owner6e4209d9…已恢复。原失败码将所有fetchGraphicBlock状态折为ENOMEM，**不能据此断言物理内存耗尽**；现已补DIRECT_FETCH_FAILED真实c2_status/pool/capture_count日志，编译通过尚未实机。下一步检查C2输出池额度/等待背压及outputDelay=8与capture队列数量的关系，不能直接放大队列或重启固件重试。

SSH曾卡ECDH/关闭；后使用严格主机校验的ecdh-sha2-nistp256重新连通，但此时boot已变5e25567a-6282-42ac-a81e-d9e5c01773b3。用户明确确认“手动或其他任务重启”，不得记为自主崩溃；用户此前确认桌面触摸正常。当前guardY、无/dev/video*、实验C2服务未运行、data owner为row6e4209d9…，本轮没有在新boot装载固件。旧collector以signal结束53061ms/234records/19358B，不能称完整120秒。后续须重新核对启动入口与硬件状态，优先离线处理背压问题。

四角色补充out/video-decode/p010-direct：BASELINE/MODIFIED/ROLLBACK均实际构建并在新boot无参数本地枚举4组件exit0，rollback源字节hash一致；不注册Binder/不打开视频，不代表三次硬解。原venus-transport角色保留并扩展ledger。阶段日志/应用日志/collector已取回，完整适配目标仍未完成。

**2026-10-30 4K60 Main10真实Surface出帧，性能不通过；P010行复制有改善**：用户最新目标明确允许直接操作Surface并跳过锁屏，保留screen-only/nohibernate，不代表允许真正休眠。原93.42Mbps向量保留。首次VBV40000/40000样本因初始CPB仍有41.397495Mbps平均码率，没有部署；另存out/video-decode/main10-4k60-vbv35，VBV35000/35000、Main-tier5.1、真yuv420p10le、3840x2160/60、600帧/3B，实测36.225498Mbps。使用同签名独立org.surface.sq2.videotest.main10 APK更新user10，未更改原应用。

同boot b621a7ac/guardY、9608 owner/c859 allocator在独立watchdog窗口实测：自动选c2.sq2.hevc.experimental、hardware=true、format_supported=true，成功START/OUTPUT_FORMAT；25秒期限146输出/渲染、152输入，eos=false/realtime=false/pass=false，STOP_OK/RELEASE_OK。计时149帧copy_sync22367937us，主要问题转为输出复制性能，不是发现拒绝。门槛首次switch-user10过早返回Failed，随后确认可用时切换成功，必须以actual-user10判断，不只凭命令调用过。完整run脚本持久日志、独立恢复计时器保留。

修改sq2-c2-v4l2-component.h的deliver() P010分支，新增sq2-p010-copy.h：已验证深度/stride/capacity后，Y连续16bit及UV交错相邻2字节的布局使用现有sq2_copy_row的NEON行复制；其它有效平面布局保留逐样本fallback，不改变NV12、同步、所有权或广告。tests/sq2_p010_copy_test.cpp以参考逐样本结果比较整块内存，覆盖planar/UV/VU/列跨度、未对齐、padding及3840宽；aarch64静态/QEMU通过。新owner SHA6e4209d90a711fbb7d6ac8ecd49340156ca273e0a99d0a299b3415c1ccbe90ed构建通过。

同样APK/码流的第二窗口main10-4k60-row，collector11871/watchdog11981，普通UID1010255自动选中，真实布局rows7680/cols2,4,4/UVdelta2；25秒235输入、229输出/渲染，仍eos=false/realtime=false/pass=false，STOP_OK/RELEASE_OK、RUN_EXIT1、RESTORE_EXIT0。233帧copy_sync20875873us，其中DMA_SYNC498503us、像素复制20376543us；约87ms/帧复制仍不够60fps。晚帧截图有完整彩色渐变斜纹，非逐像素/HDR证明。不能把性能改善说成4K60通过。

两轮结束均恢复原allocator7f394d70…，row watchdog最终ORIGINAL_ALREADY_PRESENT_NO_RESTART；XML已撤销、current-user10、surfaceflinger running、boot未变、guardY，正常入口未改、未重启PAS。当前data owner保留6e4209d9…运行，backup before-row为9608。collector完整120001ms/396records/32526B取回。补充四角色out/video-decode/p010-row-copy，BASELINE/MODIFIED/ROLLBACK同无参数store本地枚举count4/exit0，恢复源hash一致；不是三次硬解。真实性能对照只有146→229帧两轮。下一步实查gralloc P010容量/UV偏移与native capture几何，设计保留所有权/栅栏的直接DMABUF输出或消除复制的路径；不可盲目取消容量检查、扩大广告或放宽realtime门槛。High-tier、其它codec/联合约束及正常启动持久化仍未完成。

**2026-10-30 Main10 4K60发现拒绝已定位到码率；纠正上一轮服务缺席判断**：重新核对kernel.log，surfaceflinger29821已在候选窗口启动，并正常访问allocator；上轮查询误用大写SurfaceFlinger造成假阴性，不能据此认定服务启动故障。init子进程崩溃在切换前已经存在。恢复原版后surfaceflinger9048的重启也与手动allocator恢复吻合。此前“框架状态异常”并非已证实根因。

本轮b窗口使用setsid+持久xtrace，collector2735/watchdog2822 READY后复用已核验9608 owner/c859 allocator，无PAS。首次稳定门槛的am get-current-user在activity暂未就绪时失败，set-e提前退出；没有重新切换allocator，另行确认system_server/surfaceflinger两PID及user10后，才继续同窗口播放器。普通应用实际得到CAPABILITY hardware=true/profiles2/format_supported=false，AUTO_SELECTED null，PLAYER_EXIT1，XML清理RESTORED，没有开始解码。随后手动恢复allocator7f…；180秒watchdog确认ORIGINAL_ALREADY_PRESENT_NO_RESTART。owner现保留9608候选running（不是默认启动部署）。

增加仅测试APK的格式/VideoCapabilities输出，原版allocator下再做发现诊断：c尝试因框架恢复后current-user0、包只安装user10而未启动，恢复user10后的d尝试实际输出INPUT_FORMAT bitrate/max-bitrate93424830、3840x2160/60/profile2/level65536；VIDEO_LIMITS bitrate=[1,40000000] size_rate=true，format_supported=false。本机FFmpeg trace_headers确认码流general_tier_flag=0、general_level_idc=153，ffprobe确认Main10/yuv420p10le/600帧、93.42483Mbps。故当前测试素材未约束码率，与Main-tier5.1声明的40Mbps不匹配；不能归为4K60硬件吞吐失败。下一步在保留原不匹配向量证据的前提下生成有VBV约束的Main-tier5.1完整4K60 Main10流，再测自动发现/Surface；High-tier/更高码率必须单独依据固件能力和实测对齐，不可直接抬高广告。普通应用Main10 4K60仍未通过。

原四角色和main10-4k60-run补充角色保留、ledger追加；该补充BASELINE/MODIFIED/ROLLBACK仍是shell语法均0及还原哈希一致，不是硬件三态对照。证据同目录capability-result.txt及b/d持久阶段日志。下一次稳定门槛必须容忍am/pidof瞬时失败、核实两个进程和user10；此为诊断脚本缺陷，不是decoder能力问题。

**2026-10-30 Main10 4K60窗口在播放前中止，已恢复**：核对安装包证书不同（原4524c6f3…、本机13879b22…），以独立包名org.surface.sq2.videotest.main10重新打包/签名并为user10安装成功，未覆盖/卸载原应用。第一次签名相对路径多一层导致exit1，修正后验证0。测试脚本副本改独立包名和hevc-main10-4k60-a，仍要求600输入/输出/渲染和realtime/EOS。原脚本未改。

同boot b621a7ac/guardY，collector29594 smoke与watchdog29685 restore-main10 main10-4k60-a READY后，备份e7da owner并临时换9608、scoped allocator c859（库22f7）。此轮新增执行了allocator restorecon；不能直接归因，但与前次成功部署不同。allocator/C2曾running，框架重启期间SurfaceFlinger缺席，临时播放器进入清理且没有result.txt，未取得任何本轮解码结果。SSH旧连接失去后续输出，但新连接一直可用；远端测试脚本已不存在，故结束本地残留SSH，不重启测试。临时XML已撤销，logcat30539已停止。不得把缺result当作4K60性能失败或固件崩溃。

已手动恢复allocator（7f394d70…hash一致），后180秒watchdog记录ORIGINAL_ALREADY_PRESENT_NO_RESTART；owner也从本轮备份恢复e7da7db7…并重启实验C2。恢复后system_server9259、surfaceflinger9048，截图确实显示桌面；boot不变、未重新PAS、未写启动盘。collector完整120000ms/415records/33110B已取回，但过滤器未保留init service/exec错误，现有日志中的Trusty/TPM循环崩溃不能归因于本次解码。证据out/video-decode/main10-4k60-run；补充四角色仅shell语法BASELINE/MODIFIED/ROLLBACK均0，回滚字节一致，非三次硬解；原四角色ledger已追加。下一步先补有界init错误采集和框架稳定门槛，查清服务窗口失败，再用新的独立label重试，不能盲目重复allocator切换。保留独立测试APK供后续使用。

**2026-10-30 本机同步后的接续检查点**：解码器分支从c19ba9b快进至65c1662，保留本机四个未跟踪音频文件，未纳入本轮。实查设备仍b621a7ac-3cc8-4d0b-8933-6faf4a7c9082、guardY/boot_completed1，allocator原版7f394d70…，C2 wrapper bdf11d60…仍running，没有重新PAS或切换服务。本机重新构建C2服务得到与检查点完全一致的9608a1f3…；本机缺失的4K60 Main10 APK已重新构建并签名验证通过，SHA256 cd406bac4017d2e32be05f5dbe0d7d0acbcbc859b99965ea77a662b0855ee4c3，600帧/Main10/yuv420p10le/3B帧。此机新生成私有测试签名，安装前须核对设备已有测试包证书，不能盲目覆盖或卸载清数据。APK尚未上传/安装/播放。

watchdog新增可选第二参数label（1..48字符，仅字母数字下划线连字符），形成同boot独立证据目录，保留无label旧路径和mkdir拒绝覆盖。tests/test_sq2_allocator_labels.py同一输入BASELINE退出1（第二个不同标签仍撞旧目录）、MODIFIED退出0（不同标签独立、重复拒绝、非法/空/过长拒绝）、ROLLBACK退出1；patch重建和还原SHA一致。原12项watchdog和5项restore保护回归通过；QEMU静态会话测试含4K P010预算/EOS/失败回收通过，非本轮硬解实测。设备仅运行新脚本observe labels-20261030-a，5秒后OBSERVE_FINISHED_NO_CHANGE、allocator哈希不变。补充证据out/video-decode/allocator-labels，原venus-transport四角色仍保留并扩展ledger。下一步核对测试包签名、加入Main10 4K60播放器独立label/600帧断言，再用新label恢复窗口部署已构建owner并做有界Surface测试；当前未证明4K60 Main10普通应用通过。

**2026-10-30 保存检查点：4K Main10独立C2六帧通过，4K60 APK构建完成未测试**：新增sq2-video-memory.h，原生P010单帧32MiB/池384MiB，NV12仍16MiB/192MiB，压缩输入和可选import/map缓存上限不变。session协商/queue及C2 dma_heap分配共用格式对应上限。P010当前不进入仅NV12的mapper缓存，不能把增大native池当作性能优化。主机ASan/UBSan测试覆盖4K P010 13槽成功/EOS、17槽超预算、单帧越界、分配第7槽失败后全部回收及NV12旧限制，原生命周期/错误/重用测试也通过。

HEVC向量脚本新增--size 320x240/1920x1080/3840x2160，P010样本检查改array避免巨量Python整数；默认小向量行为保持。组件harness按向量尺寸复制/算CRC/配置接口，4K等待60秒；builder新增--vector-dir/--output-dir保留旧产物。生成hevc-main10-4k-vector为3840x2160、真10bit、6帧无B帧，独立FFmpeg P010参考。新C2服务本地构建SHA9608a1f372af6f86676819c560e03c2cc3649b011e2eb921afB6AB1E6C8A5E5E（大小写无关），**尚未替换运行owner e7da7db7…**；默认AVC harness新源码编译通过，尚未新实机回归。

同boot b621a7ac…驻留native，仅运行独立/data/local/tmp/sq2-c2-main10-4k-test --frames，没有重新启动PAS或切换allocator。4K六帧Y/UV CRC全匹配，C2_RESULT done7/mask3f/eos1/errors0，C2_STOP0/C2_RELEASE0/CLIENT_BUFFERS_RELEASED、TEST_EXIT0。结果保存在out/video-decode/hevc-main10-4k-vector/sq2-c2-main10-4k-result.txt。collector20204有smoke并完整结束120000ms/234records/15846B，已取回同目录。此为root独立C2+gralloc大帧正确性，不是普通Surface4K10bit或60fps性能证明。

4K60 Main10普通APK已构建完成（exec78479正常exit0，无待轮询任务），目录out/video-decode/player-app-3840x2160-hevc-main10-60fps；600帧/真yuv420p10le/Main10/3B帧。尚未上传安装，当前设备测试APK仍Main8 4K60。下一步先备份并部署新owner、为本boot新的allocator测试窗口加唯一label和固定恢复保护，再测4K60 Main10普通应用；已有restore-main10-b621…目录已使用，不得覆盖。当前allocator原版7f394d70…、C2 wrapper bdf11d60…/owner e7da…、native驻留，正常入口未改、临时XML已撤销。其余格式/联合约束/持久化仍待完成。

**2026-10-30 Main10普通应用通过，Main8 4K60回归通过**：重新核对BC711/1000215216、ESP B027-0512/marker、data UUID4f46d2ec…、loop0 p3/root来源、菜单d1ae9af6…和p010-stride镜像e519556b…后，仅原子替换一次性env（d762d0a0…），旧env备份before-main10-scoped，主动Android reboot。新boot **b621a7ac-3cc8-4d0b-8933-6faf4a7c9082**，guardY/boot_completed1，core a5f2bcc5…/dec66e9c26a…匹配。首次用户切换期间SSH短暂断开，同boot恢复；collector6338 smoke确认后本boot仅一次lab-start成功。不得重跑PAS。

data owner e7da7db7…、private-probe bdf11d60…、C2 mapper eb31d402…哈希均核对；将RAM /vendor/bin/sq2-c2-experimental替换为private-probe，chmod755/chcon hal_codec2_default_exec，rc仍root/kernel域，重启服务running。正式allocator原exe7f394d70…另存backup，候选lib22f7e4c2…及scoped launcher c859a014…恢复到RAM；新增watchdog restore-main10模式180秒、新hash固定、独立目录，三模式12项主机测试通过。watchdog14886 READY后才切换allocator，默认服务found，C2仍running；新应用collector18663另行启动且smoke落盘。

普通UID1010254运行hevc-main10-small，自动选c2.sq2.hevc.experimental，hardware=true/profiles2/format_supported=true，profile2/Main10、320x240/30/300帧。结果 **300输入/输出/渲染、9962ms、EOS/realtime/pass=true、STOP_OK/RELEASE_OK/RESTORED**。晚帧截图人工查看有彩色渐变斜纹测试图案；不是HDR或逐像素证明。完整目录out/video-decode/hevc-main10-vector/sq2-player-hevc-main10-small-b621a7ac-3cc8-4d0b-8933-6faf4a7c9082已取回。这是首次普通应用Surface Main10链路通过，不能扩展为4K10bit能力。

同一候选仍运行期间安装已有HEVC Main8 3840x2160/60 APK，hevc-4k60普通应用自动选同组件，**600输入/输出/渲染、9978ms、EOS/realtime/pass=true、STOP_OK/RELEASE_OK/RESTORED**；结果/截图目录同上hevc-4k60已取回。当前测试APK为Main8 4K60。AVC在本轮组合下尚未新回归。

boot collector完整KMSG_END120000ms/366records/27201B，app collector完整KMSG_END120000ms/287records/20647B，覆盖两次播放结束，均已取回hevc-main10-vector/sq2-main10-scoped-*；app未见venus/HFI/BUG/Oops/panic/OVERRUN记录，但有audit限流，不能称全量无错误。临时XML已撤销。180秒watchdog最终ORIGINAL_RESTORED/RESTART_REQUESTED，原allocator7f394d70…running/default found，boot不变；用户10唤醒/跳过锁屏、timeout2147483646恢复。C2实验wrapper和native仍驻留，allocator已回原版，因此再次Main10 Surface测试前需重新布置已验证候选和新的独立回退窗口，不可直接重复本boot同名证据目录。

剩余范围：Main10 4K/60端到端内存预算（native4K P01025067520B高于当前16MiB限制）、VP9 profile2、MPEG适配、联合尺寸/吞吐约束、AVC回归及正常启动持久化仍未完成。不得把本轮小尺寸Main10+8bit4K60拼成4K60 Main10通过。

**2026-10-30 收窄LD路径后真实allocator服务注册通过，定时恢复通过**：watchdog新增restore-scoped模式，仅识别新c859a014…，独立run目录保留旧证据。两模式恢复/原版/no-unknown/坏备份8项主机检查通过；Android手动restore也增加新hash白名单。新sample-sq2-allocator-start.sh从initramfs做三次有界只读PID/exe/maps/service check取证。

重核boot6e42d2b5…/guardY/boot_completed1/原版exe及backup7f394d70…/candidate22f7e4c2…后上传新launcher并校验c859a014…；logger17008、watchdog17009、sampler17010先启动，READY及活跃确认后原子换入launcher并restart。快照第一项为旧PID32438，第二/三项为候选PID17456，exe为sq2-allocator-original，maps明确候选专用目录libminigbm与/system/lib64/libbinder{,_ndk}，两次IAllocator/default均found；SSH实际可连、服务running/boot_completed1。由此验证本轮收窄路径解决了默认服务注册问题，不是Main10呈现或长期运行证明。

45秒后定时器仍按计划恢复原版，日志ORIGINAL_RESTORED/RESTART_REQUESTED；boot未变，原exe完整SHA7f394d70…，默认服务found/running。用户10唤醒/跳过锁屏、timeout2147483646再次恢复。logger17008已无/proc项，快照和logcat已取回out/video-decode/minigbm-p010/allocator-scoped-*。两轮logcat实际均达到4194304字节限制而截断，**并非完整90秒窗口**；旧retry覆盖到回退后，新scoped覆盖候选启动及约14秒后。后续应区分窗口timeout与文件限额，并保留独立快照/回退证据；threadpool0警告在成功候选中仍出现，不能单凭该警告判断卡死。

当前运行原版分配器，candidate库及backup在RAM vendor保留；新launcher在data。正常入口无驻留native，下一步进入已验证的p010-stride实验入口（必须重新核对ESP身份/一次性env），恢复新C2 owner及scoped allocator，进行Main10普通APK，并回归Main8/AVC。不把注册通过当作Main10普通应用成功，也不取消P010/4K内存上限问题。正常启动持久化仍未完成。

**2026-10-30 有界真实服务诊断自动恢复；发现LD路径改变Binder依赖**：新增sq2-allocator-watchdog.sh（initramfs根、root cgroup、setsid）及实际脚本隔离恢复测试test_sq2_allocator_watchdog.py。root/Android共用mnt namespace但根inode不同，已修正前置检查；observe空跑PID26321完成，无框架重启。主机restore/unchanged/unknown/bad-backup四项通过。正式试验先备份原exe7f394d70…到vendor/bin/hw/sq2-allocator-original，并准备candidate22f7e4c2…；独立logcat进程30059（timeout90、ulimit-f8192）及watchdog30060（45秒）确认活跃/READY后，才原子切换旧launcher249da7ca…并ctl.restart。

再次断网，但45秒后watchdog在Android框架之外恢复原exe并请求restart，SSH恢复；日志明确ORIGINAL_RESTORED/RESTART_REQUESTED，boot仍6e42d2b5…，新原版allocator PID32438/running/hash7f394d70…，boot_completed1。用户10已切回、唤醒/跳过锁屏、timeout2147483646恢复。没有主动重启整板。watchdog已结束，logger30059的/proc条目也已不存在；90秒日志完整取回out/video-decode/minigbm-p010/allocator-retry-logcat.txt。原始日志不得提交。

日志候选PID30493已经打印Minigbm AIDL allocator starting up，随后libbinder.BackendUnifiedServiceManager在IAllocator/default路径报threadpool0；SF持续等待默认服务直到回退。未见候选明确链接失败，不能再归类为未能执行main。关键对照：正常allocator maps的Binder来自/system/lib64，旧launcher把/vendor/lib64:/system/lib64加入LD_LIBRARY_PATH后，--list选了/vendor/lib64/libbinder.so并改变namespace共享依赖；可能导致注册路径异常，但尚未证明唯一因果。只设候选目录的--list恢复system Binder，同时仍选candidate+vendor donor。

已将sq2-allocator-launch.c收窄LD_LIBRARY_PATH为/vendor/lib64/sq2-p010-allocator；新编译allocator-launch-scoped SHA c859a01459841b8e7a8caff77981dd5d4bd30f13311c0f50e12bc07e6c7416b1，尚未上传或真实服务验证。旧launcher产物保留用于取证，禁止再次作为新候选使用。下一轮先给恢复脚本/定时器添加新hash的明确白名单和相应测试，采用新的唯一run目录（本boot restore目录已经存在，当前脚本会拒绝重跑），验证新候选后才有界切换。不能绕过重跑保护或让timer误拒新hash。Main10应用仍未通过。

**2026-10-30 原launcher隔离执行链通过**：新增 sq2-allocator-isolated-probe.c，root创建独立mount namespace并递归MS_PRIVATE，将data中的isolated-hw绑定到该进程的/vendor/bin/hw，将候选目录绑定到/vendor/lib64/sq2-p010-allocator。随后降为uid1000/gid1003/supp1026，设置有效/许可/继承及ambient SYS_NICE，执行原SHA249da7ca…launcher；原launcher设置LD_LIBRARY_PATH并清LD_PRELOAD，执行对应backup路径的诊断ELF。15秒alarm继承至exec，不注册默认服务。未复制SELinux域/完整cap bounding/task profile/init管理语义，不应称为init启动等价。

prepare脚本新增--needed-stub，仅对私有诊断ELF把无版本依赖的libm.so改为libs.so，stub自身DT_NEEDED保留libm；注册导入仍是不可转发的SQ2专用符号。有效诊断ELF SHA3925d62d98df3776b1717f7fc602a1f8261e3dd80a7d6f2cd64010ce25a5a75d。第一版改liblog的方式破坏verneed而被linker拒绝，8f13b3…产物废弃，不是实际allocator故障证据。正式helper/有效副本执行输出ISOLATED_LAUNCH、REGISTRATION_INTERCEPTED valid1/flags1/uid1000、exit0；allocator/SF/system_server仍2016/2049/2327，原服务exe仍7f394d70…。全局vendor只新增一个空的sq2-p010-allocator挂载点目录，库/可执行文件替换全在子进程私有namespace，退出即消失；data staging保留。

该结果没有复现原服务失败。后续若做真实注册/服务切换，必须先补initramfs侧独立定时恢复及完整stderr/logcat采集，回退不得依赖Android框架或Wi-Fi；不要把本次诊断通过作为直接重试实时切换的依据。Main10普通应用仍未通过，正常启动能力不变。

**2026-10-30 实际 allocator 注册前诊断通过，未触碰正式服务**：先取回并验证原exe7f394d70…，readelf确认实际版本唯一注册导入为 AServiceManager_addServiceWithFlags@LIBBINDER_NDK34（与参考源码旧API有差异）。新增 prepare-sq2-allocator-registration-probe.py，固定原exe哈希，只在私有副本将该导入等长填充改名为 SQ2_allocator_probe_registration；拒绝未知输入/覆盖输出。产物SHA ecb8480ca33c834d16a69d4e98bf347ff5ff210b0cf6ad4f137f2a7a61cd4f30，readelf核验原注册导入已不存在。

probe/video/sq2-allocator-registration-probe.c提供专用符号，精确检查非空binder和默认allocator实例名，打印后_exit，不调用/转发真实注册。DSO以LIBBINDER_NDK34版本导出，仅用于诊断副本。实机没有LD_PRELOAD时副本明确CANNOT LINK/exit1（无法意外落回真实注册）；加载stub后，原版和候选22f7e4c2…两套LD_LIBRARY_PATH均REGISTRATION_INTERCEPTED valid1/flags1/uid0/exit0。每项timeout15秒，原allocator/SF/system_server PID2016/2049/2327前后不变。未替换正常HAL、未重启框架、没有新native/PAS操作。

这验证实际程序在root直接启动时已完成注册前初始化，不等同init服务的UID/能力/SELinux/命名空间、原launcher exec链或真实注册成功。候选不能因此发布。下一步复核原launcher路径与init环境；仍须保留不能注册的诊断副本，不得把正常原exe当作该副本执行。Main10 Surface问题尚未解决。

**2026-10-30 分配器注册前 driver 初始化独立对照通过**：当前 boot6e42d2b5…，正常 allocator/SF/system_server 为2016/2049/2327。新增 probe/video/sq2-gralloc-init-probe.cpp，以 RTLD_NOW dlopen 指定库并通过准确导出 _ZN19cros_gralloc_driver12get_instanceEv 调用 donor 的 std::shared_ptr<cros_gralloc_driver> 返回接口；只使用不完整类型/shared_ptr 控制块，不读私有对象布局、不注册 Binder、不运行 allocator main。15秒 alarm，释放客户端引用，保留 DSO 到进程退出以匹配 donor 的静态 singleton 生命周期。

原版与 candidate22f7e4c2…分别在 root 下 GRALLOC_INIT_OK=1/exit0；随后 --system 在 dlopen 前降为 uid1000/gid1003/supplementary1026（实读正式进程），两者同样成功。未保留 SYS_NICE、未复制 init SELinux 域/命名空间/exec 链，因此不是完整服务启动等价测试。正式三进程 PID 前后不变，未替换图形服务。依赖能枚举、get_instance 能初始化，仍不能证明启动 launcher/实际服务注册或 Surface 呈现成功；下一步需取实际 exec/注册前诊断，避免重复实时替换。探针产物和设备路径均在 minigbm-p010/sq2-p010-gralloc 下。

**2026-10-30 黑屏反馈后已恢复正常启动，取回切换日志**：用户确认此前切换后卡住或黑屏。随后只读连接成功，boot ID 为 6e42d2b5-99ab-4d9b-9769-92e6d3dc5d03，boot_completed=1，screen_only=Y，cmdline 保留 nohibernate、正常 closed Vulkan 入口；本轮未发送 reboot。allocator 服务 running，原 exe 完整 SHA 7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da，全局 mapper c1a5708286a42723d1ea8640d08e2486948c382a1531a08173cee1f77a25c283。RAM 候选已回退，无需运行恢复脚本。开始当前用户为0，已将用户0和10的 screen_off_timeout 设为2147483646，并 switch-user 10、WAKEUP、dismiss-keyguard；实读当前用户10。未修改凭据或休眠保护。

旧持久 collector 已取回 out/video-decode/minigbm-p010/allocator-switch-kernel.log，104515字节，KMSG_END timeout/120000ms/597records/104434payload bytes。日志证实 allocator onrestart 执行了 restart surfaceflinger，原 SF1998 被杀，新 SF29784 启动，zygote也重启；随后持续约两分钟无法找到 IAllocator/default，Wi-Fi 记录本机主动 DEAUTH_LEAVING。因此先前命令至少到达 allocator restart，不能继续写成全部步骤均未知；候选实际加载库/失败原因仍缺证。持续的内核collector也表明这不是已证实的瞬间内核硬挂。audit 丢失/限流存在，不能以没有错误记录排除故障。

不重试实时服务替换。只执行 linker64 --list（不运行服务 main）：原 allocator 配合 data 目录候选 LD_LIBRARY_PATH 可枚举到 candidate、vendor donor 及完整依赖，launcher 自身依赖也可枚举；这不是 init UID/能力/命名空间下的实际启动验证，也不能证明符号重定位或注册通过。当前32个 tombstone 的 Cmdline 均为已有 init/trusty/gscd，没有保留 allocator tombstone；不能据此排除先前 allocator 崩溃。下一步需设计不触碰正在运行默认服务的启动/注册前诊断并保留 stderr，定位真实服务启动失败，再安排受控呈现测试。Main10独立分配与原生六帧结论保留，普通应用及4K10bit仍未通过。

**2026-10-30 分配器恢复工具就绪，设备仍不可达**：后续多次 SSH 到 192.168.8.194 超时，工作站邻居表该地址为 Incomplete，无法确认当前 boot、服务状态或候选是否落地；不据此推断整机崩溃，也不扫描其它设备冒认目标。上一切换 session98048 已终止，不存在待轮询的运行任务。

新增 `probe/video/restore-sq2-allocator.sh`，仅在 root、screen_only=Y、目标精确匹配已知 launcher 249da7ca…且备份匹配原版 7f394d70…时，复制到临时文件、复核完整 SHA、sync、原子恢复目标，再重启 vendor.graphics.allocator。目标已经是原版时不重启；未知目标或损坏备份拒绝。全局 mapper 不动。`tests/test_sq2_allocator_restore.py` 在隔离目录执行实际脚本，restore/already/unknown/bad-backup 四项通过。这是主机逻辑验证，脚本尚未上传或实机执行。

恢复连接后先读取 boot ID、服务状态和目标/备份哈希及进程 maps，再决定是否执行回退；若已经正常重启，RAM 候选可能已消失，不应重复切换。还需核对当前用户的 screen_off_timeout（上次已验证为 2147483646），保留手动关屏及内核 guard。Main10 普通应用仍未通过，不能把离线回退验证当成硬解完成。

**2026-10-30 allocator服务候选切换命令未返回，SSH暂不可达**：实际init rc服务vendor.graphics.allocator，user system/group graphics drmrpc，seclabel hal_graphics_allocator_default，只有SYS_NICE，onrestart restart surfaceflinger。因此未用需要SYS_ADMIN的私有mount方案，改为专用原生launcher只对exec的allocator设置LD_LIBRARY_PATH=/vendor/lib64/sq2-p010-allocator:/vendor/lib64:/system/lib64，unset LD_PRELOAD/SQ2_VIDEO_MAP_CACHE；全局库路径不替换。原exe SHA7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da，全局lib c1a57082…，exe/lib标签均tmpfs。操作前SF1998/system_server2140，boot38e9e63c…。

launcher源码probe/video/sq2-allocator-launch.c、产物minigbm-p010/allocator-launch SHA249da7cae6e999059e0e03a14728b7249681a9c2ecbf57751e8ded4ed276e02e，上传data/sq2-p010-gralloc。独立collector sq2-p010-allocator-switch-kernel.log smoke和candidate22f7e4c2…校验输出已收到。随后发出的set-e链计划：备份原exe到/vendor/bin/hw/sq2-allocator-original及/data/local/tmp/sq2-p010-gralloc/allocator-original；在RAM vendor/lib64/sq2-p010-allocator放candidate；原子替换服务exe为launcher；ctl.restart vendor.graphics.allocator；sleep4后输出服务/PIDs。**exec session98048随后以Connection reset/exit1结束，仅收到smoke/hash，后续步骤完成情况未知。** 新SSH连接超时，尚无服务/PID/画面确认。不要重发切换命令、不要擅自重启；98048已终止，先读设备状态。已询问用户当前屏幕状态，待答。

恢复后先核对实际exe/backup/服务pid的maps，确认是否只allocator加载新库（SF应仍全局c1a57082…），以及bootID判断框架重启还是板重启。若服务失败，核验原exe备份hash后原子恢复服务路径并ctl.restart vendor.graphics.allocator；全局库本来未变，无需替换全局mapper。重启会清RAM vendor候选，但本轮未主动发reboot。不得把命令发出当成部署成功或把网络断开解释成Main10硬件不支持。

**2026-10-30 P010显示usage独立分配候选通过，未替换全局服务**：新增只读sq2-drm-plane-formats.c，O_RDONLY打开card0、仅fd级UNIVERSAL_PLANES/GETPLANERESOURCES/GETPLANE，无master/framebuffer/modeset。实际四个视频plane33/39/45/51均公布P010，另外四个RGB plane没有；固定内核dpu_plane_format_mod_supported接受linear。不能把格式枚举当成实际scanout呈现通过。

minigbm固定源msm.c SHA991257af…只在SC_7280给P010添加SCANOUT；新增sq2-minigbm-p010.h以DRM_MSM_GET_PARAM读真实CHIP_ID，仅0x06080001允许增加LINEAR_METADATA/P010的BO_USE_SCANOUT，不定义SC7280、不改其它格式/UBWC/GPU身份。prepare-sq2-minigbm-map-cache.py --p010-scanout生成独立/root/.cache/sq2-video-minigbm-p010，保留原映射缓存补丁；builder新增SQ2_MINIGBM_STAGE/OUT变量，原默认路径保持。候选out/video-decode/minigbm-p010/libminigbm_gralloc.so SHA22f7e4c2bd76338519964d3e67d2392e4e7e35aeb5904fb2b6c0fa393603ce2c。

新增sq2-minigbm-p010-probe.c独立dlopen指定库，renderD128上请求SW_READ/WRITE_OFTEN+TEXTURE+SCANOUT P010320x240，仅分配/释放无渲染命令。boot38e9e63c…旧全局库得到SUPPORTED0/ALLOCATED0/exit3，新候选SUPPORTED1/ALLOCATED1/RELEASED/exit0。原始结果与DRM格式清单取回out/video-decode/minigbm-p010。设备候选及allocate-probe在/data/local/tmp/sq2-p010-gralloc；未bind、未替换/重启allocator或SurfaceFlinger，默认服务/全局mapper仍原样。

下一步检查allocator init rc/实际库与donor依赖，准备仅allocator进程私有mount namespace覆盖的候选启动及回退，保持系统其它图形进程库不变，然后Main10普通APK重新测试。已有C2私有mapper仍eb31d402…，owner e7da…，XML已撤销；切勿把独立分配成功称为Surface或Main10播放通过。

**2026-10-30 Main10普通应用失败定位到Surface分配usage**：boot38e9e63c…。重核旧启动服务4e7d98a7…、data owner67b326ca…、private-probe bdf11d60…/mapper eb31d402…后，备份data/sq2-c2-owner-before-main10（67b3）及sq2-c2-service-before-main10（4e7d），停止服务、以e7da7db7d9442a21105cb3c8044dae128365ba35ee77d41a5ec1e21068a535b9替换data owner，并将private-probe复制到RAM vendor服务入口；恢复标签/start，running。全局mapper未变。新服务只RAM，正常镜像未改。

安装Main10小APK，runner新增hevc-main10-small，独立collector smoke后临时XML运行。普通UID1010254自动选c2.sq2.hevc.experimental、profiles2/format_supported=true、Main10 profile2/CSD2387；原生捕获11buffer成功，但无输出即MediaCodec NO_MEMORY，STOP_ERROR/RELEASE_AFTER_STOP_ERROR；runner完成RESTORED、XML撤销，服务仍running。完整目录out/video-decode/hevc-main10-vector/sq2-player-hevc-main10-small-38e9e63c-19ab-4f53-95fb-25413f559844。

完整logcat明确：全局android.hardware.graphics.allocator-service.minigbm拒绝YCBCR_P010/DRM_P010 usage0x933（CPU read/write + GPU_TEXTURE + COMPOSER_OVERLAY）；GraphicBufferAllocator alloc320x240/format54失败，C2IgbaBuffer allocation transaction failed。不是native解码失败，也不是默认CPU-only allocator不支持P010。私有mapper仅影响服务import，不改变全局allocator进程。源码msm.c P010在texture_source_formats中，BO_USE_SCANOUT只在SC_7280编译条件给P010增加；当前SQ2不能盲开SC7280或冒充其它GPU。下一步核对实际DRM平面/P010与客户端合成分配策略，或以受控C2非Surface池交付验证真实GPU导入；保持既有图形fix，不因usage拒绝判整个Main10不支持。大段init子进程crash日志是已有trusty服务噪声，未据此断言板重启。

全量logcat和collector已取回out（含原始日志不提交）；当前collector完整收尾需核对。当前APK为Main10、owner为e7da、wrapper bdf11d60，XML已撤销。普通应用未通过，界面/flush/4K10bit尚待继续；不发布永久能力。

**2026-10-30 Main10配置自动选择P010并通过六帧**：Sq2DecodeInterface新增HEVC Main10 profile与由profile计算的output pixel format（Main10=0x36，Main=0x23；其它codec常量0x23）。组件移除显式构造format覆盖，每次打开会话查询picture-size+pixel-format，据此选择native P010/NV12；因此flush后重新打开也重新查询，但尚未实测flush。独立harness通过config_vb配置Main10并确认P010，不再靠构造参数绕过接口。

设备参数测试Main10->Main切换pixel随之变化，VPx拒绝不支持profile/AVC与尺寸/pool检查通过，参数数11。随后普通组件Main10协商测试MAIN10_PROFILE_SELECTS_P010、done7/mask3f/eos1/errors0、六CRC匹配、停止释放0/客户端buffers释放、exit0。两份日志已取回hevc-main10-vector/sq2-c2-main10-{parameters,negotiation}-result.txt；仍独立root harness，不是普通APK/Surface。

新store本地产物SHAe7da7db7d9442a21105cb3c8044dae128365ba35ee77d41a5ec1e21068a535b9（未上传/替换服务）。build-sq2-video-app.sh新增hevc-main10：真10bit geq源、libx265 Main10/3B帧，生成player-app-hevc-main10/sq2-video-test.apk；vector.json核验Main 10/yuv420p10le/300帧（320x240/30），尚未安装。下一步先备份/恢复新boot的私有wrapper+owner并临时XML测试普通APK，再Main8/AVC回归；不可假定此前RAM wrapper仍存在。Main10小尺寸路径已验证，但当前16MiB预算不支持4K10bit，未发布正常启动能力。接口候选现在含Main10，上线前仍须处理尺寸联合约束/预算，不能把本地编译当成全尺寸支持。

**2026-10-30 Android C2 Main10六帧像素通过，AVC回归通过**：Sq2C2V4L2Component新增末尾显式outputFormat参数（缺省NV12，注册store旧调用不变），传给native session。P010输出使用HAL0x36，验证实际geometry/fourcc、2字节行宽/有效payload、gralloc10bit/16bit/shift6/非大端/采样和步长；按源stride和目标rowInc/colInc逐sample复制16bit，不做8bit缩减。旧8bit优化复制路径保留；16MiB单帧/192MiB预算未扩大，4K10bit仍未支持。

build-sq2-c2-component-test.py新增--codec hevc-main10，使用已验证原生Main10向量并显式P010输出；harness按C2平面语义重组P010，与独立参考六帧Y/UV CRC比较。boot38e9e63c…运行/data/local/tmp/sq2-c2-main10-test --frames：done7/mask3f/eos1/errors0，六帧匹配，queue/stop/release0、CLIENT_BUFFERS_RELEASED、exit0。按相同新组件源码重编AVC默认NV12回归也done7/mask3f/eos1/errors0、停止/释放0、exit0。完整结果已取回out/video-decode/hevc-main10-vector/sq2-c2-main10-frames-result.txt和sq2-c2-p010-avc-result.txt。

**这是独立root C2组件+真实硬件+真实gralloc验证，不是普通UID MediaCodec/Surface/Main10能力声明。** 当前接口仍只广告HEVC Main，测试通过构造参数明确选择P010，不假称完成profile协商；运行store未替换。下一步将Main10参数与输出pixel format/自动选择贯通，验证配置/flush/重建，再普通APK；VP9 profile2、4K10bit预算、MPEG及持久化仍开放。没有重启/重初始化native或更改正常入口。

**2026-10-30 Android P010 allocator实机通过**：sq2-c2-allocator-test.cpp新增显式--p010（默认0x23旧分支保持），通过平台Codec2 allocator分配HAL_PIXEL_FORMAT_YCBCR_P010=0x36，验证3平面/10bit/allocated16/rightShift6/非大端/行列步长/采样边界，然后写带10bit低位的不同YUV图案、unmap/remap并逐sample读回。build-sq2-c2-component-test.py新增--allocator-test独立输出allocator-check/interface-test，固定头与库校验保持，不覆盖组件产物。

设备boot38e9e63c… Android全局原mapper（非私有缓存wrapper）运行exit0，320x240/256/288均P010_10BIT_REMAP_READBACK_OK/RELEASE_DONE。实际布局：Y row768/col2，U/V row768/col4；10/16/shift6；240和256高UVoffset196608/196610、分配307200B；288高UVoffset221184/221186、分配344064B。因此C2图像stride768与native P010 stride640不同，后续必须逐行/按plane布局复制，不能整块memcpy。只验证分配映射与读回，不是C2解码/Surface显示/Main10广告。

探针/data/local/tmp/sq2-c2-p010-allocator，结果sq2-p010-allocator-result.txt已取回out/video-decode/hevc-main10-vector。原生后端未新增解码命令、未重启PAS。下一步给Sq2C2V4L2Component加明确P010输出选择和16bit样本复制，先独立Main10 C2六帧参考harness再启普通应用；注册store/接口目前仍8bit，不提前广告。当前未提交变更继续保留。

**2026-10-30 Main10原生六帧通过，NV12回归通过**：新boot38e9e63c-19ab-4f53-95fb-25413f559844/boot_completed1，core a5f2bcc5…/dec66e9c26a…匹配；独立sq2-p010-stride-boot-kernel.log smoke后本boot仅一次lab-start成功。user10/唤醒/跳过锁屏/timeout2147483646已恢复，guard由lab-start校验Y。

原decode-test-crop --dmabuf（不dump、不改严格检查）Main10六帧Y/UV CRC全匹配mask3f，6输入归还、drain1/LAST1/ret0，双STREAMOFF0/CLOSE_DONE，TEST_EXIT0。捕获P010 320x256、stride640、crop320x240，有效payload245760；原较大sizeimage294912保留。随后新编译decode-test-nv12对原8bit HEVC六帧同样mask3f、全部输入/EOS/停止/关闭通过，exit0。两套结果/native-start和当时collector已取回out/video-decode/hevc-main10-vector/sq2-p010-stride-*.txt/log；collector完整收尾还需取回。**只证明原生320x240 Main10完整解码与8bit回归，不证明AndroidMain10、4K10bit、长片或完整能力对齐。** 当前Android服务仍镜像旧版，67b3owner没有重启后重新部署；下一步验证C2 P010分配布局，再接入10bit图像复制/参数与普通应用。现有allocator探针probe/video/sq2-c2-allocator-test.cpp只测0x23，需要独立新增P010分支；本地minigbm msm.c已有P010支持，实际allocator仍待测。

**2026-10-30 P010行距修正一次性重启已发出**：新增pack revision p010-stride固定a5f2bcc5…/66e9c26a…模块；镜像out/video-decode/boot-candidate/capabilities-p010-stride.cpio.gz SHAe519556b0d5e5aa7ccb98772ef58fceee6fad2647f905cf85a914cab58a60b7b。打包审计仅core/dec/manifest改变且密码addon最后。boot31cf07bf…期间重核BC711/ESP UUID/marker/data UUID/root backing、once0、ESP余517660KiB后上传；设备SHA及gzip -t通过，sync后原子改名native-c2-capabilities-p010-stride.cpio.gz。菜单备份grub.cfg.before-p010-stride（旧7c03e5a6…），新菜单d1ae9af6f9dc4c7e54d8fd37fda108e36f130393394f303b72661f95ad156e06，仅实验initrd路径变化。备份once0并核验d762d0a0…后原子设once1，sync并主动Android reboot，session36909，等待新boot确认，不重复重启。

上一轮两个collector完整取回hevc-main10-vector，layout日志KMSG_END timeout120013ms/290records/20130bytes。新编译8bit HEVC runner decode-test-nv12已上传/data/local/tmp，Main10用已在设备的decode-test-crop（不dump），两者严格CRC。下一步新boot核验哈希/guard/boot_completed，独立collector后只一次lab-start，然后Main10六帧完整及NV12回归。Android10bit尚未接入；本地minigbm msm.c已经列出P010 texture-source/分配支持，仍需实机allocator/C2布局检查。

**2026-10-30 P010首帧逐字节参考匹配，行距修正候选已编译**：boot仍31cf07bf…，已有日志无venus会话错误、上轮双STREAMOFF/CLOSE返回，未重启PAS。在独立新collector smoke后用新增--dump-first NEW_FILE /dev/video0做一次受限诊断：最多1MiB、O_EXCL/O_NOFOLLOW、DMA_READ同步、fsync，仅保存首个有效payload，不改变原geometry/CRC判定。实际保存245760字节，仍short-frame/exit1、双STREAMOFF0/CLOSE_DONE。原始payload及result已取回hevc-main10-vector；按stride640/Yscan256提取可见Y/UV，与独立FFmpeg ref.p010第0帧**全部230400字节相同**，CRC582aa82b/350913a6；stride640/scan240不匹配，768布局无法放入有效payload。结果含原始/参考SHA在p010-first-layout.json；只是首帧，不是六帧/EOS或AndroidMain10通过。

本地固定kernel的drivers/gpu/drm/msm/disp/dpu1/msm_media_info.h SHA88ee1ae38078035cc96ee064e370ae4389333f6c0ce3135a83d9d0c87a543a9a，VENUS_Y_STRIDE/UV_STRIDE线性P010为ALIGN(width*2,128)，P010_UBWC才256；Yscan32/UVscan16。prepare脚本已将暴露仅SQ2的P010 bytesperline修正为128对齐，分配sizeimage仍保守沿用原256对齐预算，不改其他格式。新stage/root/.cache/sq2-video-p010-stride/venus编译完成session59170 exit0，core a5f2bcc54b0ec523c8df886f1675f4dbb9c934c9756e373028bbe307f4d1af0f、dec66e9c26a9602a7d01a4f0f4d74110967f7aa90af85d449a24bfa16a2c1315a47，符号missing0。尚未添加pack revision/打包/部署。下一步新独立镜像启动后完整六帧Main10 CRC+EOS，再做NV12回归；AndroidP010路径仍待实现。runner原有主机阶段/回收测试通过，新dump路径实机只验证本次小payload，不代表所有故障分支已测。

**2026-10-30 裁剪修复实机生效，Main10首帧布局仍失败**：新boot31cf07bf-f0b2-4a66-8163-96550013d9c5/boot_completed1，core bb835d4d…/dec f57dbf81…匹配。独立collector sq2-p010-crop-boot-kernel.log smoke后本boot仅一次lab-start成功。user10/唤醒/跳过锁屏/timeout2147483646已恢复。首次decode-test-crop --dmabuf输出CROP_BEFORE_FORMAT=320x240，S_FMT(P010)后crop仍320x240，裁剪修复通过实机；capture宽320高256、stride768、size294912。捕获10buffer/STREAMON成功，返回input0及首个capture bytes245760/offset0/flags4008，但严格need校验报short frame，未作CRC，exit1；两个STREAMOFF0、CLOSE_DONE。

不能称Main10解码通过。245760与报告stride768/scan256的布局不一致；可能P010实际stride/scanlines或固件bytesused定义，需有界采集真实buffer并与独立P010参考比较，不能直接忽略short-frame或猜stride上线。完整result/native-start/当时collector取回out/video-decode/hevc-main10-vector；collector收尾待取。当前native仍驻留，没再试、没热卸，Android服务旧版。下一步先看已有日志/代码，必要时独立新启动做一次有界帧布局取证。

**2026-10-30 裁剪保留候选已编译/打包，正在上传**：新增venus-sq2-crop.h，在SQ2且reconfig（SOURCE_CHANGE已到）、捕获队列、协商存储宽高未变、原裁剪非空/非负且边界有效时，vdec_s_fmt保留固件crop；其它情况仍重置。tests/sq2_crop_test.c在ASan/UBSan覆盖实测320x240-in-320x256、非零offset、尺寸改变、非SQ2/非捕获/无reconfig、负值/空/越界/UINT32_MAX。runner新增G_SELECTION前置日志使旧mock缺ioctl，已补只读selection fixture，资源/阶段测试通过。

新stage/root/.cache/sq2-video-p010-crop/venus编译通过；core bb835d4da5faa9282c98dbb02083fbf4738dbef4d2a2ff1b83f5538b681ef6ce，dec f57dbf811968e4ac03f21b3b917c093ee0f2155276cc0d91038fc214a7dec79d，符号审计missing0，格式helper、640停止序列、10END案例通过。打包--revision p010-crop输出capabilities-p010-crop.cpio.gz SHA500f153b46ae0e62ce9b3764c9e710040afa9a45db79312265dea2dd308e8410，仅core/dec/manifest变化、密码addon最后。当前boot仍85c96f2e…；身份/ESP/UUID/marker/root backing重核通过，ESP余711396KiB。旧collector完整取回KMSG_END timeout120001ms/334records/25321bytes。新镜像SCP session40568已完成；设备端SHA/gzip校验通过，sync后原子改名。备份grub.cfg.before-p010-crop及sq2-c2-test.env.before-p010-crop，核验新菜单/once文件后原子替换、sync并主动Android reboot（session48944）。待新boot确认，不重复发reboot。准备菜单out/video-decode/boot-candidate/grub.p010-crop.cfg SHA7c03e5a64261cc95ffc4915b9f5d11cdfce2d84f75f5406549b78f2ec1c0c117，仅实验initrd路径改变。

**2026-10-30 P010新启动成功；Main10发现S_FMT裁剪覆盖**：新boot85c96f2e-d1ab-412d-ad9f-2c68c018b7d3、boot_completed1、guardY，core eb2a8725…/dec ce3366cd…匹配。独立sq2-p010-boot-kernel.log smoke通过后，本boot仅一次video-decoder-lab-start成功；禁止重跑/热卸。已切user10、WAKEUP/dismiss-keyguard并设timeout2147483646。当前Android服务为启动镜像旧版，未重新部署owner67b3。

Main10六帧--dmabuf首次试验exit1：source change成功，P010 S_FMT接受（fourcc30313050、320x256、stride768、size294912），但crop320x256与向量320x240不符，拒绝在capture STREAMON前继续；输出STREAMOFF0/CLOSE_DONE。没有硬件帧验证通过。runner新增G_SELECTION_BEFORE_FORMAT日志，重新编译decode-test-crop作第二次有界试验（不重启PAS）：**切换格式前crop0,0,320,240，S_FMT(P010)后变0,0,320,256**，同样干净关闭。确认是vdec_s_fmt无条件inst->crop=对齐后width/height覆盖，不是固件最初报错；现有严格尺寸检查保持。

完整两轮结果和native-start已取回out/video-decode/hevc-main10-vector/sq2-p010-{main10,crop}-result.txt等，collector已取回（收尾需核对），发现退出后的invalid session/no valid instance消息（pkt221007/21001）；CLOSE_DONE只证明用户态关闭返回，不证明固件无迟到消息。当前boot不继续帧测试，后续用修复候选独立新启动。下一步修改SQ2捕获格式切换：source change后仅在存储尺寸不变且已有crop有效时保留固件crop，保留初始化/其它平台原语义；增加真实helper边界检查、新stage编译/符号审计/打包，再独立重启验证P010六帧。不能在驻留模块上直接热替换，也不能用忽略crop/换256高向量代替修复。

**2026-10-30 P010一次性启动已发出**：SSH自行恢复，仍boot59af714a…/guardY。重新核对BC711/ESP UUID及marker/data UUID/root backing，ESP余905132KiB。候选已上传临时路径并在设备sha1808a39f…及gzip -t通过，sync后原子改名/media/esp/boot/sq2-android1/native-c2-capabilities-p010.cpio.gz。仅实验入口initrd路径由v3改为p010，默认正常入口不变；菜单备份grub.cfg.before-p010（69e016c5…），新菜单d47afa2057dbd15797ecf296aa191e940d9ce39a9111146f76b5f88224c2b867。once=0备份sq2-c2-test.env.before-p010，上传once=1核验d762d0a0…后原子替换并sync。已通过Android reboot发出主动重启（exec session4758），尚待新boot确认，不重复发reboot。

重新编译的静态Main10 runner out/video-decode/hevc-main10-vector/decode-test-p010通过QEMU padded-reference（2字节/sample），已上传Android/data/local/tmp同名，尚未执行。新boot先验证core eb2a8725…/dec ce3366cd…、boot_completed和screen_only，再启动独立collector并仅一次video-decoder-lab-start；不可沿用上一boot的PAS状态或热卸。随后有界六帧Main10实际CRC/EOS/关闭，再做NV12回归。Android服务重启后回到镜像旧版，RAM owner/private-wrapper需要后续重新部署，不假定67b326ca仍在运行。

**2026-10-30 P010候选打包完成，未部署**：有效stage仍/root/.cache/sq2-video-p010-v3/venus；core eb2a8725c3dd82ab168dfa668fc4871d4e5feae07ac5b437480bbe62a046e8c4，dec ce3366cd67a9f7e4b16c2b875475fd0d2656c5fb4be242adb25ce55244450301。从当前设备新采集guardY+kallsyms，audit-sq2-video-modules.py排除被替换resident模块旧exports后，两模块missing=0；这仅符号闭包，不是ABI/硬件证明。实际helper ASan/UBSan六codec格式选择与默认索引检查再次通过。

pack-sq2-video-capabilities.py新增显式--revision p010及上述固定hash，保留1/3选择；新私有产物out/video-decode/boot-candidate/capabilities-p010.cpio.gz SHA1808a39f88c1f67984ad6770070194e178221eca6bf9711d50c85d1ba60ccb3a。完整解压/last-entry-wins审计只变core/dec/SHA256SUMS，密码addon最后，Android owner不变。尚未上传、改菜单或重启。设备曾成功读取BC711/1000215216sectors、ESP UUID B027-0512/marker SQ2-INTERNAL-512-20261027、data UUID4f46d2ec…、loop0 backing p3、root erofs/data p1、菜单hash69e016c5…和once=0；紧接着df及SCP读取连接超时，不能把早先成功当作部署完成。grub.before-p010.cfg不存在，后续生成菜单因缺输入退出，没有任何设备写入。下轮先复查连接/状态再做部署前身份核对。P010原生向量尚未实机，不能广告Main10。

**2026-10-30 VP8 4K30普通应用通过**：同boot59af714a…/owner67b326ca…，vp8-4k自动选择c2.sq2.vp8.experimental，hardware/format_supported=true；3840x2160/30、300输入/输出/渲染，9962ms，EOS/realtime/pass=true、STOP_OK/RELEASE_OK/RESTORED。目录已取回out/video-decode/vpx-app/sq2-player-vp8-4k-59af714a-4cfe-4017-9ce0-d1656715c923。构建session45611已完成，当前APK为VP8 4K30。该轮沿用仍在120秒窗口内的sq2-vp9-4k60-kernel.log（测试结束时uptime约925秒，collector起点约824秒），现已取回完整日志，KMSG_END timeout120000ms、245records/17664bytes；无KMSG_OVERRUN，未见venus/hfi/BUG/Oops/panic记录（有audit限流，不能据此推断所有日志无缺失）。收尾XML_REMOVED、服务running。不测试VP8 4K60：32400宏块/帧乘60超出固件1036800宏块/秒；不是把VP9的吞吐复制给VP8。接下来优先补P010原生候选启动/10bit和剩余MPEG适配，正常启动持久化仍未完成。

**2026-10-30 VP9 4K60普通应用通过**：同boot59af714a…/owner67b326ca…，profile0 3840x2160/60普通UID自动选择c2.sq2.vp9.experimental，hardware/format_supported=true；600输入/输出/渲染，10056ms，EOS/realtime/pass=true，STOP_OK/RELEASE_OK/RESTORED。完整结果/截图已取回out/video-decode/vpx-app/sq2-player-vp9-4k60-59af714a-4cfe-4017-9ce0-d1656715c923，人工检查late截图有测试图案；不是逐像素校验或长片稳定性认证。收尾boot不变、XML_REMOVED。上一轮4K30 collector已确认KMSG_END timeout120000ms/255records/18171bytes；4K60独立collector的smoke已验证，120秒结束后仍需取回完整日志。60fps构建session82952已正常完成；当前APK为VP9 4K60。下一项VP8 4K30构建正在运行（核对当前会话），还未实机运行。

**2026-10-30 VP9 4K30应用通过、新owner VP8回归通过**：boot仍59af714a…，owner67b326ca…，普通UID1010254自动选择硬件组件。vp8-small3（320x240/30）与vp9-4k（3840x2160/30、VP9 profile0）均300输入/输出/渲染、9963ms、EOS/realtime/pass=true，STOP_OK/RELEASE_OK/RESTORED。结果和截图已取回out/video-decode/vpx-app下各同名boot目录；收尾XML_REMOVED、服务running、boot未变。4K结果不是60fps、10bit或长片证明。独立sq2-vp9-4k-kernel.log有smoke标记，120秒有界collector，完整收尾仍需取回核对。下一项VP9 4K60 APK构建已启动，exec session82952，输出player-app-3840x2160-vp9-60fps；先轮询现有handle，不重复启动构建。尚未上传或测试60fps APK。调试screen_off_timeout再次实读2147483646、guardY。

**2026-10-30 新用户偏好与VPx普通应用通过**：用户已移除PIN，明确调试期间可跳过锁屏，并要求不自动关屏。已在新boot以am switch-user 10、WAKEUP、wm dismiss-keyguard进入用户10，不清除凭据。screen_off_timeout设2147483646ms（约24.8天），避免旧lab-keep-awake仅对2147483647的两分钟迁移；复查值保持。覆盖此前30分钟调试偏好；手动关屏和suspend.screen_only=Y不变。用户还明确固件不支持就丢弃，见能力文档。

此前c939c2e已提交并推送origin/codex/sq2-video-decode，随后确认正常bootd4bb6f99…/boot_completed1/无native。完整复核NVMe/ESP/root/菜单69e016c5…/v3镜像72a6037f…后仅置一次env并主动重启，当前boot **59af714a-4cfe-4017-9ce0-d1656715c923**，env已清0、core50afdd91…正确。独立collector smoke后本boot仅首次lab-start成功，不能重跑或热卸。

新四组件owner372c024b…先通过HEVC4K60普通APK回归：600输入/输出/渲染，9977ms，EOS/STOP/RELEASE/RESTORED。VP8首轮测试APK因WebM没有KEY_FRAME_RATE而NPE，未进入硬解；build脚本现生成fps.txt，APK验证容器若有帧率则与固定测试元数据一致，否则采用元数据，禁止猜实际文件帧率。vp8-small2自动选择hardware=true，300帧9965ms、EOS/STOP/RELEASE通过。

VP9首轮自动选中但9字节CSD被组件拒绝；对照本地固定AOSP C2SoftVpxDec，改为有界消费VPx容器CSD，不拼接到硬件帧：拒EOS、多buffer/多block、映射错误及超过1MiB。新owner **67b326ca19b2c949078ba6bcdb13926db5e0a214f1c1c1775a40f26673446c44** 已RAM部署（前版备份sq2-c2-owner-vpx-before-csd，旧双组件备份sq2-c2-owner-before-vpx）。vp9-small2普通UID自动选择hardware=true/format_supported=true，300输入/输出/渲染9969ms，EOS/realtime/pass=true、STOP_OK/RELEASE_OK/RESTORED。VP8/VP9均320x240/profile0，不是4K/10bit/长片认证；VP8通过版本为CSD改动前，后续需回归新版本。

本轮启动日志、完整APK结果/截图/日志和collector取回out/video-decode/vpx-app；collector确认KMSG_END timeout120000ms，收尾XML_REMOVED、boot保持。当前APK为修复帧率的VP9版。默认启动镜像不变，新owner/private wrapper仅RAM；无新MPEG2或P010硬件测试。此次提交后的源码/文档改动尚未提交。

**2026-10-30 会话层P010适配，仅主机测试**：Sq2V4L2Session::open新增末尾outputFormat参数（缺省NV12，所有旧调用保持），P010仅允许HEVC/VP9；Sq2Geometry新增实际format，捕获协商核对返回fourcc不可静默降8bit。可见尺寸/字节stride/sizeimage采用64bit边界计算，拒绝奇数YUV420尺寸、P010奇数字节stride、短分配和短输出payload。单帧16MiB/总192MiB限制未扩大，4K P010仍不接受。Android C2未请求P010，图像复制与profile广告仍8bit。

tests/sq2_v4l2_session_test.cpp在ASan/UBSan下覆盖NV12/P010两组payload/token/EOS、replacement失败后客户端引用保留、100帧复用5次分配、close/release；额外拒绝错误stride、驱动替换格式、47B短分配和47B短帧，并检查零交付/无映射泄漏。四组件store重新编译通过，out/aidl-store/interface-test已被新版覆盖，**设备上的2bea9691…候选仍旧，不能把新本地产物与旧哈希混用**。没有上传或替换设备服务。SSH仍超时，正常重启是否完成未知，仍待用户屏幕状态回应。

**2026-10-30 P010候选实际helper离线检查通过**：新增tests/test_sq2_p010_selection.py，直接提取stage中to_hfi_raw_fmt/find_fmt_from_caps/venus_helper_get_out_fmts/get_framesize_raw_p010实际C代码，使用已保存固件各codec格式表，在ASan/UBSan下执行。HEVC/VP9 P010选择TP10_UBWC DPB+P010 OPB通过；其它四codec拒绝；删除任一必需端口格式后拒绝且输出清0；六codec NV12选择保持UBWC+NV12。并检查数组默认NV12索引0/AVC索引8保持、P010末尾追加。仅主机算法检查，不证明内核ABI或硬件帧。

尺寸函数实际计算320x240 P010=294912B，3840x2160 P010=25067520B，确认当前单帧16MiB上限不足。扩展Android路径必须同时覆盖native capture、C2GraphicBlock、私有mapper/import缓存预算，不能只改XML或单处上限。设备本轮SSH仍超时，未操作设备或重复重启；仍等待用户屏幕状态/恢复信息。此轮有离线验证进展，目标未标blocked或complete。

**2026-10-30 P010原生候选，仅离线**：prepare-sq2-venus-iris1.py新增V4L2 P010捕获格式，仅SQ2且已识别10bit时可选/枚举；追加到原format数组末尾，保留硬编码AVC索引8/NV12索引0。to_hfi_raw_fmt补P010映射，bytesperline按ALIGN(width*2,256)；复用内核已有P010 sizeimage计算。SQ2 P010输出必须真实能力同时包含OUTPUT TP10_UBWC和OUTPUT2 P010，才选择该DPB/OPB组合，否则EINVAL，避免原通用逻辑错误寻找P010_UBWC或把P010输出当成OUTPUT。其它平台不暴露新格式。

有效stage为/root/.cache/sq2-video-p010-v3/venus，编译通过（仍缺Module.symvers、需未来实机导出审计）；较早p010和p010-v2因插入格式改变默认索引均废弃，禁止部署。没有打包/上传。原生runner现在从header每sample字节数选择NV12/P010、检查实际返回fourcc、按字节行宽/stride计算CRC及缓冲边界。--reference-test把独立参考放入带0xa5填充的stride/scanline缓冲再校验，AArch64 QEMU下NV12/P010六帧均通过；test_sq2_v4l2_runner.py资源/阶段故障回归通过。Main10二进制out/video-decode/hevc-main10-vector/decode-test尚未实机执行。

设备SSH本轮仍超时，屏幕状态问题待用户回复；未重复reboot/写盘。下一步恢复设备后先评估MPEG2会话诊断与候选启动，再验证P010真实geometry/帧字节/生命周期；Android Sq2V4L2Session/C2GraphicBlock仍仅8bit，单帧16MiB及192MiB预算尚未覆盖4K P010/8K，不得直接广告Main10或放大上限。

**2026-10-30 离线Main10验证素材**：设备重启后SSH仍超时，用户屏幕状态问题尚无回复，未重复重启/写设备。离线检查MPEG2固件能力明确含output2 NV12，故不能仅因0x1008推断该格式不受支持；具体属性还待first-error跟踪。现有is_dynamic_bufmode对HFI4默认true、multistream/format选择等路径已检查，尚无证据选择修改哪项。

build-sq2-hevc-test-vector.py新增--bit-depth 10，生成10-bit geq图样（非8-bit源简单扩展）、Main10 ES和独立P010LE六帧参考；ffprobe校验profile/pix_fmt，P010低6位必须全0且有效10bit低2位确有非零。out/video-decode/hevc-main10-vector码流SHAd47f1b74d9e93b6a2e70ad3b60fd1bef577e7850ea84f54d59a362cf19e6454b。8-bit重新生成与旧clip/ref逐字节一致。header新增每sample字节数；NV12原生runner编译时明确拒绝2字节参考，避免误测。**没有实现或部署P010输出，Main10未硬件验证**；下一步仍需设备恢复及原生10-bit格式/geometry/buffer适配，不能改XML直接宣称支持。

**2026-10-30 MPEG2错误洪泛定性与正常启动恢复**：只观察既有流量的sample-hfi-events.sh运行1秒、64KiB独立trace缓冲，未提交任何解码命令。当前持续消息确为size28/type0x21001/session0x3013efa5/event2/data1=0x1008/data2=0，即HFI_ERR_SESSION_UNSUPPORTED_SETTING。固定hfi_msgs.c将此类列为非fatal并清inst->error，故此前没有直接上报错误；尚未确定是哪项属性、不证明硬件不支持MPEG2。样本已取回out/video-decode/mpeg2-vector/sq2-hfi-existing-events.txt，探针已移除。

为终止坏会话持续事件，确认一次env=0后sync并主动Android reboot，目标为未改动默认正常入口（不载native）。重启SSH已reset，后续连接仍超时；**本条记录时尚未确认新boot/恢复，不要重复发reboot**。旧boot3dd93e6c…不再当当前状态。待SSH恢复先确认boot ID、boot_completed、guard和native是否未加载，再安排独立候选启动，不在默认入口直接绕过lab-start条件。

新增sample-mpeg2-first-error.sh，仅语法检查，未部署/执行：新启动native和独立collector就绪后，一次320x256测试前注册property提交及HFI event探针，首个event2/data1=0x1008触发traceoff，保留前序属性；128KiB/45秒timeout，退出移除探针。这是诊断候选，trigger实际支持及清理需核对，不能把脚本准备视为定位完成。MPEG2原320x240裁剪问题仍开放，Android应用解锁问题继续待用户响应。

**2026-10-30 MPEG-2验证失败，当前native状态需重新评估**：新增build-sq2-mpeg2-test-vector.py，生成六帧无B帧MPEG2 ES，ffprobe包边界覆盖全流/每包一picture，独立NV12参考和CRC。支持320x240及320x256，直接核对sequence header尺寸。runner新增SQ2_MPEG2_VECTOR，不放宽geometry判定。原320x240实测收到crop320x256，在输出前拒绝，streamoff/close正常、exit1。独立sample-mpeg2-geometry.sh有界kprobe确认vdec_event_change error0,width320,height256,input_crop全0；这是固件报告缺少可见裁剪信息，不是测试素材尺寸错误。探针已退出移除。

320x256对照保留原失败，不作为缩小支持范围的替代。对照完成source change和capture STREAMON，但0输出/0输入归还，RESULT ret1；两个STREAMOFF返回0，CLOSE_BEGIN之后外层25秒timeout终止进程，exit143，无CLOSE_DONE。之后内核大量`no valid instance ... pkt:21001`，不据此断言整板崩溃或PAS停止。boot仍3dd93e6c…、guardY、SSH可用、无mpeg2探针进程。**不要继续沿用当前native做其它解码回归，也不要热卸/重跑lab-start；先分析保存的失败/关闭证据，必要时干净启动再测。** 正常默认启动镜像未改，临时四组件服务尚未部署。

原始输出/geometry trace/完整事后dmesg已取回out/video-decode/mpeg2-vector/sq2-mpeg2-*.txt。大量旧会话消息可能已挤出更早dmesg，后续需新独立有界collector覆盖完整测试过程，不能用事后空error筛选断言无错误。当前user0，普通应用解锁问题仍待用户回复；MPEG1/2、10-bit及完整能力目标未完成。

**2026-10-30 VP8/VP9 Codec2组件像素验证通过**：在boot3dd93e6c…驻留v3后端运行独立Android C2 harness，无需桌面、不注册新store。build-sq2-c2-component-test.py新增--codec vp8/vp9，输入为各自六帧原生向量；组件和接口使用与候选store相同Sq2Codec映射。测试按C2GraphicView实际rowInc/colInc重组规范NV12再比较，兼容I420/NV12/NV21布局，避免旧测试直接把U平面当交错UV读取的错误。

两个格式均C2_RESULT done7/mask3f/eos1/errors0，六帧Y/UV CRC全匹配独立FFmpeg参考，queue/stop/release返回0，客户端保留buffer最后释放，进程exit0。AVC同源码回归也done7/mask3f/eos1/errors0、停止释放0。日志out/video-decode/vpx-component及设备/data/local/tmp/sq2-c2-{vp8,vp9}-frames-result.txt、sq2-c2-avc-vpx-regression.txt。此为独立root进程的真实C2组件→原生硬件→图像buffer验证，**不是普通UID MediaCodec/Extractor/Surface播放证明**。当前服务owner739d…仍不变，四格式candidate未--serve；user0仍待解锁，应用APK未替换。无需为此重启或重新启动native/PAS。

**2026-10-30 Android VPx接入候选（未替换服务）**：新增sq2-codecs.h集中映射AVC/HEVC/VP8/VP9名字、MIME和fourcc，接口/组件改显式Sq2Codec，默认AVC供旧测试使用。store新增VP8/VP9条目，临时XML新增OMX.qcom.video.decoder.vp8/vp9别名；当前输出仍NV12 8-bit，VP8 profile0/LEVEL_UNUSED，VP9 profile0、level1..5.1，完整固件profile2/尺寸吞吐仍未实现。VPx拒绝意外codec-config work，避免拼接成损坏帧；真实WebM MediaExtractor路径尚待验证。

四组件候选SHA2bea96916fcd2bcdd6ed5491a93e8e49cd39307054efd8ef8e77e0201ae527ab，设备/data/local/tmp/sq2-c2-vpx-candidate本地store检查STATUS0/list4及MIME正确，未--serve、未替换owner739d…或当前服务。build-sq2-c2-aidl-service.py新增--interface-test输出parameter-test，设备通过VP8/VP9 profile0接受、未支持profile拒绝并保持原配置，以及AVC/尺寸/pool原回归。编译和参数检查不等于解码通过。

build-sq2-video-app.sh新增vp8/vp9，生成10秒300帧WebM（保留历史资产名clip.mp4，Extractor按内容识别），lag/altref关闭；MainActivity按四种MIME核对硬件组件，runner新增vp8-small/vp9-small自动选择标签。两个320x240 APK已构建，未安装，设备当前仍旧HEVC4K60 APK。下一步解锁后先做v3 HEVC4K60回归，再备份/替换新owner、上传新XML/runner、分别安装VPx APK实测，不能以store枚举替代播放。当前user0，解锁问题仍待用户回答，不重复请求、不清凭据。

v3 collector最终KMSG_END timeout120000ms已确认并完整取回caps-regression/sq2-caps-v3-kernel.log；native保持驻留，当前boot3dd93e6c…。

v3本轮收尾更新：已核对userdata保留的wrapper bdf11d60…、owner739d346e…、私有mapper eb31d402…及全局mapper c1a57082…，在RAM恢复私有C2 wrapper并启动，service running。当前前台user0，已请求用户正常解锁进入桌面以继续Android回归，未改凭据。下一轮可用新boot尚未占用的hevc-4k60标签；native已驻留，禁止重跑lab-start。

**2026-10-30 v3实机VP9/尺寸枚举通过**：再次完整核对BC711512GB、ESP B027-0512、SQ2-INTERNAL-512-20261027、userdata UUID和root loop0/p3，ESP可用1098868KiB后上传独立native-c2-capabilities-v3.cpio.gz。设备完整SHA72a6037f…、gzip通过，sync/原子替换；备份grub.before-capabilities-v3.cfg及sq2-c2-before-v3.env。菜单仅实验initrd改v3，新菜单SHA69e016c5…，默认正常入口保持。一次env1在启动中已消费为0。

主动Android reboot后boot **3dd93e6c-cd92-4f9e-8c62-ca23bebd3fd1**，boot_completed1/guardY，core50afdd91…/dec257ffcd1…匹配。独立120秒collector smoke后仅首次调用video-decoder-lab-start，START_EXIT0、native ready；不热卸载。VP9六帧Y/UV CRC全匹配，EOS flags104008已无ERROR，RESULT frames6/mask3f/input_returned6/drain1/last1/ret0，streamoff和close通过。HEVC六帧同样完整通过。全格式新静态探针实读MPEG2 96..1920、VP8 96..4096、AVC/HEVC/VP9 96..8192，均step1。证明按codec查表生效，不是任意方形最大尺寸或吞吐实测。VP9/VP8 profile controls仍过宽0..3，HEVC/MPEG2查询EINVAL，MPEG1缺枚举，这些仍需对齐。

注意out/video-decode/sq2-v4l2-capabilities是旧Android动态、仅AVC探针，不能再用作全格式证据。本轮从当前源码重新静态编译caps-regression/capabilities-v3-probe，完整枚举结果sq2-caps-v3-enumeration-all.txt。启动/VP9/HEVC/枚举日志已取回out/video-decode/caps-regression。collector最终日志尚需取回。当前启动C2仍镜像旧版，新owner/private wrapper尚未在本boot恢复；不能将本轮原生验证当作Android v3回归。后续恢复C2、接入其它codec/10-bit/真实联合能力并持久化，目标未完成。

**2026-10-30 VP9空EOS候选v3（尚未部署）**：sample-vp9-eos.sh新增packet模式，实际hfi_session_ftb_done包size104/stream1/error0/flags0x20000021/bytes0/offset0；六帧各147456B且error0。证据out/video-decode/caps-regression/sq2-vp9-eos-packet-{trace,decode}.txt。原生runner仍ret1，未伪造成功。venus-sq2-vp9-eos.h仅对SQ2 VP9 decoder、error0、bytes/offset0、精确EOS|SYNCFRAME|DROP_FRAME组合清DROP；其它SoC/codec/encoder、任意非零error/长度/偏移及其它flags全保留。实际helper ASan/UBSan测试覆盖上述各字段32位单bit变化，通过。prepare脚本仅decoder packet分支调用，vdec原有损坏/丢帧处理不变。

stage /root/.cache/sq2-video-caps-v3/venus编译完成，含v2逐codec尺寸枚举。core SHA50afdd9164eb9eb4c2f7882871045dcdd52c88ef0f32e219ab875c6251ea644b，dec SHA257ffcd19294a3a16ddb667a20ed3ae0196471b206de947c4ed8e1f0fb585d92。audit-sq2-video-modules.py现剔除被替换resident模块的旧导出再核对新模块闭包，避免误用旧符号；当前实机导出审计missing0，报告out/video-decode/caps-regression/v3-link-audit.json。缺Module.symvers的modpost警告仍存在，此为符号闭包不是完整ABI验证。

pack-sq2-video-capabilities.py --revision 3生成capabilities-v3.cpio.gz SHA72a6037f63400e804e031d2b2d057d09b8d7e38017ea86ad6e50cd037b3fff16，审计仅core/dec/SHA256SUMS三项改变、密码addon最后。尚未上传ESP/改菜单/重启；当前仍boot9e548e25…第一版native及临时新版C2。下一步重新核对物理盘/ESP/root与剩余空间，独立一次性候选启动验证VP9 EOS及逐codec枚举，不能热卸native/PAS。Android新增格式、Main10与完整能力对齐未完成。

**2026-10-30能力候选帧回归**：同boot9e548e25…，新native后端HEVC六帧独立NV12 CRC、EOS/streamoff/close通过。RAM恢复私有wrapper bdf11d60…、owner739d346e…及192MiB mapper eb31d402…，不是镜像持久化。用户正常解锁后前台user10，普通APK hevc-4k60b自动选中c2.sq2.hevc.experimental，hardware=true/format_supported=true；3840×2160/60fps/Main，600输入/输出/渲染，9977ms，EOS/realtime/pass=true，STOP_OK/RELEASE_OK/RESTORED。此前hevc-4k60c因user0禁止启动失败，不能计为解码失败。收尾XML_REMOVED、media running、guardY、boot不变，独立collector已KMSG_END timeout120000ms。证据完整取回out/video-decode/caps-regression。仅合成10秒8-bit验证，非Main10或B站4K证明。

VP9同boot重现六帧像素匹配但空EOS ERROR。新增sample-vp9-eos.sh以独立tracefs instance、128KiB缓冲、25秒timeout采集vdec_buf_done标量，不卸载模块。实际EOS为type3/bytes0/offset0、V4L2 flags0x100008、HFI flags0x20000021，即EOS|SYNCFRAME|DROP_FRAME，没有DATACORRUPT；当前驱动DROP_FRAME分支将其标为VB2错误。尚未修改或吞掉此错误，需进一步明确空结束标记与真正坏帧的区分及固件packet error。探针和instance退出均已移除，原始trace/decode日志留out。第二版逐codec ENUM_FRAMESIZES候选仍未部署；后续其它codec、10-bit、完整能力映射及持久化仍未完成。

**2026-10-30能力候选实机启动**：备份EFI/BOOT/grub.before-capabilities.cfg和sq2-c2-before-capabilities.env，仅实验项initrd改为native-c2-capabilities.cpio.gz，菜单新SHA8dbc5acd…；默认正常项不变。一次env=1已消费为0。主动Android reboot进入boot9e548e25-71bc-4e43-9e6b-90e01a5e0853，boot_completed1/guardY，新core39400063…匹配。独立120秒collector smoke后只调用一次video-decoder-lab-start，所有哈希通过、START_EXIT0、/dev/video0就绪，PAS/native保持驻留，不热卸。

首次枚举AVC尺寸已变8192，表明真实能力路径生效；但MPEG2/VP8也显示8192。源码确认vdec_enum_framesizes忽略请求fourcc而查询当前默认AVC能力，因此此前“所有codec原生4096”不能当作每个codec真实上限。新增venus-sq2-frame-sizes.h，仅SQ2压缩格式枚举按codec查表、无会话状态修改；MPEG2/VP8/AVC/HEVC/VP9分别1920/4096/8192/8192/8192，缺尺寸的MPEG1返回EINVAL而不捏造。实际函数与恢复能力包ASan/UBSan测试通过，第二版stage/root/.cache/sq2-video-caps-v2/venus编译通过，**尚未打包/部署第二版**。现机仍第一版core/dec。

本轮证据out/video-decode/hfi-session/sq2-caps-9e54-{start.txt,enumeration.txt,kernel.log}，collector最终KMSG_END timeout120000ms已取回。Android旧启动C2服务仍为原基线，上一boot临时mapper wrapper未恢复；/data/local/tmp新owner/mapper文件仍可用，但不能把当前启动记为新版Android解码回归通过。下一步先完成新native后端帧回归/逐格式枚举修复验证，再恢复新版私有C2运行态。

**候选上传完成**：/media/esp/boot/sq2-android1/native-c2-capabilities.cpio.gz已完整落盘198373763字节，SHA1cb9fa4b…与本地一致，设备gzip -t通过；sync后从.new原子命名，再sync。返回CANDIDATE_STORED_MENU_UNCHANGED。未更新菜单/一次标记、未重启，下一步备份当前EFI菜单与env并准备一次性能力启动。VPx collector已确认KMSG_END timeout120000ms，最终记录尚在设备。

**2026-10-30 VP8/VP9原生验证与磁盘复核**：新增build-sq2-vpx-test-vector.py生成6帧VP8/VP9，剥离IVF容器后提交真实帧包，独立FFmpeg NV12参考；runner用SQ2_VPX_VECTOR选择实际fourcc。两者QEMU CRC自测通过，主机runner故障清理回归通过。实机同boot39441275…：VP8六帧Y/UV全匹配、EOS/streamoff/close、exit0；VP9六帧也全匹配，但末尾空缓冲flags104048包含ERROR，runner未吞错，ret1/last0，streamoff/close正常。暂不可标VP9完整成功；需进一步取得HFI flags/error来源。日志out/video-decode/{vp8-vector,vp9-vector}，collector bounded120秒，尚需取回最终KMSG_END。未改当前resident native。

磁盘身份现已完整只读复核：BC711 SK hynix512GB、1000215216扇区；标记SQ2-INTERNAL-512-20261027；ESP /dev/nvme0n1p6 UUID B027-0512；data p1 UUID4f46d2ec-323e-486e-b473-fc2adc0849ba；Android root /dev/loop0 erofs来自p3、data来自p1。ESP可用1292604KiB。真实菜单/EFI/BOOT/grub.cfg默认sq2-internal，实验sq2-c2-test由sq2-c2-test.env先消费一次标记后启动，旧实验镜像SHAa5038428…匹配本地基线。能力候选正在上传独立native-c2-capabilities.cpio.gz.new；必须核对完整hash/gzip后才原子命名、更新一次性入口，默认镜像/菜单尚未修改，未重启。

**2026-10-30能力候选打包/链接检查**：现场仍boot39441275…、guardY、XML_REMOVED。新venus-core required177/provided67，新venus-dec required130；从实际__ksymtab集合剔除resident旧core/dec后，按新core导出再核对decoder，missing均0，vermagic均6.3.8-sq2-android1 SMP mod_unload aarch64。证据out/video-decode/hfi-session/candidate-link-audit.json。此为符号闭包，不是加载/解码验证。

新增pack-sq2-video-capabilities.py固定a5038428…实验基线及本轮两个模块哈希，生成out/video-decode/boot-candidate/capabilities.cpio.gz SHA1cb9fa4b2759b9545220d0192db8f55cad591542cb93f2c83e5e78021ab3214f。最终条目审计只变/etc/sq2-video-native下core/dec/SHA256SUMS三项，凭据addon最后，gzip测试通过。旧启动C2服务刻意保留；新owner739d346e…和私有mapper后续仍需在启动后RAM部署，不能据此镜像宣称完整Android硬解持久化。

未写ESP/菜单、未重启。只读已确认Surface NVMe为BC711 SK hynix512GB、ESP挂载/dev/nvme0n1p6、存在SQ2-SSD-ID/SQ2-PROBE-MEDIA标记；后续标记内容/UUID/菜单读取遇SSH超时，磁盘身份门槛尚未完整。不要凭此半份检查进行磁盘写入。接下来复核标记/UUID/root source/可用空间/真实EFI菜单位置，再准备一次性启动及原入口回退；不要热卸当前native/PAS。

**2026-10-30 SQ2真实能力解析候选**：新增sq2-hfi-properties.h有界逐属性walker及venus-sq2-capabilities.h事务式目标表构建。仅SYS_INIT_DONE且IS_SQ2_HFI走此路径，传入pkt->num_properties校验完整属性数；session初始化及其它平台保留原路径。临时堆表全部验证后才发布，拒绝未知ID、截断/计数溢出、目标数组累积越界，失败不改变原core能力；不把payload内碰巧等于属性ID的值当成新属性。prepare-sq2-venus-iris1.py增加固定hfi_parser.c哈希及这些补丁。

实际C代码ASan/UBSan验证恢复的75属性包、全部截断长度、payload ID碰撞、极大计数、非法plane、未知ID、33项超限和两组17项累积越界。真实表解析10个编码/解码条目，decoder掩码0x701a，各格式宽度8192/4096/1920正确；失败事务旧表逐字节不变。候选stage /root/.cache/sq2-video-caps/venus，内核modules编译通过，但缺Module.symvers、KBUILD_MODPOST_WARN=1，尚未做本候选运行导出闭包/实机加载，不能视作部署通过。现机native/PAS保持驻留；后续需独立启动验证，不热卸换core。其它codec接入、MPEG1格式映射、10-bit及更大缓冲仍待完成。

**2026-10-30能力来源恢复/定位**：SSH恢复，同boot39441275…、XML_REMOVED。hevc-4k60c完整目录与collector取回，600帧9978ms结果再次核实；copy+sync2297973us，释放7项87822336B，collector KMSG_END timeout120000ms。全格式只读工具实机枚举MPEG2/AVC/VP8/VP9/HEVC、均96..4096，缺MPEG1；HEVC/MPEG2 profile control EINVAL。原始/data/local/tmp/sq2-hfi-session-44c0.kmsg已恢复，严格解析75属性/774体字全部通过。数值快照configs/sq2-firmware-decode-capabilities.json，明细见VIDEO-CAPABILITY-ALIGNMENT.md。MPEG2固件上限1920/30fps而非当前驱动4096，AVC/HEVC/VP9固件8192，不能统一扩大所有codec。

源码定位：固定6.3 hfi_parser先hfi_platform_parser，成功便返回，HFI4使用hfi_platform_v4静态表而未解析真实SYS_INIT能力。SQ2应接有界逐属性解析，保留其它平台行为；不能简单绕过静态入口后落入无长度边界的逐字扫描。当前只完成定位/证据快照，尚未修改/加载native能力解析。native/PAS保持驻留，切勿为此热卸载。下一步可先做解析器与主机边界测试，然后准备独立启动候选；并继续其它codec的实际帧验证。

**2026-10-30 最新目标与运行态**：用户明确要求对齐固件上报的全部解码格式及分辨率，包含HEVC 4K60；不能只补AVC/HEVC名字。已有记录包含H264、HEVC、VP8、VP9、MPEG1/2，需逐格式核对profile/位深/尺寸/吞吐和Android映射。固件报告8192范围而现V4L2枚举只到4096，是待解决差异，不应静默当成对齐。当前原始caps-44c0.json不在本工作站预期目录，详细分组能力需要恢复原始证据后再填。

**更新：HEVC 4K60已通过首轮**。192MiB native capture预算候选实际上传哈希739d346e5ef12949a39e41a8b827c1fe3697eafa16eb865fd0c3a9ffe6f30c87，与本地一致，已替换RAM owner并启动。hevc-4k60c自动选择hardware=true/format_supported=true，3840×2160/60fps/Main/600帧全部输入、输出、渲染，9978ms，EOS/realtime/pass=true及STOP_OK/RELEASE_OK/RESTORED。不是Main10、长片或B站4K证明。运行完整result已由SSH返回；之后取回截图/日志遇网络超时，需补取/data/local/tmp/sq2-player-hevc-4k60c-39441275-d887-4e17-a774-9e1fad93cdd3及collector文件。以下“尚未上传”描述为本轮较早状态。能力对齐表见VIDEO-CAPABILITY-ALIGNMENT.md，原生能力枚举工具已扩为ENUM_FMT全格式，当前只有本地构建通过，上传也遇SSH超时。

本轮Bilibili HD 2.0.1实接通：c2原名评分100被拒，OMX.sq2别名700、720p约90秒2716帧。按用户要求改OMX.qcom.video.decoder.avc后评分800并选中原c2.sq2.avc.experimental；1080p capture及2073600像素HW资源，最后会话1384帧，退出释放12项37748736B。BV1NGZtBwELa画质菜单仍最高1080P高码率。qcom轮完整证据out/video-decode/bilibili/sq2-bilibili-qcom；测试退出OBSERVED/RESTORED、XML_REMOVED、media running、guardY。别名不是OEM驱动替换，未写默认镜像。

HEVC已取得原生像素证据：build-sq2-hevc-test-vector.py生成320×240 Main、6帧、无B帧；原生runner编译SQ2_HEVC_VECTOR，真实6帧Y/UV CRC全匹配独立FFmpeg参考，EOS/streamoff/close及返回0。Android store新增c2.sq2.hevc.experimental，XML别名OMX.qcom.video.decoder.hevc，暂仅Main 8-bit；配置拼接复用原有Annex-B有界字节容器（类名Sq2AvcConfig不表示解析AVC）。小尺寸HEVC含219个B帧，自动选择hardware=true、300输入/输出/渲染、9961ms、EOS/STOP/RELEASE/RESTORED全通过。

4K60 Main素材600帧、449个B帧、约24.8Mbps/level5.1。前两轮在开始后失败；加入SESSION_ERROR日志确认errno=-75/EOVERFLOW，未见输出。当前候选将native capture预算128MiB改192MiB，保留32槽/单帧16MiB上限以容纳HEVC额外参考槽，已构建但上传遇SSH超时，尚未验证生效。当前RAM运行的是带错误日志的候选；原AVC owner9a556a…保留/data/local/tmp/sq2-c2-owner-avc-9a55；首版HEVC owner84781d97…通过小尺寸。下一步必须核对上传哈希再运行hevc-4k60c，不可把未上传的预算修复标为有效。默认镜像未改，boot39441275…，native/PAS不重复装卸。

新APK构建支持SIZE hevc 60，期望600帧/10秒；当前设备APK为4K60测试版。原生测试证据及两轮应用日志在out/video-decode/hevc-playback，测试脚本均撤销临时XML。主机runner测试移除对私有out向量依赖，ASan/UBSan阶段边界与故障清理通过。尚需HEVC Surface像素、4K60性能、AVC回归、其它格式、持久化及重启；目标仍未完成。

**AVC Main/High、B帧及4K自动选择通过（2026-10-29，最新）**：普通APK的1080p Main/High各300帧均完整输出/渲染、约9.96秒，PTS严格递增，EOS/STOP/RELEASE正常。High素材含141个B帧。新增--profile main/high六帧向量，ffprobe coded_picture_number建立解码序→显示PTS映射；High向量ce117e50…含3个B帧，Surface/CSD输出6帧逐字节等于独立FFmpeg NV12参考，修改缓存前后各通过一次。证据out/video-decode/high-bframes-vector/{frames,final-frames}。

4K High首轮4输入0输出：复制到V4L2后未归还框架输入缓冲，等待重排输出造成入口阻塞；process提交/泵完成后clear input.buffers（V4L2已有独立复制），新增C2PortActualDelayTuning::output=8。修复后指定组件300帧9960ms通过。随后接口发布Constrained Baseline/Main/High三项，High10仍拒绝；10参数、level5.1/High接受/High10拒绝及pool往返实机通过。自动选择首轮在第9个输出allocation触及旧8项import上限，错误返回NO_MEMORY；改有界LRU后完整300帧但14.899秒，缓存轮换拖慢，不记成功。

最终import和私有minigbm缓存协调为最多16项/192MiB、单allocation16MiB；import按最近使用淘汰，仍仅持独立AHB引用、不持queue block/CPU lock，超大或不可测大小跳过优化。主机实际unmap/destroy ASan/UBSan覆盖10000次复用、16×12MiB项数边界、12×16MiB字节边界及31次最终释放通过。新私有库eb31d4022c97e1ac70e4b396eeb7c5599d1f904730bbdd241704a6929bc967a8；旧b673保存在设备sq2-map-cache/libminigbm_gralloc-128.so。原stage/helper已更新，构建仍使用该固定stage；不要误把之前128MiB构建当当前版本。

最终自动选择4K High：hardware=true/software=false/profiles=3/format_supported=true，300输入/输出/渲染，9964ms，EOS/realtime/pass=true、STOP/RELEASE/RESTORED；9个唯一输出，结束释放9项112914432B，copy+sync1.621秒。当前RAM owner9a556a2c3ada89e53d476d7aa71d712ec52e265ae8b891d96d122379de7bc62b、wrapper bdf11d60…；全局minigbm仍c1a57082…。APK d076bfe3…为4K High版。日志成功和失败全取回out/video-decode/profile-playback；最终collector KMSG_END timeout120000ms、无运行collector，XML_REMOVED、boot3944/PID1978/2246/guardY保持。未更改默认启动镜像，native/PAS不重复装卸。

下一步：实际Bilibili接入、其它codec能力对齐及实验包持久化/重启回归。上述只是指定合成码流验证，非所有AVC工具/长片/最大重排深度认证；不能将完成AVC测试标为整个硬解目标完成。输出delay=8是当前有界实验值，尚未按SPS动态推导。设备NDK frames探针已换成High向量e3f88650…，后续比较必须用high-bframes-vector/ref.nv12，不能误用原baseline参考。

**自动选择4K30通过（2026-10-29，最新）**：普通APK通过MediaCodecList查询，hardware=true/software=false、Constrained Baseline profile一项、format_supported=true，findDecoderForFormat自动选中c2.sq2.avc.experimental；3840x2160/30fps/300帧输入、输出、渲染全部完成，9960ms，EOS/realtime/pass=true，STOP_OK/RELEASE_OK/RESTORED。这仍是合成AVC测试，不代表Bilibili、Main/High、HEVC/VP9或默认启动完成。

首轮自动枚举在VideoCapabilities.updateLimits抛出Range上下界异常并触发JNI SIGABRT，仅测试APK崩溃；设备boot39441275-d887-4e17-a774-9e1fad93cdd3、框架PID1978/2246未变，用户确认屏幕可操作。接口补齐profile/level与input video/avc、output video/raw MIME。只公布Constrained Baseline、最高level5.2；起初仅接受5.2导致configure拒绝框架传入5.1，补齐标准较低level后通过。接口实机9参数、5.1接受、High拒绝、尺寸错误回退及block pool往返通过；不把level范围视作每档性能认证。XML实验尺寸更新到320x240–3840x2160。

当前RAM owner SHA516f682393e6db1fadaa17b800b730cc1fcefdeb8967036964efa417f047d73e，私有wrapper/mapper不变，全局minigbm SHA c1a57082…保持。新APK be5bcfee…支持codec=auto；runner新增auto标签并读取全部logcat buffer（此前默认buffer日志为空）。最新完整证据已取回out/video-decode/auto-selection/；collector smoke通过，显式停止后KMSG_END reason=signal/52613ms，XML_REMOVED、guardY、boot/PID保持。当前正常启动镜像未改，native/PAS保持驻留，不重复装卸。下一步真实应用与其它profile验证、补齐持久化包及重启回归；目标仍未完成。

**4K30同一会话60秒通过（2026-10-29，最新）**：测试APK loops=6重复10秒资产，仅推进输入PTS、不重建decoder/flush，1800输入/输出/渲染，EOS/realtime/pass=true，59961ms，STOP_OK/RELEASE_OK。始终4个唯一输出缓冲，结束IMPORT_RELEASE count4/50184192B，copy+sync6.196s（含DMA2.063s）。这是一分钟合成baseline测试，不是数小时稳定性/真实B站/其它profile/codec证明。当前APK SHA237cc787…，脚本43936f46…已设备哈希核对，owner仍0d7dcc2d…。

SSH runner收尾connection reset，随后只读连接取得上述完整最终结果，XML_REMOVED、boot3944/PID1978/2246保持；collector随后显式kill。证据取回目标out/video-decode/long-play，源/data/local/tmp/sq2-player-2160p-long-39441275-d887-4e17-a774-9e1fad93cdd3及sq2-long-kernel.log。下一步自动选择和真实应用兼容、能力广告/profile核验，再更新实验启动包/重启持久性；当前正常启动仍未启用新硬解，目标不可标完成。

**4K seek/flush通过（2026-10-29）**：组件新增独立configured_配置副本，flush关闭硬件会话后恢复SPS/PPS到待发送缓存；正常stop/reset仍清空。新的连续CSD序列替换旧配置，配置错误清两份。AIDL与独立组件构建通过。普通APK先显示60帧，再MediaCodec.flush+Extractor seekTo(5秒,PREVIOUS_SYNC)，实际同步点4.933333秒；输入215（含flush丢弃3），输出/渲染212，EOS/STOP/RELEASE正常，7000ms/realtime/pass=true，跳转后拒绝小于同步点的旧PTS。首次播放器错误要求精确5秒，已纠正，不能记为驱动失败。证据out/video-decode/seek/sq2-player-2160p-seek2-39441275-d887-4e17-a774-9e1fad93cdd3。当前owner0d7dcc2df26dbe3ef2dcd4db68aa419a460d745102c3f22f88a6d9025d94004d，private wrapper/mapper保持。

**4K通过轮收尾已核对**：完整结果、两张截图及kernel log已取回out/video-decode/import-retain/；晚期截图可见测试图样，非4K逐像素证明。collector实读KMSG_END reason=timeout/120000ms，已结束。boot3944、system_server1978、SF2246、guardY保持，XML_REMOVED，全局minigbm仍c1a57082…。服务继续以私有候选待命；此覆盖下方收尾尚待核对说明。

**4K30首次实时通过（2026-10-29，最新）**：组件新增有界独立AHardwareBuffer CLONE导入引用（最多8个/128MiB/单FD16MiB，按dev+inode去重），只在私有映射缓存开启时使用。保留分配对象而不持有C2输出block/队列槽位、不持续CPU lock；stop清理pool后释放所有import。配合上一轮私有minigbm缓存，使缓存映射能跨每帧C2导入对象销毁存活。RAM实际owner SHA95f3f6122c58d671064330c10e5baa3738702e51a3dce13223cb85c7ff627da1，wrapper仍bdf11d60…，私有库b6734690…。

先Surface独立CSD六帧全字节匹配FFmpeg（out/video-decode/import-retain/six），再普通UID APK 3840x2160/30fps/10秒300帧：inputs/outputs/rendered=300、EOS、realtime=true、pass=true，9962ms，STOP_OK/RELEASE_OK/RESTORED。copy+sync由16.909s降至1.129s（Y0.540s、UV0.265s、DMA0.323s），fetch5.088s含输出池等待；4个唯一输出buffer，停止记录IMPORT_RELEASE count4/50184192B。证明本测试4K30实时通过，仍不是长期稳定/seek/flush/高profile/HEVC/VP9/B站自动选择或默认启动完成。上限/错误路径与实际释放长期检查仍需完善。

当前服务PID8362私有namespace，native驻留，不重载PAS。日志/data/local/tmp/sq2-player-2160p-import-39441275-d887-4e17-a774-9e1fad93cdd3/及sq2-import-kernel.log；收尾kill/check与SCP正在遭遇间歇连接超时，需核对本地取回与collector有界结束，不能据发出命令记为已完成。启动镜像仍旧实验service，全局图形库未主动修改。下一步先归档/核验画面与系统状态，再长播放/seek/flush、正常应用选择、持久化包；不要因4K单轮通过标记全目标完成。

**映射缓存真实4K已完成但未提速（覆盖下方尚未启动状态）**：网络恢复后chmod755 owner并启动成功。私有服务PID23569实际/root/vendor/lib64/libminigbm_gralloc.so=b6734690…，环境SQ2_VIDEO_MAP_CACHE=1；Android init全局库仍c1a57082…。4K300帧/渲染/EOS/STOP/RELEASE完成，20971ms，copy+sync16.909s，仍pass=false/realtime=false；OUTPUT_UNIQUE=3。collector已kill，runner返回RESTORED。不能把独立buffer的缓存收益当作实际播放性能修复。下一步观察每帧C2GraphicAllocation/mapper导入对象是否被销毁，导致BO最终释放清掉缓存；必要时仅保留独立import引用来维持映射对象生命周期，不能持有BufferQueue输出block阻止归还，也不能持续CPU lock绕过消费者同步。该销毁解释目前仍是待验证假设。

服务目前运行私有wrapper→owner，默认镜像未改，native/PAS不重载。日志设备/data/local/tmp/sq2-player-2160p-map-cache-39441275-d887-4e17-a774-9e1fad93cdd3/与sq2-map-cache-{private,off,kernel}.log，取回目标out/video-decode/map-cache-results。后续核对实际文件，不把网络不稳定下的SCP命令发出等同于归档完成。

**映射缓存候选私有A/B通过，真实4K尚未启动（2026-10-29）**：新增prepare/build-sq2-minigbm-map-cache，固定minigbm455b745…/drv.c6655bfd5…与donor1ab63ce8…。候选C桥b673469018e0dcc7eb3ba238f2e36681a3265c2399db2682b958b75f2e10d210，默认关闭，SQ2_VIDEO_MAP_CACHE=1仅实验进程启用。仅MSM/无bo_flush/非cached/非protected/线性NV12/RW/单映射≤16MiB；每driver最多16缓存项、128MiB，附加一个mapping引用，普通unmap继续减活动引用，BO最终destroy仍释放。WC发布前dsb sy，原mapper fence/锁解锁不绕过。helper与实际源抽取unmap/destroy的ASan/UBSan测试覆盖默认关闭/拒绝条件/10000次复用/预算满拒绝/18次最终回收，通过；不代表所有并发或导入格式同步认证。

LD_PRELOAD试验因sphal命名空间缺cros_gralloc_driver::retain符号而只使独立probe abort，未改系统库；已改用sq2-map-cache-private.c新mount namespace+根递归private+单库bind，只有该子进程看候选。memory-probe开启时两次重新map首写858/865us、0minor faults，所有字节正确；关闭时35521/35555us、2025faults。系统库仍c1a57082…，框架PID1978/2246保持。日志在设备/data/local/tmp/sq2-map-cache-{private,off}.log，尚未全取回。原owner/remap取回仍因间歇网络失败，不误记已归档。

准备真实4K：RAM /vendor/bin/sq2-c2-experimental已换私有启动器SHA bdf11d60a20e2433dbb8c5fb84e06a15b02806785979dcb0f802aae4d5408183；--serve应exec/data/local/tmp/sq2-c2-owner（b113c5d4…）并设置缓存。**最后观察init服务stopped，初始/data临时owner文件缺执行位；后续chmod/start命令SSH均超时，不能认定已执行。** 下步先连接，chmod755该已核验owner，启动并核对PID/exe及进程私有库哈希/namespace，再启动独立collector验证ready/smoke，运行test-c2-player.sh 2160p-map-cache。该label测试还未启动，sq2-map-cache-kernel collector命令也连接失败，不复用或猜日志存在；先核对。不要重载native/PAS。本boot仍3944，默认镜像未改；系统全局库始终原版。若缓存服务无法启动，RAM恢复/vendor/bin为/data/local/tmp/sq2-c2-owner即可回原候选。

**本轮收尾网络限制**：上述remap及owner结果已从SSH stdout读到，但最后SCP归档全部连接超时；out/video-decode/output-mapping尚无完整原始文件，不能按已取回处理。源仍在设备/data/local/tmp/sq2-c2-memory-remap.log、sq2-owner-kernel.log、sq2-player-2160p-owner-39441275-d887-4e17-a774-9e1fad93cdd3/。SCP/SSH进程均已退出，无后台传输；最后清理/boot/PID检查在失联之前完成。先恢复连接并取证，不据超时推断整机故障；此前用户确认正常不能替代当前屏幕观察。

**映射反复缺页与输出三缓冲复用已实测（2026-10-29，最新）**：memory-probe增加getrusage及同一graphic allocation两次unmap/map；8.29MB首次写40326us/2025 minor faults，每次重映射后35537–35807us/2025 faults，映射内后续约0.7ms/0 faults。真实C2新增前12帧pool/allocator/inode及总unique统计：pool17/allocator20，三个inode9871/9873/9875循环，300帧OUTPUT_UNIQUE=3，说明输出pool已实际复用。4K300帧20952ms，渲染/EOS/STOP/RELEASE通过但实时性失败；copy+sync16.847s。主要问题是重复映射/缺页，不再按“每帧新分配输出”解释。

恢复固定minigbm源码455b7456397dff01443584b09e2fd594079e905c至/root/.cache/sq2-video-minigbm-reference。drv_bo_flush_or_unmap在backend无bo_flush时调用unmap；MSM只有bo_map/bo_unmap，cros_gralloc unlock每次走此路径；drv_bo_mapping_destroy在BO最终释放时清理所有映射。kernel msm_gem_fault逐页vmf_insert_pfn，与测量一致。下一步构建仅实验解码进程启用的映射保留候选，必须保留cache/fence同步、限制映射生命周期/预算、验证最终释放，不能简单常驻C2GraphicView或盲目跳过unlock。尚未修改/替换minigbm或kernel。原minigbm构建输入目录已缺失，不可盲跑旧build-sq2-minigbm-bridge.sh覆盖现有库。

当前RAM服务b113c5d4dd129f4f36c8d5a9b7eb81609a85ff99ccf9c49223681c65fb177ab9；证据out/video-decode/output-mapping，collector停止，XML撤销，boot3944/PID1978/2246保持。默认启动镜像未改。

**连接恢复、首次gralloc写入慢已测得（覆盖下方失联状态）**：用户确认屏幕正常，SSH恢复；boot3944/PID1978/2246未变，guardY。逐轮allocation-only probe已运行并全字节检查：8.29MB cached→gralloc首次41207us，后续717–793us；system heap→cached约530us；gralloc→cached每轮约5.8ms。首次访问慢是实测，具体是否page-fault/缓存/映射开销尚未核实。不能把临时网络超时当作设备挂死。collector已结束，XML已撤销。日志取回out/video-decode/output-pool，pool轮累计copy+sync16.929s（Y11.137s、UV5.551s、sync0.240s）。仍未证明实际输出buffer复用，下一步记录pool local ID/allocator及每帧allocation身份，检查反复map/unmap是否重建PTE，再选择修复。pool-interface-test实机6参数及配置1234/query回读通过，尺寸合法/非法边界仍通过。RAM候选仍38267973…，未写启动镜像。

**输出池参数候选与测试后失联（2026-10-29，当前先取证）**：新增allocation-only sq2-c2-memory-probe.cpp，不打开video/PAS。8轮8.29MB复制全字节正确：cached↔cached/system heap约4.3–4.6ms总计，cached→gralloc45.5ms、gralloc→cached46.8ms、随后heap→gralloc6.0ms；提示首次映射访问可能昂贵，但逐轮计时修订只编译，未跑，不能定论。新增C2PortBlockPoolsTuning::output参数，使框架可配置输出pool ID；AIDL构建通过，pool参数往返fixture仅编译待运行。

RAM候选382679735c529b041a07391f8ee4963bf99571a4b8f141ea46ba0fe09e358aad实测4K：300输入/输出/渲染，EOS、STOP_OK、RELEASE_OK、RESTORED均返回，21034ms，仍pass=false/realtime=false。不能据约1.2秒差异认定pool已实际复用；需取回logcat核对pool ID/类型及累计复制时间。测试随后SSH连续超时，用户已被询问屏幕/IP，尚无当前设备状态回答；不能判定整机挂死或自动重启。最后已知boot3944。本轮collector在/proc/1/root/data/local/tmp/sq2-pool-kernel.log，120秒/2MiB有界；显式kill连接失败，待核对终止状态。日志目录/data/local/tmp/sq2-player-2160p-pool-39441275-d887-4e17-a774-9e1fad93cdd3，尚未取回。逐轮memory-probe传输失败，旧设备binary不代表新源码。恢复后先读boot/上述日志/pstore，不重载固件，不盲目重跑。默认启动镜像未更改。

**复制微优化未提升4K（2026-10-29，最新）**：分离DMA同步/亮度/色度计时，并加入有边界的ARM NEON行复制/UV拆分/UV交换helper。ASan/UBSan主机与静态ARM64 QEMU遍历长度0..2050、16种源对齐、尾部及目标哨兵通过，AIDL构建通过。实机布局NV12（cols1/2/2，U/V指针差1，root1/1，offset0/1），原代码已走memcpy；平面及VU优化未命中，不能当作性能改善。显式向量行复制同样未改善：4K300帧22253ms，pass=false/realtime=false，EOS/STOP/RELEASE通过。累计copy+sync18.166s，其中DMA同步0.242s、Y12.347s、UV5.576s；下一步分别测量源读/目标写与内存映射属性，不再猜测色度转换或VPU时钟。

当前RAM服务SHA7d178dfa1b5e62604e0b25a148f9c86898b071fb4a96101adb14342ceac62902。此版本Surface独立CSD六帧再次逐字节匹配参考；不等于4K逐像素验证。证据out/video-decode/fixed-capture/sq2-player-2160p-{neon,vu,vector}-39441275-d887-4e17-a774-9e1fad93cdd3及vector-six。boot3944/PID1978/2246/guardY保持，XML撤销、有限collector停止，启动镜像未改。helper目前保留为已验证但无性能收益候选，不标为4K修复。

**4K耗时定位（2026-10-29）**：计时候选ae0f01160a605fa8138a4504bc4e5ae98d2df940e71d88efee545e37bb5f53dc已替换RAM服务。新APK实测300帧22230ms，正确判定pass=false/realtime=false，EOS/STOP/RELEASE通过。累计fetch0.283s/map0.090s/复制含DMA同步18.176s/unmap0.034s，优先分离同步与复制并优化像素搬运；没有证据需提高VPU时钟。证据out/video-decode/fixed-capture/2160p-timing，XML已撤销/collector停止/boot与框架PID保持。本轮用户确认现在可操作，旧黑屏反馈不是新故障。最新候选仍仅RAM部署，未写启动镜像。

**固定capture池修复1080p释放；4K尚不实时（2026-10-29，覆盖下方旧状态）**：video fd close的内核栈定位到drm_gem_put_pages/dma_buf_release/__fput。旧组件每帧更换gralloc capture，与驱动必须保留映射直到session END的安全策略叠加，造成分配/回收随帧数增长。保留驱动退休隔离及预算，组件改为固定dma-heap capture池，DMA同步后复制到独立C2输出；这是硬件解码加一次CPU像素复制，不是零拷贝。主机原替换模式及复用100帧仅5次分配的ASan/UBSan通过，完整AIDL构建通过；实机Surface独立CSD六帧与参考逐字节一致。

RAM候选89ab6dc6272eb94d435fe5a3f3f240409ab1d0e1c460ffe7aa5d97d0244297df：1080p30/300帧全部输出及渲染、EOS/STOP/RELEASE通过，耗时9963ms，固定9个3MiB capture；fd close约7ms。4K3840x2160/300帧全部输出/渲染和释放通过，但10秒视频耗时22295ms（约13.5fps），**不是4K30通过**；旧日志pass=true只判断完整性，播放器源码已补实时性判定，尚待新APK实测。证据out/video-decode/fixed-capture/{six,1080p,2160p}。新增输出fetch/map/copy+sync/unmap累计耗时诊断仅编译，尚未部署。

用户澄清“卡住或黑屏”为之前SPS/PPS失联，当前可操作；只读核对boot39441275-d887-4e17-a774-9e1fad93cdd3、PID1978/2246、guardY、Android完成1保持。当前native驻留且本boot已PAS一次，禁止重载。临时media XML已撤销。实验启动镜像仍旧服务，RAM修复未持久化；默认启动保持正常闭源。最新fetch远端仍c19ba9b，集成修改在工作区。下一步测量4K分配/映射/复制耗时并完善输出pool，不能据完整帧数宣称实时性、B站兼容或全目标完成。

**720p通过；1080p关闭超时定位到video fd close（2026-10-29，最新）**：新增只读sq2-v4l2-capabilities.c实查H264尺寸96..4096两轴、step1，profile列表Baseline/Constrained Baseline/Main/High/Stereo/Multiview（仅报告非逐项验证）。AIDL接口实验范围改96..4096 step2；临时XML按每轮尺寸上限广告，仓库默认XML仍320x240。普通APK 1280x720/30fps/10秒300帧完整播放、300渲染回调/EOS/release0通过，boot3944/PID1978/2246保持。

1080p首轮300帧及300渲染/EOS通过但stop报错，后续诊断两轮为299渲染/300输出，均STOP_ERROR code=-2147483648、release仍执行，不能算完整通过。真实C2 onStop stateEnded/pending0，STREAMOFF两队列+输入unmap约3ms，**close(video fd)耗时3.69秒**，capture leases释放约4ms；整个session close最终返回0，但框架3.3秒先报UNKNOWN_ERROR。当前候选SHA24cc24083e466a27ba28efa4fc991234c9d8f13796bd7be195654e4361fe7cdd在RAM vendor驻留，包含close-stage Android日志；内核未改，无PAS重启。证据out/video-decode/player-app-1920x1080/close-trace及720p/hardware。所有临时XML已清理、测试应用已停止、collector结束，系统PID保持。下一步针对fd close采样内核栈/分析vdec_close及vb2释放，修复耗时后再继续4K；不要只调高超时或把1080p画面出现当作完整成功。当前未发现可用/sys/kernel/{debug/,}tracing/available_tracers路径。

**普通APK可见硬件播放300帧通过（2026-10-29，最新）**：新增probe/video/player-app、build-sq2-video-app.sh和test-c2-player.sh。无privileged权限的Java MediaExtractor/MediaCodec/SurfaceView应用（当前user10，UID1010254）播放FFmpeg生成320x240/30fps/10秒/300帧AVC baseline MP4，显式选择实验codec。软件与硬件采用同一按PTS限提前10ms提交的播放器均inputs300/outputs300/rendered300/EOS/pass=true、release成功；硬件耗时约9964ms，两张截图含变化中的测试图样及烧入时间/帧号。证据out/video-decode/player-app/hardware-paced。当前boot3944/PID1978/2246保持、guardY，临时XML已清理/缓存刷新，应用force-stop撤销KEEP_SCREEN_ON，native仍驻留。

保留限制：最初不限制render-ahead时，软件自然节流而硬件约1秒即解出300帧，只59次渲染回调、屏幕提前到最后帧。新增应用PTS节流使正常可见播放通过，但不是组件buffer-pool/backpressure已修复；需继续补接口/输出pool等参数，不能靠测试播放器证明所有第三方应用兼容。当前XML只广告320x240，接口fixture最大640x480，不代表硬件极限。下一步基于真实V4L2能力补参数、验证更高分辨率及长播放/seek/flush，然后接B站与4K，仍未完成全目标或持久化默认启动。

**独立CSD硬解与Surface/ImageReader像素均通过（2026-10-29，最新）**：经核验BC711序列号/ESP B027-0512/marker/mapper root，使用既有一次性env启动实验镜像a5038428…，boot39441275-d887-4e17-a774-9e1fad93cdd3，guardY、Android1、PID1978/2246，一次性env已消耗0。停止空闲实验服务，在RAM vendor原子替换为CSD候选SHA8382c98d4fb92db3f892b91419bc160a37a4d90874fb42837dcf3a3128a84ae0后启动；持久镜像仍旧服务。独立collector ready/smoke后本boot唯一一次video-decoder-lab-start成功，native驻留，不再PAS或热卸。

先--csd-stripped真实硬件六帧/EOS/全部像素通过；再新增--surface-csd，AMediaCodec配置AImageReader的ANativeWindow，releaseOutputBuffer(render=true)，消费实际三平面图像，按row/pixel stride打包I420。软件对照及硬件各六帧全字节匹配FFmpeg参考，Image timestamp_ns为0/33333000/66666000/99999000/133332000/166665000，尺寸320x240，crop全幅，EOS正常。证据out/video-decode/ndk-vector/{hardware-csd-stripped,software-surface,hardware-surface}。两硬件轮collector有界并正常signal结束，media XML已删除且缓存刷新，boot/PID不变；Surface轮内核日志无检出的fault/panic/HFI错误。仍不是可见SurfaceView/播放器/长视频/4K证明，不宣称零拷贝或旧黑屏根因已修复。当前实验服务新候选与native驻留；默认下次启动仍正常闭源。下一步普通APK可见Surface播放、完善能力参数和真实分辨率/长视频测试，再持久化新候选。

**AIDL修订候选已构建；去除in-band配置的软件对照通过（2026-10-29）**：恢复固定hardware/interfaces/native源码、生成144个AIDL头，包含CSD缓存修订的AIDL服务编译成功。设备正常boot1486上不带--serve执行候选：STORE_STATUS0、LOCAL_LIST一项c2.sq2.avc.experimental/video/avc、exit0；不注册、不替换服务、不启硬件。NDK帧探针新增--csd-stripped，将28B SPS和9B PPS仅置于csd-0/1，从AU中剔除，首AU5039变5002。c2.android.avc.decoder实际六帧/EOS通过、独立FFmpeg参考全字节一致，证据out/video-decode/ndk-vector/software-csd-stripped。旧--csd保留in-band的行为仍保留作不同试验，不要混称。当前仍正常启动、无video0，PID2058/2350未变。下一步在实验启动中使用新AIDL候选（out/video-decode/codec2-integration/aidl-store/interface-test），对--csd-stripped做有界且持久日志的真实硬件测试；旧boot bundle仍含旧服务，不能误以为新二进制已安装。Surface/应用/高分辨率仍未验证。

**设备恢复、配置work实机完成（2026-10-29，覆盖下方受阻状态）**：SSH恢复，boot1486cdb1-6666-45df-a94b-6b1d5d9189c2，Android1/guardY/nohibernate，正常闭源启动项无experimental参数；system_server2058/SF2350。实验服务/XML/video0均不存在。CSD日志确为0字节、pstore空，当前tombstone为恢复后Trusty/gscd/init子进程噪声，没有旧测试阶段证据；不能确定旧CSD探针执行到送帧或认定其为卡死根因。取回实际库并通过原固定哈希，恢复平台头后包含CSD修订的完整组件测试程序已编译。新增--config-only在无/dev/video0的正常系统运行：两项配置work callback result0/processed1、无图像/EOS、stop/release0、exit0；boot/PID不变。该模式使用不透明配置字节，只验证C2配置缓存/回调所有权，不验证SPS/PPS语法或硬件。证据out/video-decode/codec2-integration/component-backend/config-only.log。尚未替换AIDL服务或镜像，下一步恢复AIDL构建依赖，再测试修订候选配置+真实帧及Surface路径；不盲重跑旧CSD实验。

**用户确认设备卡住或黑屏（2026-10-29，最新状态）**：用户回应独立CSD试验失联询问，明确报告“设备卡住或黑屏”；不再仅视为SSH网络故障，但尚无内核/固件根因证据。已请用户强制关机后选择默认闭源Vulkan正常启动项恢复，未收到恢复确认；随后只读SSH检查仍超时。恢复后先读取持久/data/local/tmp/sq2-hardware-csd.log、pstore、boot ID及服务状态，不运行旧CSD测试、不自动加载后端。人工恢复重启不能记为自主崩溃。CSD缓存修订仅主机helper测试通过，完整组件尚未编译/部署；构建脚本实际因缺设备匹配libcodec2_soft_common.so而停止，不能拿Quest2的同名库顶替。

**MediaCodec硬件六帧像素通过；独立CSD试验失联待取证（2026-10-29）**：已fetch origin，当前codex/sq2-video-decode与远端c19ba9b一致，后续集成改动在工作区。早期VINTF/init实验服务已在候选启动生效，系统Binder的default1可发现；临时media_codecs_c2.xml加media服务刷新解决列表准入。独立NDK探针必须启动ABinderProcess_startThreadPool，否则软件对照也会卡在输入队列。修正后软件六帧及硬件c2.sq2.avc.experimental六帧均输出320x240 I420、正确PTS和EOS；转换NV12后与FFmpeg独立参考逐字节一致，十二CRC与原native记录一致。硬件成功日志和六帧保存在out/video-decode/ndk-vector/hardware-output，校验脚本scripts/verify-sq2-mediacodec-frames.py。成功轮boot434055bb，实验退出0、软件创建对照0、RESTORED；不等于Surface/播放器/4K支持。

**当前需先恢复连接取证**：随后test-c2-media-refresh.sh --frames --csd（独立csd-0/1）运行期间SSH reset，重连超时且ping不可达。日志写在设备/data/local/tmp/sq2-hardware-csd.log，尚未取回，不能推断板级重启或解码失败根因；XML回退也尚未复核。最后成功观测system_server2001/SF27412、guardY、native core驻留、init服务running。恢复后先读boot ID/该日志/进程/XML，按实际状态恢复临时XML，禁止重新PAS或热卸provider。用户已被询问设备屏幕/网络状态。原默认启动镜像未提升；当前候选native-c2-experimental的一次性env已消耗，下次正常启动回原闭源入口。独立CSD、Surface、完整参数/异步输出/长视频/高分辨率仍待完成。

**AIDL实验注册/跨进程发现通过，MediaCodec发现未完成（2026-10-29）**：新增sq2-c2-aidl-service.cpp实现C2ComponentStore并使用设备libcodec2_aidl_noisurface.so包装；新增sq2-c2-aidl-client-test.cpp独立Binder客户端。固定hardware/interfaces 1a56e38edc2f2f6189ef405ee1edce554e15cbc0、frameworks/native 4f463a6b1de9198963dc6aff74154a504ba3f8f6，冻结C2 v1接口与bufferpool/common等生成NDK头；实际设备库报告AIDL v2/hash2f7448ba8211c5cd1de7bd6c17fe7c5fd034ebbe。首轮未传--hash导致生成头缺getInterfaceHash虚函数，asBinder错位至getInterfaceHash、独立进程139；添加冻结版本/hash后修复，非板级崩溃。wrapper sizeof352与设备QTI provider分配大小一致，仅局部ABI证据。正常default1注册返回-3/exit5；该实例未VINTF声明。显式--serve-local降为本机稳定性后临时default1注册0，独立客户端跨进程listComponents返回c2.sq2.avc.experimental/video/avc，createInterface/getConfigurable非空、client exit0。**这只是实验AIDL发现，不是MediaCodec发现完成：同轮AMediaCodec目标创建仍NULL/exit2，软件对照成功；框架按VINTF声明发现store。** 12秒有界服务正常timeout124后已核实SERVICE_ABSENT、仅原default/software保留、PID1968/2172不变。没有改VINTF/XML/默认服务/启动盘/固件。下一步实施可回滚VINTF实例声明与服务发现刷新，再补必要组件参数并测试MediaCodec创建/解码/Surface；不要把--serve-local作为正式集成或安全策略配置。证据out/video-decode/codec2-integration/aidl-store/local-live.txt。用户要求保存并推送；本轮保存源码/构建准备脚本和状态，raw logs/二进制/密钥留out/private。

**独立C2组件真实帧回调通过（2026-10-29，本轮提交记录）**：新增sq2-c2-v4l2-component.h将process读取C2Work linear block→通用V4L2 session→C2BlockPool graphic block→C2Buffer回调连通；后端不含向量、不fork旧runner。测试向量仅在sq2-c2-component-test.cpp。a101同boot实测六输入C2Work+单独EOS，七回调result0、六帧全部12CRC匹配、ordinal校验通过、mask3f/eos1/errors0，stop/release0，客户端保留图形buffer到组件关闭后再释放；SSH0/PID1968/2172不变、guardY/pstore空。首轮collector smoke前缀被过滤，脚本exit1且未启动组件；改白名单前缀与新v2日志后通过。补充四角色BASELINE/ROLLBACK旧队列探针返回OMITTED38、MODIFIED真实解码，均程序exit0；工作负载不同，不称三次硬件解码。证据out/video-decode/codec2-integration/component-backend。**仍非MediaCodec可用服务：AVC单路径、接口五参数/fixture范围、未AIDLv2注册、未Surface显示；动态分辨率变化拒绝，drain()仍OMITTED，输出pump依赖process而无独立事件线程，codec-config/partial-frame/reorder/异常/重启矩阵待补。下一步补组件生命周期与异步输出/参数能力，并接AIDLv2独立store与MediaCodec端到端；不要用通过向量推断其它codec能力。** 用户要求完成本轮后提交，保持private/out/密钥/原始日志不入库；没有改启动盘或系统媒体服务。

**V4L2通用会话后端候选（2026-10-29）**：新增probe/video/sq2-v4l2-session.h，不含向量/CRC/固件装载，open(codec,width,height,allocate,deliver)、submit实际字节与opaque token、pump、requestDrain、close。输入MMAP槽，输出由外部分配DMABUF owner，DQ交付旧lease后申请新lease才requeue，避免覆盖客户端帧；SOURCE_CHANGE协商真实NV12几何/最小buffer数，EOS待输入归还后STOP/LAST，错误sticky，close先streamoff/close再释放owner。动态分辨率变化暂明确ENOTSUP，不伪报支持。tests/sq2_v4l2_session_test.cpp实际头+wrapped syscalls交叉编译/QEMU通过：任意5字节输入、token123456789、替换lease、EOS、替换ENOMEM保留客户端owner、close后客户端释放，均非硬件。NDK35 -Wall/-Wextra/-Werror编译通过；首次g++不存在exit127已改用aarch64-linux-gnu-g++。旧固定runner未改。补充四角色以新头文件空基线表示API不存在，modified mock通过，rollback恢复空头；不要混称设备回滚或三次硬件试验。证据out/video-decode/codec2-integration/session-backend。**下一步：实现C2组件allocate回调从C2BlockPool fetchGraphicBlock/map检查真实stride/offset/容量，deliver用C2Buffer及work ordinal完成pending work；process读取C2Work输入linear block提交，不再fork固定向量runner。先补mock失败边界/编译，再有界实机组件解码验证。** 当前QueueProbe仍OMITTED，尚无可注册组件或MediaCodec证明。

**真实C2工作队列基础类通过（2026-10-29）**：接口类分离到probe/video/sq2-c2-interface.h，原接口测试保持；新增sq2-c2-queue-test.cpp继承设备实际libcodec2_soft_common的SimpleC2Component，独立进程start→queue_nb移交一项C2Work→worker回调→stop/reset/release实际通过exit0。process明确返回C2_OMITTED38且不输出帧，**没有接解码后端，不是可用解码器**。使用system优先、swcodec APEX其次的进程局部LD_LIBRARY_PATH，不改全局或服务。基础头实际位于同固定av提交media/module/foundation/include，修正初次找错路径编译exit1。四角色自包含源码BASELINE/ROLLBACK接口测试、MODIFIED队列测试全部实际exit0，patch重建/rollback hash一致；a101/guardY/PID1968/2172不变。证据out/video-decode/codec2-integration/component-queue。**下一步必须将V4L2后端从固定向量runner重构为接受C2Work实际输入的会话，输出由C2BlockPool提供并以C2Buffer/ordinal回传；不能把OMITTED占位或固定六帧子进程包装当作组件完成。** 同时补齐MIME/profile/allocator/pool参数和实查能力，之后AIDLv2独立store及MediaCodec端到端。当前未注册、未启动固件、未重跑小向量。

**C2组件参数接口初步实测（2026-10-29）**：新增sq2-c2-interface-test.cpp，真实链接设备libcodec2_vndk/libcodec2和平台libc++，SimpleC2Interface+自定义C2InterfaceHelper建立kind/domain/input LINEAR/output GRAPHIC/picture-size五参数，未注册服务。独立fixture范围320..640/240..480不是设备能力或最终范围。发现官方validatePossible仍TODO返回OK；初版接受641并assert退出134。改supportsAtAll/保留旧值后又发现C2R::plus不合并status（源码TODO mStatus），故OK.plus(BadValue)仍ret0但failure1；现用第一项BadValue初始化result，后续只追加失败。实测合法640x480成功；641返回22/failure1、查询仍640x480、exit0；baseline/rollback同输入重现assert134，补丁重建/回滚hash一致。初次RTTI链接失败改-fno-rtti，未改系统库。a101boot/guardY/PID1968/2172不变，未碰固件或服务。证据out/video-decode/codec2-integration/component-interface。**下一步：将接口类分离供组件复用，补齐真实能力来源及必要MIME/profile/level/allocator/pool参数，实现C2Component状态/工作队列/回调接入已有V4L2/gralloc后端；再独立组件测试与AIDLv2注册/MediaCodec端到端。** 不把五参数测试称为完整组件，不把fixture范围写进生产XML。

**C2 gralloc→V4L2真实帧通过（2026-10-29）**：a101同boot，图形布局探针实测320x240及320x256均159744B，row384，UV偏移98304/98305，前两fd同inode/dev，另metadata fd12288B；288高容量180224、UV110592，不能通用硬编码。新增sq2-c2-v4l2-bridge.cpp为当前向量保留九个真实DEFAULT_GRAPHIC对象，unwrap公开API后校验布局/容量/同buffer引用，dup到200..208给独立静态runner；runner --graphic-fds仅当前9buffer/159744B/384stride/256scanline，输入仍systemheap。**一次真实六帧全部12CRC匹配、EOS无ERROR、双STREAMOFF/close0、父进程C2_RELEASE_DONE child_status0、BRIDGE_EXIT/SSH0**。boot/PID1968/2172不变，guardY/pstore空，无固件重启或服务替换。完整证据out/video-decode/codec2-integration/graphic-layout/native-result.json及持久kmsg/stdout。布局BASELINE/ROLLBACK单240、MODIFIED三尺寸metadata均实际exit0；非三次解码。旧mock通过，新继承fd分支仅正向实机覆盖，失败注入待补。**下一步：实现真实C2Component/C2ComponentInterface配置/工作队列/输出block与回调，接实际AIDLv2 ComponentStore（libcodec2_aidl_noisurface），独立测试组件后再MediaCodec注册/解码/Surface显示与逐codec能力矩阵；不要继续用小向量重复替代组件接入。** 当前桥接仅实验进程，不是MediaCodec可用服务；保留系统软件回退，不假报XML能力。

**Codec2分配器真实调用通过（2026-10-29）**：独立sq2-c2-allocator-test.cpp用固定AOSP C2头、NDK r28c libc++头私有__1命名空间副本及设备实际libc++.so构建（gralloc枚举来自Mesa26.2.4 android_stub，无链接Mesa allocator）。a101同boot通过dlopen实际libcodec2_vndk、GetCodec2PlatformAllocatorStore、DEFAULT_LINEAR 4096B分配/map/写读368640/unmap/release；扩展DEFAULT_GRAPHIC format0x23 320x240分配/map三平面成功，row384 col1/2/2 depth8，unmap/release0。BASELINE线性、MODIFIED线性+图形、ROLLBACK恢复线性均实际exit0；patch重建/回滚hash一致，guardY/PID1968/2172未变。不改服务/固件/启动盘。仅局部ABI/分配器证据，不是完整ABI或MediaCodec解码证明。证据out/video-decode/codec2-integration/allocator-transaction。**下一步：检查C2 gralloc native handle及平面offset/容量，与V4L2 CAPTURE所需stride384/scanline256/size159744对接，验证真实解码帧写入Android图形buffer；然后构建AIDLv2组件服务与MediaCodec端到端测试。不能直接假定当前可见240高分配有256行容量，不修改已有媒体服务或假报codec能力。**

**原生DMA-BUF数据路径通过（2026-10-29）**：在a101c096-7d7e-4464-94c2-28d70b521b94同boot驻留固件上，runner新增--dmabuf：两队列使用system dma_heap进口fd、输入WRITE/输出READ显式CPU同步、失败逐项释放fd/mapping。实际六帧H264 320x240全部12CRC匹配、输入归还6、EOS空LAST flags0x104000无ERROR、两队列STREAMOFF及close完成，decode/SSH exit0；PID1968/2172与boot不变、guardY、pstore空、IRQ161。未重启固件、未改启动镜像。host ASan覆盖六fd/sync/close及第三次分配失败退回前两项；补充四角色patch重建/回滚哈希一致，BASELINE/MODIFIED/ROLLBACK同CRC自检均exit0，DMA-BUF源码分支False/True/False，非三次硬件对照。证据out/video-decode/venus-transport/dmabuf-frames/native-result.json及VERIFICATION.txt。Android provider实际PID1947 media.hwcodec，对应vendor.qti.media.c2@1.0-service，链接AIDL C2 V2与libcodec2_aidl_noisurface.so；另一个libcodec2_aidl.so链接V1，不能误用。官方v4l2_codec2固定af9fcc0819b689786af44c2711320414b29b01a3；frameworks/av固定e2f098935447ca4945946de5cb69db843fe3f003，缓存/root/.cache/sq2-codec2-av-reference。**下一步：匹配实际平台C++/C2 ABI，构建独立allocator/graphic-buffer接入验证，再接AIDL组件；保持现有媒体服务/软件回退。system heap成功不等于gralloc/AHardwareBuffer或Android MediaCodec接通，也不证明其它codec/长播放能力。** 不要重复小向量代替Android接入，不在本boot重新PAS或热卸provider。

**原生EOS修复及双会话实测通过（2026-10-29）**：preparer仅为SQ2把vdec_buf_done中空LAST从VB2_ERROR改正常DONE，非LAST空帧/legacy保持原样，后续HFI DATACORRUPT/DROP仍ERROR。新增test_sq2_vdec_buf_done.py抽取实际函数，128组合+missing-owner，BASELINE/ROLLBACK空LAST ERROR、MODIFIED DONE，均程序exit0；patch重建/rollback hash一致。全旧mock/三模块编译/5生成回归通过，测试已接build脚本。正常重启后boot a101c096-7d7e-4464-94c2-28d70b521b94，guardY/Android完成，11模块live符号闭包0/传输hash匹配，仅首次PAS。**相同严格runner（未放宽ERROR检查）连续两次独立会话每次6帧/12CRC全匹配、输入归还6、EOS event及LAST flags0x104001无ERROR、两queue STREAMOFF0/close完成，round与SSH全exit0**。IRQ102、PID1968/2172与boot前后不变；启动镜像未修改，临时模块驻留，不再PAS/热卸provider。仅证明320x240 H264 baseline双会话，不代表长播放/其它codec/AndroidMediaCodec。证据out/video-decode/venus-transport/eos-last/native-result.json及ledger。转入Android接入：本机Android17/SDK37/CL3B.260622.273.R1，实际AIDL IComponentStore default/software、vendor manifest AIDLv2；已有QTI服务/库未改。已克隆官方v4l2_codec2到/root/.cache/sq2-v4l2-codec2-reference，固定af9fcc0819b689786af44c2711320414b29b01a3，其service实际为HIDL1.2，不能直接冒充AIDL替换。**下一动作：检查实际AIDL provider依赖与开放v4l2_codec2组件/allocator构建接口，准备匹配Codec2/AIDL构建输入和NV12/DMA-BUF桥接验证，再注册真实组件；不靠XML假声明能力。** 保留默认软件回退，native kernel基础暂不再重跑小向量。

**原生V4L2六帧CRC通过，EOS ERROR仍开放（2026-10-29）**：用户已授权继续数据路径，并更新目标明确允许直接操作Surface。bf6d同boot驻留core上，--queue-only实测6QBUF/close0；首次完整模式QBUF与STREAMON0，但runner误将DQEVENT的ENOENT当致命错误提前streamoff/close。固定内核v4l2-event.c确认空事件返回ENOENT，已修正两个event循环，实际main mock覆盖ENOENT/EAGAIN→SOURCE_CHANGE后清理。第二次收到source-change，runner多余CAPTURE S_FMT将crop240重置为aligned256；固定vdec_s_fmt确认，改为仅非NV12时S_FMT。第三次真正输出6帧320x240、stride384/scanline256，全部12个Y/UV CRC与独立ref匹配，输入归还6，STOP0，收到EOS event及空LAST缓冲flags0x104041（含ERROR）；严格runner仍exit1，未掩盖错误。两个STREAMOFF/close0；boot/PID2047/2213不变、guardY、pstore空，IRQ最终116，最后kmsg无invalid/fault/error筛选结果。原始ad8e失联根因仍不能由本次通过推定已修复。**下一动作：修复SQ2 vdec_buf_done的空LAST判错分支**（原代码所有!bytesused设VB2_ERROR），保留非LAST空帧及HFI DATACORRUPT/DROP的错误，实际函数测试/编译后验证正常EOS，再Android Codec2接入与多编码/尺寸能力矩阵。不能在bf6d重新PAS或热卸提供者；当前内核未做EOS修复，需新boot部署（用户已允许直接操作设备，仍核对状态/保护/保留启动盘）。六帧证据out/video-decode/venus-transport/qbuf-trace/native-result.json，runner SHA61ce8f49…，stdout SHA8e0c1966…；测试/角色源对照不等于三次硬件对照。

**原生初始化/申请阶段实测通过（2026-10-29）**：恢复后实核boot bf6d6bff-756c-457e-872c-dd0ddffbe711、guardY、Android完成、pstore空。已取回ad8e六帧日志，只有READY/smoke/audit，没有结束/驱动错误/帧标记，根因仍未知。用户随后明确允许“不送帧”的分阶段定位。preparer新增默认false/只读sq2_stage_trace，SQ2专用、atomic饱和128条，记录8处调用前后（PM、session init、input resolution、两类bufreq、queue counts/PM put），无额外MMIO/睡眠/重试；实际helper host ASan检查默认关闭/legacy零日志/1000调用只128条，5生成回归/全部旧mock/三模块编译通过。v1函数终点过宽导致anchor错误，v2收窄修正。runner新增--format-only、--buffers-only及配置ioctl前后fsync；实际main mock证明格式模式无REQBUFS，申请模式6 mmap，两者无QBUF/STREAMON、close一次。bf6d干净启动只启一次PAS，新overlay自动匹配decoder，无driver_override。真实format-only与buffers-only均exit0/close完成，6输入buffer共2949120B；16阶段日志ret0（SESSION_INIT、输入/输出需求均有回复），IRQ3→7，PID2047/2213与boot均未变，guardY、pstore空。**未送帧，不是ad8e挂起根因修复**。当前core/固件resident，不能再PAS启动/热卸provider。下一步审查并为QBUF、STREAMON/资源装载/首次输入/首个source-change建立有界定位，再获准推进数据路径；不要直接宣称V4L2帧/Codec2可用。下一阶段可复用本boot驻留core，不需重载固件。候选stage /root/.cache/sq2-venus-stage-trace-v2；证据out/video-decode/venus-transport/stage-trace，旧四角色保持，source trace False/True/False全exit0、patch重建/rollback hash一致；初次源码检查literal误带分号已修正并加入stdout断言，原错误记录保留。

**原生六帧首次执行中断（2026-10-29）**：新增probe/video/sq2-v4l2-decode-test.c，复用固定h264-vector，非阻塞stateful OUTPUT→SOURCE_CHANGE→NV12 CAPTURE，有限buffer/20秒循环、逐可见行CRC/唯一帧、输入EBD后STOP与LAST、streamoff/close；外层40秒timeout不保证解除内核卡死。ARM64静态编译/QEMU CRC已通过。在ad8e同boot现有/dev/video0执行，不重启PAS，独立120秒/2MiB collector smoke通过，随后SSH超时exit255，只拿到执行前IRQ3/PID1975/2140，没有runner完成输出。用户回复“正在重启”，不能写成自主崩溃。尚未恢复SSH（多次connection closed），**下一步首先恢复后只读取/data/local/tmp/sq2-native-frames-ad8e.kmsg及pstore/boot状态，不加载任何模块、不重复测试**；当前确切停止位置未知，不猜根因。首版stdout/stderr误放/tmp，重启可能丢失；已保存TESTED_SOURCE.c/TESTED_RUN.sh/实测binary，未来脚本改持久/data目录，runner新增每个配置ioctl前后日志，修改版仅编译未部署。实际CRC helper对独立ref.nv12六帧以384stride/256scanline填充检查12CRC并验证可见像素变异拒绝，ASan/UBSan通过，但不是硬件帧。补充四角色源trace False/True/False、同CRC自检cbf43926/exit0、补丁重建和回滚哈希一致，旧四角色保留。当前Android Codec2/原生帧和能力上限均未完成。证据out/video-decode/venus-transport/native-frames。

**首次原生IRQ/V4L2接入（2026-10-29）**：用户明确允许正常重启，setprop sys.powerctl reboot后boot由bcd0变ad8eb3bc-42af-47e6-a592-82726fa0e14c，guardY/boot_completed1。11模块当次导出missing0仍漏命名空间：实际venus-dec装载被DMA_BUF/dma_buf_put拒绝（PAS未启动），preparer新增MODULE_IMPORT_NS(DMA_BUF)，全mock/三模块编译与5生成测试通过，修订decoder实际装载成功。collector首轮180s/4MiB超上限，已在任何insmod前失败；修订120s/2MiB记录完整。原生parent首次PAS认证0并成功probe绑定，hfi_core_init等待SYS_INIT成功才可到此；实际GIC206/SPI174中断计数3。decoder子节点误写qcom,venus-decoder，原驱动匹配venus-decoder；本boot仅对确切子节点driver_override绑定，无固件重启，/dev/video0 QUERYCAP/ENUM_FMT exit0，输入MPG2/H264/VP80/VP90/HEVC、输出NV12/Q08C。overlay源码已修正并重编译SHA2c95b3ba…，未热换；下次freshboot用新overlay。system_server/SurfaceFlinger PID1975/2140前后不变，boot不变、pstore空、筛选未见SMMU fault/Oops。**尚无原生V4L2帧/CRC/EOS，更无Android Codec2**。当前固件已启动一次且保持resident，不能再PAS启动或热卸provider；下一步在本boot现有/dev/video0构建/运行有界stateful H264六帧runner（沿用已核验h264-vector及独立NV12参考），记录source-change/stride/bytesused/可见YUV CRC、EOS与close；失败保留并先取日志，不用私有HFI重复启动代替。若需重新boot须另取用户许可。证据out/video-decode/venus-transport/native-first-boot，原四角色保持并补充源对照False/True/False；它们不是三次硬件试验。

**stop/flush闭环与live预检（2026-10-29）**：实际生成stop_capture/output/streaming、hfi_session_flush及FLUSH reply抽取编译640组合，覆盖SQ2/legacy、close与streamoff、两队列/八状态/五种回复失败/原错误。确认vdec_flush_done仅日志、这些stop路径不等reconf；close抑制callback仍complete控制回复。复现stock stop_streaming清零session_error，SQ2现保留原错误并置flush/core错误，start_streaming错误入口拒绝重启；baseline/rollback timeout error0、modified1，全部程序exit0，ASan/UBSan及三模块/4回归通过。只读SSH实核bootbcd0、6.3.8-sq2-android1、guardY，旧overlay/VIDEOCC仍驻留。新core/dec单独live符号预检exit1（媒体依赖未载），加入foundation六媒体模块后八模块missing0、exit0；capture SHA8596a1ba…，full-preflight.json记录依赖，**非ABI/加载证明**。未启动固件/重启。已异步询问用户是否现在方便正常重启，未收到许可前不执行。下一动作：准备有界native V4L2 runner、收集器与八模块按依赖装载清单/哈希，核验overlay匹配及freshboot状态；获重启许可后正常重启，干净boot首次只跑原生IRQ/SYS_INIT/V4L2路径。不要在当前boot再启PAS，不用私有6帧测试替代V4L2/Codec2。证据out/video-decode/venus-transport/stop-sequence。

**内部池预算与收尾（2026-10-29）**：helpers两处DPB/internal dma_alloc及六处dma_free全部经sq2_intbuf_alloc/dma_free；SQ2与VB2共用原1GiB实验预算，非SQ2仍原DMA行为。分配前reserve、provider失败退款、直接free退款、退休保留charge到callback真实release；内部预算拒绝经stock NULL分支报-ENOMEM。实际helper/mock测试共享精确上限、超限不调用provider、失败/直接free退账及退休后release退账，ASan/UBSan通过，生成源计数2/6并拒绝残留原DMA调用。另按buffer所属不可重置scope->ended判定允许回收仍标FIRMWARE的DPB；SQ2 vdec_session_release五次abort决策改累计，旧平台不变。后两分支仅scope helper测试+生成回归/编译，尚非完整close运行验证。v1/v2三模块与原回归通过，原Module.symvers缺失/未上机。验证脚本首次找错host test位置exit1，从已完成stage复制到记录目录后重跑通过。**下一步**：对实际vdec stop/output/capture/flush路径做闭环主机时序测试（特别close抑制flush_done后是否仍等reconf），核查streamoff-only与terminal worker锁序；通过后完成当次导出符号/模块依赖闭包及新boot一次性的IRQ/SYS_INIT/V4L2真实帧测试，不再重复已完成的小向量或预算检查。Android Codec2/逐codec对齐仍开放。证据out/video-decode/venus-transport/internal-budget。

**会话代次回收接入（2026-10-29）**：sq2-venus-retire新增独立refcount scope与不可重置ended状态。HFI session init建立新scope（旧未确认拒绝替换）；在实际VB2 process、DPB process、内部SET三个地址发布点绑定，旧scope确认后活动对象可重绑定新scope；不是按分配时固化。专用END成功路径scope_end只摘取该代已退休项；晚到旧代项即时释放，其他会话失败项继续等PAS；instance destroy drop所有者引用，entry持引用直到真实回收。内部成功unset/realloc改转退休链，SET失败但已绑定者也保留，避免丢scope引用/过早free；未插链intbuf处理零list。实际wrapper/mock测试覆盖两会话、旧代晚到、活动对象跨代复用、失败会话在drop owner后仍保持至PAS，全程ASan/UBSan，原2000竞争/200callback/END等及三模块编译/4生成回归通过。source scope_api False/True/False对照及patch/rollback哈希通过；不是硬件实测。新增END mock stub时替换误及Pythonmarker，工具检查后已修正，最终测试通过。**下一步**：内部/DPB仍无预算，补入共享预算；审查END后仍标记FIRMWARE的DPB及streamoff-only/error-flush等待/错误累计，明确真实V4L2测试前条件。缺Module.symvers/未设备访问限制保持，不能宣称Android硬解。证据out/video-decode/venus-transport/session-scope。

**VB2保留预算（2026-10-29）**：审查会话代次时发现helpers/vdec错误路径也会SESSION_END，活动VB2对象可能保留并复用，不能仅按分配时代次在END后无条件回收。先给实际SQ2 VB2 allocator接入core锁保护的1GiB实验预算，live+retired driver-held backing均记账；MMAP按分配大小，进口DMA-BUF按exporter完整size（不是请求plane size），provider失败退账，退休不退账，实际release时退账。零/超限/溢出拒绝-ENOSPC，PAS停止后新分配-EIO；该预算不是硬件能力或系统总内存限制，**内部/DPB池尚未计入**。实际wrapper/mock检查full exporter、失败退款、退休保留、晚到释放、post-stop拒绝以及原2000竞争/内部转交通过ASan/UBSan；三模块及4回归通过，缺Module.symvers限制不变，无设备访问。source四角色budget_api False/True/False、exit0，非三次硬件试验；最初apply_patch上下文顺序错误，校正后完成。下一步会话scope必须在实际HFI提交时绑定/更新（覆盖会话结束后复用活动VB2对象），独立refcount并按确认END回收该代退休项；失败直到PAS停止；补内部池预算、streamoff/error-flush审查后才上机。证据out/video-decode/venus-transport/dma-budget。

**正常回收的END确认前置修复（2026-10-29）**：审查发现stock hfi_session_deinit在sys_error绕过发送仍返回0，且共享inst->done可由其它回复完成，不能拿其ret0释放退休DMA。SQ2现使用独立sq2_end_done/error与sent/confirmed字段，只有END handler完成；sys_error返回-EIO，不相关回复不能确认，失败/超时END不重发且阻止后续session init；真实成功确认后才允许下轮init重置确认状态。未据此提前释放任何buffer。实际生成deinit及END/load handlers以mock回复执行10组合：BASELINE/ROLLBACK sys_error及不相关回复均ret0；MODIFIED分别-5/-110、confirmed0；正常END、错误回复、发送失败/重复调用通过ASan/UBSan。三模块/原回归通过；缺Module.symvers、无设备访问。对照测试初次unused timeout stub触发Werror exit1，令legacy mock等待实际调用该stub后重测通过。**下一步直接接会话代际拥有的retirement fence**：不能把可重置的inst->sq2_end_confirmed直接附给旧buffer；必须用独立引用计数scope，成功END释放该代已退休项、晚到项即时释放，失败保留到PAS停止；同时加全局有界分配计数。随后streamoff-only/error-flush审查、新boot V4L2及Android Codec2。证据out/video-decode/venus-transport/session-end。

**HFI实例回调引用（2026-10-29）**：固定hfi.c/hfi_msgs.c哈希；SQ2 to_instance在core锁内增加callback计数，handler之后put；session_destroy先从列表移除，解锁后drain。vdec_close最前标记closing并等待在途回调，后续新EBD/FBD/session event拒绝，控制回复仍允许；FLUSH仍complete但closing不调用flush_done，core system-error广播跳过closing实例。最后put先保存core、只唤醒core长寿命waitqueue，避免最后引用归零后访问已释放inst等待队列。实际inline helper用mock waitqueue/POSIX线程跑200交错，验证close等待在途引用、帧/事件拒绝、控制回复保留、legacy不计数；ASan/UBSan通过，生成回归检查close-before-m2m、get/put及remove-unlock-drain顺序。三模块编译成功，缺Module.symvers不算ABI证明。首次编译缺hfi_msgs.h常量exit1，显式include后v2通过；交易验证首次Python引号错误exit1已修正。source transaction guard BASELINE=False/MODIFIED=True/ROLLBACK=False，全部exit0，仅生成源对照。未上机，**不是完整IRQ/VB2并发证明**；下一步补有界retirement/正常session安全回收，审查streamoff-only（非close）路径及error与flush等待，再新boot真实V4L2/Codec2与codec能力矩阵。证据out/video-decode/venus-transport/callback-lifetime。

**终止后resume入口修复（2026-10-29）**：审查确认venus_iface_cmdq_write与venus_set_state使用同一hdev锁，命令队列在DEINIT后已拒绝；但venus_resume原样会在DEINIT后再次venus_power_on。SQ2分支现将deinit的state/suspended/power_enabled一次加锁更新；resume在同锁内拒绝DEINIT或sys_error，然后才允许power/MMIO。初始零状态（create后、SYS_INIT前）仍允许首次resume，非SQ2保持原行为。实际生成函数抽取编译24状态组合ASan/UBSan通过；相同deinit→resume输入BASELINE/ROLLBACK ret0/power_calls1/suspended0，MODIFIED ret-5/power_calls0/suspended1，均程序exit0；补丁重建及回滚hash通过。全flags三模块编译、原测试与4生成回归通过；未上机、缺Module.symvers限制不变。**下一未完成点**：hfi_msgs.c::to_instance在core锁内查找后释放锁返回裸inst，handler在锁外使用；hfi_session_destroy移除列表后vdec_close销毁ctx/ctrl/inst的完整回调寿命仍须同步，不能直接全程锁住close（同步HFI命令等待IRQ会死锁）。需要实例callback引用/关闭阶段与等待点的实际实现/测试。之后有界retirement及正常session回收，再新boot V4L2/Codec2。证据out/video-decode/venus-transport/terminal-resume。

**DPB/残留内部buffer归属（2026-10-29）**：新增venus-sq2-intbuf.h，SQ2 free_dpb_buf转core retirement、不提前ida_free复用固件可见tag；vdec_close在hfi_session_destroy之后、kfree(inst)之前转交所有残留DPB（含FIRMWARE-owned）与内部buffer，回调只依赖buffer元数据及get_device引用、不依赖inst/IDA。helpers.h新增固定哈希。actual helper/mock测试将2个DPB+3个内部buffer转交后先free实例，再PAS确认drain，5个释放、device refs归零，ASan/UBSan通过；生成close-hook选择对照baseline/rollback core_owned0、modified5，三程序exit0，仅该helper模型，不是完整close实测。两轮三模块编译、32释放故障、VB2/retire2000项竞争、原故障测试与4生成回归通过；缺Module.symvers限制保持，未连接设备。验证脚本首次引用/tmp二进制已消失exit1，改为out持久路径重编译后通过。**下一步**：审查/修复terminal与streamoff/close/HFI回调锁序、确认停止后新提交全拒绝；补有界内存与正常session回收，当前保守buffer/tag保留不能用于长期播放。新boot真实V4L2/Android Codec2仍未完成。证据out/video-decode/venus-transport/internal-owner，原角色及观察保留。

**VB2 backing寿命接入（2026-10-29）**：固定vdec.c哈希，SQ2输入/捕获两队列使用venus-sq2-vb2.h memops，非SQ2保持原memops。MMAP backing与进口DMA-BUF的mapping/额外get_dma_buf引用/decoder模块pin独立于vb2_buffer与inst；VB2 unmap先保留同一mapping，detach/put转入core退休链。独立retire mutex避免再引入inst/core锁逆序；仅IRQ已同步且PAS stop成功后drain，PAS失败不drain，晚到的退休直接释放。actual wrapper/退休代码的mock ASan/UBSan覆盖申请/attach/map失败、重复map/unmap、VB2引用丢弃、正常回收、未映射detach、晚到退休；两个线程2000退休项与stop线程竞争各释放一次。core故障测试确认PAS失败无drain；三模块全flags编译、4项生成源回归通过。baseline/rollback选stock mock：close时refs0/mappings0/unmaps1/detach1；modified refs1/mappings1/unmaps0/detach0，PAS确认后归零。首轮生成q->mem_ops错误anchor与host puts冲突均exit1已修正。**编译/主机验证，不是可部署解码器**：未连接设备，缺原Module.symvers；当前保守地将所有退休backing留到terminal stop，尚需有界内存/正常session回收策略。下一步DPB与内部缓冲区转core-owned归属、terminal与streamoff/close/HFI回调完整锁序及禁止停止后新提交，之后才能新boot实机V4L2/Android Codec2。证据out/video-decode/venus-transport/vb2-lease，原角色不替换。

**内部缓冲区释放失败修复（2026-10-29）**：实际生成helpers.c的intbufs_unset_buffers与venus_helper_intbufs_realloc在SQ2 RELEASE_BUFFERS失败时立即返回原错误，保留失败及后续DMA/list，不继续释放或分配替代scratch；旧平台行为不变。实际函数抽取编译，32个mock用例覆盖两入口、SQ2/legacy、成功/三个失败位置、-110/-5，ASan/UBSan通过。相同首请求-110输入，BASELINE/ROLLBACK均错误释放3个并返回0，MODIFIED释放0、保留3、返回-110；三程序exit0，基线仅观察并非安全通过。补丁重建/回滚hash通过；全flags三模块编译及4项生成回归通过，缺原Module.symvers仍非ABI证明。未上机。**整体生命周期仍未修复**：stock vdec_close调用m2m_ctx_release，VB2会unmap/detach进口DMA-BUF；vb2_dc_attach_dmabuf没有额外get_dma_buf引用，不能只跳过detach而假定exporter存活。DPB free、终止worker与streamoff并发、session释放后内部链归属也未完成。下一步实现core-owned quarantine（含独立DMA-BUF引用/映射寿命）与串行化终止，再新boot V4L2/Codec2实测，禁止当前boot二次启动或加载此候选。证据沿用out/video-decode/venus-transport，新增internal-release四角色。

**SQ2资源表/decoder节点与buffer边界（2026-10-29）**：--sq2-resources需platform-entry，新增仅microsoft,sq2-venus-lab匹配；保护参数严格使用实测0/25800000/1000000/24800000，未照搬sdm845_v2的70800000。独立PM不使用stock频率/拓扑猜测。helpers.c哈希固定，DPB/internal分配及vb2 init/prepare在32bit HFI赋值前检查完整地址/大小，output payload检查offset<=used<=length；SQ2的DPB ida失败路径补释放未入链分配。10边界例与8192长度×6检查ASan/UBSan通过，三模块/原故障测试、4项生成源回归通过。--venus-decoder overlay仅含decoder子节点，精确SID/现有power/clock/ICC继承已验证配置；SPI174来自SQ1 ACPI GSI206与SM8150参考，**不是SQ2实时IRQ验证**。DTBO/模块已编译并fdtget复核compatible、interrupts=0 ae 4、唯一video-decoder，未加载；当前boot已有驻留HFI节点，overlay明确拒绝共存，需新boot。构建首轮替换anchor不唯一exit1，缩小到intbufs_set_buffer；DT首轮属性在子节点之后exit1，重排后通过，证据均保留。下一步审查并修正并发error/streamoff及PAS失败时V4L2用户/内部缓冲区寿命，再做新boot真实V4L2帧/Android Codec2，不可仅凭地址检查宣称DMA完整安全。

**Venus入口故障测试与修复（2026-10-29）**：实际venus-sq2-core.h在主机mock提供者下执行11个probe失败点、PAS停止失败保留、terminal error无重启、remove保留及opt-in拒绝。复现旧子设备创建失败留下pm_enabled=1；新增pm_runtime_set_active返回值检查与失败分支pm_runtime_disable。相同populate失败输入BASELINE/MODIFIED/ROLLBACK分别pm_enabled=1/0/1、ret=-5、children=0、retained=1，三者程序exit0；补丁重建与回滚哈希通过。三模块编译/原组件测试通过，主机logging stub初次未消费ret导致Werror exit1已修正。不是并发/真实IRQ证明；设备未加载，ABI缺原Module.symvers限制不变。下一步补齐固定SQ2资源表、decoder-only DT节点和V4L2 buffer DMA边界，审查并发终止/用户缓冲区释放后再实机；不重复私有H264向量当作Android硬解。证据out/video-decode/venus-transport/core-faults。

**Venus独立主入口（2026-10-29）**：--platform-entry需--firmware-lease，生成core.c在devm分配前分派独立probe，并分派remove/shutdown/runtime PM；sq2_platform_allow默认false、仍无SoC match。probe顺序为独立owner→power→HFI→PAS→HFI resume/IRQ/SYS_INIT→V4L2注册/子设备。错误worker只停止，不重启/重排任务/走stock coredump；PAS失败保留全部资源。发布前且停止成功可清理，发布后remove保留core/HFI/v4l2及module引用到重启，避免打开文件UAF；**非完整热卸载支持**。实验版保持供电、禁用FW自动power-collapse并拒绝父runtime suspend，不是功耗优化。ARM64三模块编译、已有实际C/mock故障测试及3项生成源隔离检查通过；新dispatcher尚无运行故障注入/设备验证。下一步测试probe各失败/并发错误/移除，补齐SQ2资源表、仅decoder子节点和V4L2 DMA输入检查，再进行新boot首次固件启动的V4L2测试，不能继续私有向量代替Android接入。原Module.symvers缺失限制不变；本轮无设备操作。证据out/video-decode/venus-transport/core-entry。

**Venus电源所有者（2026-10-29）**：sq2-venus-power.c移植实测ICC三路径→三域→四路复位脉冲→axic/axi0/axi1/ahb/core/vcodec/cvp→200MHz/版本校验顺序，保存CPU配置与原rate；确认PAS停止后恢复配置/rate、停钟、HANA桥接脉冲、复位原态、关域/撤ICC。独立platform owner要求power ready才允许secure begin，secure/quarantine禁止power-off，仍持power禁止最终release。实际power C用mock提供者覆盖40个申请/激活失败位置和回收、严格顺序、rate恢复失败保留供电引用；owner/PAS/DMA/生成源回归及三模块编译通过。初次host测试因clocks名称冲突exit1，改为clock_refs后通过。reset_status沿用已验证provider/regmap检查；未读TZ保护寄存器。**未加载设备**，缺Module.symvers仍非完整ABI证明；主core.c probe/recovery/remove与runtime PM尚待接通，明确probe门槛保持。下一步真正接通独立平台入口与错误路径，再做V4L2/Codec2；不在bootbcd0再次启动固件。证据out/video-decode/venus-transport/power-owner/build-v2。

**Venus独立资源所有者（2026-10-29）**：sq2-venus-platform.c不使用devm，独立持有core、MMIO region/mapping、初始禁用IRQ、device/module引用；PAS begin/end已从固件dispatch连接，失败进入sticky quarantine并屏蔽IRQ，拒绝释放及重新启用。正常释放还要求HFI priv/firmware owner清空。实际owner C在主机mock资源提供者下覆盖6个申请失败点与完整回收、失败保持全部引用及正常释放；ASan/UBSan通过，另有4种sticky失败/100正常寿命及原PAS/DMA/MMIO回归，三模块ARM64编译通过。前次host编译exit1为misleading-indentation，已修正并重测，不是硬件失败。**未实机加载、未完成平台入口**：power/reset/ICC尚未归属该owner，core.c probe/recovery/remove尚未改用独立路径，EOPNOTSUPP门槛保持；设备/overlay强制移除也未验证。下一步接入实测ICC→三域→复位→时钟与关闭逆序，并纳入同一quarantine，再接主入口/V4L2。build-v3与补充四角色在out/video-decode/venus-transport/platform-owner；原记录保留。

**用户更新锁屏策略（2026-10-29）**：用户表示已移除全部Google账户，并明确授权清除PIN、禁用锁屏。已对当前用户10通过locksettings清除凭据并set-disabled=true；实读is-user-secure=false、get-disabled=true。guard=Y下关屏3秒再亮屏，Keyguard showing=false、inputRestricted=false、SCREEN_STATE_ON/AWAKE，boot仍bcd0a17e…未变。此授权取代历史“保留PIN”要求；未更改30分钟屏幕超时、未重启、未写启动镜像。账户移除是用户陈述，未另行审计；跨重启状态尚未验证。凭据不入仓库。硬解platform-owner候选仍未完成：host测试因misleading-indentation编译失败待修正，不得标为通过。

**Venus PAS调用接入（2026-10-29）**：--firmware-lease现将venus_boot/venus_shutdown的SQ2分支连接到既有sq2_venus_fw owner，沿用固定固件及PIL gap、原资源保护参数；load/auth原错误直接返回，protect失败调用owner关闭，关闭失败保留owner且记录两个错误。每core的sq2_boot_attempted阻止再次启动（不是跨模块重载的整机锁）。八种boot/protect/stop故障组合、三种非法平台、已有事务/DMA/MMIO及生成源opt-in测试通过，core/dec/enc编译成功。**完整外围隔离尚未完成**：core.c的SQ2 probe显式EOPNOTSUPP且仍无match，防止通过stock错误/恢复/remove路径释放devm/IRQ/HFI。下一动作是把这些资源改成独立拥有的SQ2上下文并接入已验证power/reset，而非解除probe门槛；之后才实机V4L2/Codec2。无设备访问或固件重启，缺Module.symvers的ABI限制不变。补充证据out/video-decode/venus-transport/pas-integration。

**Venus DMA平台接入（2026-10-29）**：实际hfi_venus create路径在队列分配前校验2300/2320/2340精确SID、DMA API domain及单设备IOMMU group；保存32bit mask/coherent/bus limit，保持更严格原limit，分配失败或释放全部队列后恢复。UC检查1MiB对齐/交集范围及全部4096页翻译；不改SCM范围。ASan/UBSan覆盖各SID/域/组/部分mask失败与恢复、两端合法映射及4096个缺页位置；ARM64 core/dec/enc编译和生成源隔离测试通过。仍无平台match/设备加载，缺Module.symvers不算ABI通过。stock core.c恢复会忽略venus_shutdown结果并重启，probe/remove也继续释放devm资源：下一步必须解决PAS失败隔离、power/reset与错误恢复/解绑寿命，再启用平台，不能只替换firmware.c调用。原证据保留，补充在out/video-decode/venus-transport/dma-integration。

**Venus传输集成推进（2026-10-29）**：prepare-sq2-venus-iris1.py新增显式、默认关闭的sq2_windows_hfi资源标志；将已实测CPU-only六邮箱、跳过VERSION/DSP写、单个16MiB coherent UC及SFR子视图接入真正hfi_venus.c调用点。IOVA范围检查拒绝越过25800000..c0000000，SFR不重复释放；原平台表不变、未添加match。core/dec/enc ARM64模块编译成功，MMIO/边界ASan/UBSan、事务及实际生成源隔离测试通过；缺原Module.symvers，仅编译通过，不是ABI或设备验证。baseline/modified/rollback生成源对照均exit0、补丁重建及回滚哈希通过。未访问设备或再次启动固件。下一步：集成精确SID/DMA aperture与已验证power/reset/PAS owner，重点处理core.c错误、恢复、remove的隔离寿命，再启用平台；Android Codec2仍待接入。补充证据out/video-decode/venus-transport，不替代原hfi-decode四角色。

**首次真实H264帧验证通过（2026-10-29）**：接续既有未提交decode候选，修正runner collector240s/4MiB非法参数（原进程12064已消失、无日志/模块，尚未固件启动），收紧bufreq多属性/尾部残片/超容量检查及分配大小溢出保护。21c883a3…在干净bootbcd0a17e…首次固件启动，实际解出320x240 Constrained Baseline level1.3的6个I/P帧，12个Y/UV CRC全部等于ffmpeg独立参考，verified6/6/mismatch0、EBD7无错、EOS1；STOP/RELEASE/SESSION_END/PAS关闭及全资源回收0，rmmod0。boot、PID2201/1973、guardY保持，pstore空、无SMMU fault/WARN。正常镜像未改，提供者与overlay驻留，不热拆、不在此boot二次启动固件。用户目标已扩展“Android可用并对齐硬件能力”：configs/sq2-video-decoder-capabilities.json逐codec区分固件声明与实测；Android media.player仍只有软件视频decoder。**下一动作：把已验证平台/PAS owner/SID/IOVA/邮箱行为接入现有Venus/V4L2准备链，先编译审计，再开放Codec2与逐codec能力对齐。不要继续只做私有向量或改XML冒充Android硬解。** 见VIDEO-DECODE新首节。

**首次H264解码session往返（2026-10-29）**：正常重启到干净boot44c03275…，本boot首次固件启动下994a00f5…实测SYS_INIT 75属性、SESSION_INIT/END（cookie53513201）均error0，全部关闭/回收0、rmmod0，boot/PID1976/2304、guardY保持，无fault/pstore。离线parse-sq2-hfi-caps.py逐条解析75条=声明数、774/774字；H264解码OUTPUT仅NV12_UBWC，OUTPUT2含线性NV12，96..8192。不是帧解码。同boot第二次固件启动失败，热重启开放：每boot只启动一次固件。**下一动作：session内属性设置/缓冲区需求查询→分配缓冲区→已知H264码流→验证输出像素。** 见VIDEO-DECODE首节。

session候选实测（2026-10-29）：boot39906e2a…内单次运行994a00f5…，到CTRL_STATUS前与成功轮相同，但控制状态保持0、SFR空，-110；新caps/session代码未到达。PAS关闭及全部回收0、rmmod0，boot/PID/guard保持、无SMMU fault、pstore空。这是同boot第二次固件启动，重复上电稳定性开放；不要在本boot重试。**下一动作：用户同意后正常重启，干净boot首次固件启动只跑一次run-hfi-session.sh。** 见VIDEO-DECODE首节。

能力体/H264 session候选（2026-10-29，已在同boot二次启动实测，未到达新阶段）：消费者新增opt-in `hfi_caps=1`（整包有界复制SYS_INIT回复、原样RAW日志与上游逐字扫描汇总）和 `hfi_session=1`（仅在dec掩码含H264时SESSION_INIT/END一次，无缓冲区/码流/帧）。环复制/写入、caps解析及事务主机ASan/UBSan通过，`994a00f5…`对39906e2a…导出missing=0。未部署，无session或解码证据。**下一未完成动作：确认设备boot/驻留提供者后只跑一次out/video-decode/hfi-session/run-hfi-session.sh并取回日志；不要重复已验证的DMA/SYS_INIT门槛。** 见VIDEO-DECODE首节。

独立DMA清理告警补正（2026-10-29）：审阅完整raw日志发现baseline/modified/rollback三次of_overlay_remove虽返回0、sysfs节点消失，却均有OF引用计数/内存泄漏告警，首次另有refcount saturation WARN。4096页DMA映射/解除映射证据有效，但不能称kernel lifetime完全清理；不再重复该热拆路径。独立builder默认拒绝，源码仅保留取证；未修复其引用寿命。HFI路径使用驻留overlay、不热拆，后续沿已成功传输推进能力体/session/真实帧。本boot曾出现诊断WARN，不能冒称无内核告警或长期稳定；不要因SSH/桌面仍正常就抹去该限制。

**首次真实HFI成功（2026-10-29）**：独立16MiB DMA对照已实测legacy1300/mask60、精确2300/2320/2340及哈希回滚副本，均4096页映射/CPU读回/全部解除映射、overlay/module清理0。随后显式Windows HFI profile（同三SID、24/192表、16MiB UC/IOVAbf000000、只写CPU邮箱、跳过该路径未用VERSION写；SCM保护范围不变）在原42版固件4e93d345…上实返CTRL40000001、空队列rx_req0→1、SYS_INIT命令r/w3/3、3112字节回复/error0/properties75、msg r/w778/778。PAS关闭0、队列解除映射、HANA退出桥接脉冲0、ICC/时钟/域回收0，消费者refcnt0后已卸载。boot39906e2a…、PID2295/2104、Android1/guardY保持，正常镜像不变。提供者与两overlay仍驻留，不热拆。93cd5b7d…消费者及a8e7c9ff…overlay已实测；这是实际VPU共享队列通信，不是解码帧/Android组件。组合测试不能单独归因SID。**下一未完成动作：有界提取/解析SYS_INIT能力体，然后真实H264 session创建/销毁与帧解码；不要重做已完成的DMA/采集门槛或退回旧1300全流程。** 见configs/sq2-video-hfi-status.json和VIDEO-DECODE。

Windows初始化静态门槛通过（2026-10-29）：inspect-sq2-windows-video-reference.py固定14369cb4…驱动/acfe9033…IORT，SQ1和原SQ2输入结果一致，六项指令检查及十映射核对通过，9项正负例检查通过。直接指令确认Windows表头24/总表192、qheader56/3队列；UC大小16MiB，基址为对象两个u32字段和减16MiB（字段语义仍需追踪）；CTRL_INIT写1。不是设备compact/16MiB通过。上一轮为progress，本轮无VPU/固件/设备状态改动。下一未完成动作：建立显式2300参考SID的独立DMA候选并验证分配/映射/解除映射，先不接电源/时钟/MMIO/PAS，正常启动配置不变；不要退回重复收集或凭静态结果宣称硬解。详见SQ1-VIDEO-REFERENCE。

SQ1参考采集与SQ2日志续接（2026-10-29）：已核验用户提供的SQ1 SSH指纹并公钥登录，确认为Surface Pro X/SQ1/ARM64/Windows22631管理员。只读导出5包、95设备资源、8张校验和有效ACPI；取回71文件全部哈希通过，原始包仅private。SQ1的qcdx包与原SQ2新版Windows包共有34文件全相同，固件为79版891997ec…；当前Android实测仍用42版4e93d345…，未替换。SQ1 DSDT/IORT给出VIDEO引擎4以及指向SMMU15000000的2300/2320/2340等映射，与当前从sdmshrike继承的1300/mask60及高IOVA有差异；须离线核对本机SID/保护范围，不能盲搬或宣称根因。旧empty-76f0日志已取回，九次邮箱写完后最后为HFI_BOOT begin，无状态/队列/关闭结果，不再是注册/ICC未知。当前只读boot39906e2a…、Android1、guardY/nohibernate、PID2295/2104、临时模块清空/pstore空；恢复方式未确认，保留用户此前“桌面触摸正常”，不得称整机冻结或自主重启。本轮无新VPU试验/启动盘修改。详见SQ1-VIDEO-REFERENCE；下一步离线核对Windows同字节驱动的SID/IOVA和HFI配置，完整硬解仍未完成。

最新恢复与新日志待取（2026-10-29）：SSH已核对用户恢复后的boot76f0f9ca…、Android就绪、guardY/nohibernate、PID2264/2082、临时视频模块清空、pstore空。compact原日志已取回：只有ready/运行期噪声/smoke（602.714066s），没有新消费者日志或KMSG_END；原主机输出确认rmmod完成并打印COMPACT_BEGIN，但不能判断卡在模块注册、probe早期ICC获取还是后续尚未落盘步骤，不能据此否定24字节布局。已增加driver_register/probe_enter/ICC get前后标记。新5a967ca3…候选在干净boot上仅加载一次（默认288表头、empty_ping及HANA退出恢复），连接再次超时，**sq2-hfi-empty-76f0.kmsg待取，未取得共享队列写入/回复/退出结果**。不再启动VPU，先恢复并读取这份逐步日志确定实际阻塞阶段；未改启动盘。

操作者确认（2026-10-29）：用户已明确反馈24字节表头对照后“整机无响应，正在重启恢复”。这次是用户执行的长按恢复，不应记录为自主崩溃或自动恢复。当前重启完成、桌面/guard及新boot ID尚待只读复核，SSH仍在标识交换前关闭；不要复跑该测试，先恢复默认入口并取回sq2-hfi-compact-976c.kmsg。启动盘未改，完整硬解未完成。

最新离线协议证据（2026-10-29）：SSH仍在主机标识前closed，compact日志未取回，本轮无设备侧重试。新增inspect-sq2-video-firmware-protocol.py对固定SQ2固件校验六组ARM指令：实际用table+8的qhdr0_offset、stride56、CPU/DSP邮箱偏移、bit15启用/清除、空队列rx_req=1，以及CTRL_STATUS bit0可早于队列解析。故状态1不是队列DMA或完整启动证明，不应盲改表头/IRQ位。新hfi_empty_ping=1先将初始cmd.rx_req设0，空通知后观察固件写成1（且r/w=0），再继续SYS_INIT；406a979b…候选编译/边界/事务/导出审计通过，未部署。保留默认288表头及HANA关闭恢复候选；先设备恢复/取compact日志，再验证完整退出与真实共享队列访问。无解码帧/Android组件，未改启动盘。见VIDEO-DECODE。

最新HFI控制启动通过（2026-10-29）：已取回19ed…完整试验日志，PAS auth0/protect0后到queue-status before，随后进入邮箱快照读取前失联。新boot976cc99e…下203943ae…只写邮箱候选实测九次发布完成，CTRL_STATUS=1；SYS_INIT已发，但cmd_r=0/w=3、msg_w=0，3秒后-110。真实shutdown0、队列解除映射、三ICC清零、全部资源回收/refcnt0，boot/PID2295/2106保持。这是控制启动通过，仍无SYS_INIT回复或解码帧。随后218b6908…的24字节表头单变量对照再次失联，**sq2-hfi-compact-976c.kmsg尚待取回，不能认定进入队列阶段，禁止重跑该完整流程。** 源码核对发现HANA=0x50100000在参考驱动的固件卸载/停钟后还会脉冲桥接，旧探针仅恢复初始复位位漏了这步；已补版本限定post_reset，PAS关闭失败仍保留所有资源，编译/审计通过，未部署。先恢复/取最后日志再决定下一次测试；未改启动盘。见VIDEO-DECODE。

最新ICC/复位续接（2026-10-29）：768d0e3修订候选在boot861e77bc…再次失联，但取回native日志只到reset_deassert axi结束（377.420876s），未到时钟/PAS/HFI；不能归因为诊断寄存器。后只读确认新boot19edba8b…、Android/guard就绪，恢复方式仍未获用户确认。已按下游补上复位前ICC请求；三条路径真实解析/请求/清零全0，随后带trace_delay_ms=100的三域四reset、AHB六时钟/版本200MHz阶段均0并回收，PID2273/2079保持。完整HFI试验随后连接再次超时，**最新native sq2-icc-only-19ed.kmsg尚未取回，不要重跑fa09f750…全流程，先恢复/取日志。** 新203943ae…候选移除全部额外MMIO及邮箱快照读取，仅保留下游控制状态轮询，关闭成功后清零独占邮箱，增加逐次写入前的collector等待；仅本地编译/导出审计，未部署。没有解码帧或Android硬解，未改启动盘。固件来源已核对为SQ2原qcvss8180.mbn同字节，目录venus-5.2是原提取脚本命名。见VIDEO-DECODE、sq2-video-interconnect-reference.json。

最新HFI续接（2026-10-29）：独立SMMU消费者SID1300/mask60、DMA-FQ唯一peer、3MiB IOVAdfc00000及768页映射/CPU读回/解除映射实测通过，不是VPU DMA。首轮PAS仍全部0，HFI CTRL_STATUS始终0、-110，未发SYS_INIT；真实shutdown及全部资源回收通过。第二轮mem_protect_video_var返回0，随后扩展诊断在读完e3000后失联，下一个计划读e1020（TZ阈值）；不得复用65709696…二进制或非安全读取e1020..e1034。源码已移除阈值/reset读取，新候选仅编译/符号审计及ring边界ASan/UBSan通过，未部署。SSH后来读到新boot861e77bc…、Android就绪、guardY、PID2293/2110、临时模块清除、pstore空；本轮恢复方式/桌面确认尚待用户答复。未改启动盘。下一步有界collector下验证安全诊断和保护设置后的真实HFI启动，无解码帧或Android硬解。见VIDEO-DECODE。

最新视频PAS通过（2026-10-29）：只读SCM窗口查询实返8b700000..9ab00000，旧fd000000在外；Surface原DTS明确记录8bd80000..8d800000 image-loader空隙。新私有pil_gap仅在该空隙固定申请8c000000..8c600000，检查live SCM窗口、无System RAM/DT reserved重叠、generic reserved父项及无其它资源owner，先无写入claim测试，再实机init_image/mem_setup/auth_and_reset/shutdown全部0，raw安全返回也0。不是占CAMERA或未知保留块；记录configs/sq2-video-pil-memory-reference.json。消费者/trace/辅助设备/owner已回收，boot/PID/guard保持；**还无HFI响应/解码帧/Android组件，目标继续进行。** 下一步SMMU合法IOVA和SYS_INIT握手。原日志out忽略，软件VIDEOCC/overlay保持临时驻留，禁止热卸载提供者/全局genpd汇总。见VIDEO-DECODE。

最新视频bring-up（2026-10-29）：干净boot b7730aab…直接用软件控制VIDEOCC，已实测桥接四reset状态/顺序/恢复、AHB及六时钟，实际读取VPU版本5010002f、mask1f6、ctrl0；200MHz档驱动报200000097Hz并可恢复19.2MHz。实际PAS加载已到mem_setup：init_image返回0后mem_setup拒绝本次独占DMA区fd000000/5MiB跨度，-22；未到auth/未启动固件。真实shutdown返回0，DMA/设备及全部消费者资源已回收、消费者卸载；boot/PID保持、guardY。支持查询对PAS9和已运行PAS1/18/4全false，现仅作诊断，不虚报成功。下一步查SCM加载内存契约，不占CAMERA/未知reserved区。overlay与软件VIDEOCC临时驻留，不热卸载或读全局pm_genpd_summary。见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

最新分阶段视频实测（2026-10-29）：独立有界kmsg确认0资源、三GDSC、GCC三AXI和MVSC时钟可申请/启停回收；原VIDEOCC在VCODEC时钟真实报stuck-off/-16并正常回收。仅将两codec域暂留软件PM控制（不改频率、不跳过halt检查）的候选24ed5d48…，三域六时钟的实际probe_result=0，回收计数0。未读VPU MMIO/未执行PAS。**后续全局pm_genpd_summary读取陷入不可杀内核循环，Android/SSH仍活；sync后默认受控重启已实核恢复：boot b7730aab…、boot_completed=1、screen_only=Y，临时模块/节点/任务清除，pstore空。不要热卸载GDSC提供者或重读该汇总。** 下一次干净启动直接加载私有候选，先补下游桥接reset流程，再读寄存器。前次v3整机无响应的根因仍未证明。详见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

最新视频试验失败（2026-10-29）：独立VIDEOCC+GDSC/clock/version探针v3测试后用户确认整机无响应，手动长按重启恢复。未获版本/清理结果，具体阻塞阶段未知，不要原样重试；无PAS/HFI/固件启动或启动盘写入。恢复后SSH实核新boot9435bddc…、boot_completed=1、screen_only=Y、PID2242/2098、pstore空，临时模块/节点已清除，用户确认桌面恢复。v4仅本地编译，默认0域/0时钟/不读MMIO并加逐步标记；下次需先独立有界kmsg采集、逐条命令标记，从零上电阶段定位。SSH本次用ecdh-sha2-nistp256/aes128-ctr连通，主机校验和原身份保持。见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

固件寿命续接（2026-10-29）：独立PAS9内存事务/所有者已编入私有候选（`--firmware-lease`），真实调用前标记secure touch，load/auth失败也需成功shutdown才释放；失败隔离同时持有DMA、独立设备和模块引用，禁止重试/重分配。16种主机故障组合及100次正常复用的ASan/UBSan检查通过，编译通过；**无启用调用点、未执行PAS**。core.c错误/恢复/remove仍释放HFI及devm资源，需整体处理后才能接入，不要只替换固件分配。目标仍为实机解码与Android组件，详见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

Iris1 HFI续接（2026-10-29）：固定下游vidc_hfi_io.h哈希，新增独立iris1资源标志及CPU时钟、DSP镜像队列、IRQ掩码适配候选，保持HFI4xx和原平台表。隔离编译core/dec/enc及ASan/UBSan mock通过，三模块对同boot运行导出的符号闭包missing=0；未部署或添加SC8180X match。电源/reset/资源验证、低端IOVA保留和固件关闭失败的内存寿命仍开放；没有硬件解码帧/Android组件。见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

视频硬解续接（2026-10-29）：`codex/sq2-video-decode` 在30aee11基础上新增固定固件MDT copy-only探针。现有4e93d345…固件在新分配低4GiB/1MiB对齐的6MiB连续DMA内存中，三个LOAD段/5MiB跨度逐段复制及BSS读回实机通过；内存/辅助设备已释放、模块已卸载，无allow参数负测被拒绝。23个未定义符号匹配运行导出，boot ID及system_server/SF PID保持、boot_completed=1、screen_only=Y。未调用SCM/PAS、未启动VPU，没有解码帧或MediaCodec硬解；下一步仍为Iris1电源/HFI和关闭失败时的内存保留策略。未写启动镜像或codec配置，详见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

eDP最终回退补验：用户恢复连接后，SSH已核对原msm完整哈希f69c91e7…、boot_completed=1、screen_only=Y、默认闭源入口无测试/debug参数、一次性env=0。实时链路恢复2.7Gbps×4、2880×1920@60/355500kHz/24bpp；上一轮v3日志已取回out。下文“最后重启后未复核”缺口现已关闭，无新增切换或重启。

eDP v3实测更新（2026-10-29）：恢复模块依赖后已到boot_completed=1/SSH，候选哈希6293fa5d…匹配；驱动读数5.4Gbps×4、仍为2880×1920@60/24bpp，但用户报告黑屏/花屏，DPU持续vblank/commit/frame-done超时。不能称链路画面通过或90/120Hz可用。已保存状态、确认一次性env清零后远程sync+重启，用户确认桌面恢复；最后重启后的SSH未重连，未再次独立核对模块哈希。默认未提升，保留三菜单和已完成EFI清理。详见DISPLAY-REFRESH。

eDP 新分支 `codex/sq2-edp-link-rates`（2026-10-29）：只读 DPCD 确认无 DSC 声明，但 eDP 1.4a 速率表最高 5.4Gbps，旧字段仅2.7Gbps；已补候选协商代码。前两次候选启动因重编 msm 缺失 depends 元数据而未加载，尚未测试到链路；自动回退后桌面/原模块恢复。v3恢复原依赖、有界本地日志，测试中，不能称5.4通过。用户指出诊断Wi-Fi flyer-test已不适用，后续不再使用该配置。用户另明确要求仅保留闭源Vulkan、原Mesa、当前调试三个菜单项，已实施；10个无引用旧initramfs经本地哈希备份后删除，EFI可用1,486,316KiB。默认内核/DTB/闭源与Mesa镜像未改。详见 [DISPLAY-REFRESH.md](DISPLAY-REFRESH.md)。

启动集成完成阶段验证（2026-10-29）：runtime-v8在v7上关闭成功调用统计；新增启动助手、post-fs-data Direct配置、固定哈希打包器与离线测试。独立镜像 `5ba59bd9…` 已通过一次性真实重启，系统自动加载闭源HAL、1.1声明与Direct配置，Mesa EGL保持；SSH公钥重连正常，用户确认桌面/触摸正常。普通APK全屏120帧、计算/图像/光栅读回通过；Wild Life 5859分/35.09 FPS，40秒采样及用户目视正常，结果包SUCCESS。相同镜像已提升默认菜单，原fa64305d… Mesa镜像仍供救援及图形回退入口使用，同镜像还有 `sq2.vulkan=mesa` opt-out。未进行第二次默认入口重启或实机opt-out启动；完整标准一致性/更多格式仍开放，不能称CTS认证。01:24 system ANR另定位到Google提醒同步onStopJob超时，抓栈主线程在AppCompat配置处理，详见QUEST2-VULKAN-ONLY；未禁用用户同步。

颜色格式后续（2026-10-29）：runtime-v7在v5上增加真实RGBX/BGRA/FP16元数据适配，修正donor的BGRA新建导出format0描述。多格式AHB导入/导出、GPU与CPU读回、释放后引用及原窗口/多队列回归通过，尺寸至2880×1920；全屏FP16导入20轮触发25秒限时，仅独立3轮完整通过。临时系统接入时版本XML校正为1.1，PackageManager实测4198400；普通APK全屏120帧和Chrome实际ANGLE/Vulkan网页通过。新增格式交换链和完整CTS仍未验，v7未重跑Wild Life。已手动回退Mesa及原XML，Direct配置移除、测试锁释放，screen_only保持Y；正常启动镜像尚未修改。脚本/精确矩阵见QUEST2-VULKAN-ONLY。

当前目标：保留 Mesa EGL/GLES，仅替换系统 Vulkan 后端。闭源 EGL/GLES 研究保留在分支，不再切换桌面 GLES。

后续进展（2026-10-28）：已定位Wild Life交换链失败为适配层只接受UNORM、拒绝sRGB。版本5实验候选补充sRGB，12帧窗口中间亮度编码读回通过；无Direct配置仍花屏，加同版私有Direct配置后5876 / 35.19 FPS，采样和用户目视正常。已手动回退Mesa、移走临时配置，未持久化。用户允许Vulkan-only关闭120秒回退，新增显式 `--start-manual`；具体边界和复现见QUEST2-VULKAN-ONLY。下文失败记录为修正前历史。

最新进展：多队列重复释放已有候选修复；普通 APK 的外部句柄错误已定位为 `/proc/self/mem` 权限拒绝，改用有边界检查的 `process_vm_readv` 后，2880×1920 三角形完成120帧，截图正常。Chrome可正常打开，前八个被捕获的 Vulkan 管线创建成功，但后续部分管线仍有链接错误，不能称为Chrome整体失败，也不能据网页可见认定全部Vulkan路径通过。

**历史 Wild Life 失败（修正前）**：应用曾弹出“基准测试运行失败”；已由上方sRGB + Direct实测更新，保留失败证据见 [QUEST2-VULKAN-ONLY.md](QUEST2-VULKAN-ONLY.md)。

运行候选后续：runtime-v3 已补充真实RGBA8 AHB新建导出，64²16轮、257×129/2880×1920各3轮GPU读回及释放后双引用存活通过；导入再导出16轮、两种窗口格式及四队列150轮回归通过。早期导出测试漏声明export位，已修正重测，不引用旧结果作为标准通过。系统临时接入后普通APK120帧通过，Chrome可显示网页但GSL报缓存请求无法转换（flags 0x0c0c2600）；该错误在DRM分配函数拒绝分支，日志中的kgsl字样不证明使用KGSL。已回退Mesa并卸载Direct配置，boot/SF保持。更多格式、缓存分配、Chrome历史管线、能力XML与持久化仍开放，详见同页。

独立内存矩阵进一步复现：六类实际公布内存中type2（properties=0xb）4KB合法分配返回-3，其余五类分配通过，只有buffer兼容的type0/4另有GPU读写覆盖。GSL旧DRM缓存转换拒绝0x0c000000，缓存维护入口也不完整；需要建立实际调用关联并补足缓存语义，不能仅放行或偷换type。原始日志/反汇编仍在out，矩阵明确失败，默认系统保持Mesa。

后续缓存适配（2026-10-29）：有界GSL调用跟踪确认type2与Chrome失败均为flags=0x0c0c2600；当前MSM 1.10.0内核实际接受CACHED_COHERENT分配。runtime-v5在固定GSL拒绝分支接入真实硬件一致性缓存，另按真实HOST_CACHED属性为type4选择同一分配模式，未更改公开type/index或伪造cache维护成功。六类内存分配通过，buffer兼容type0/4各150轮4KiB及16轮16MiB GPU读写通过；type2/3仅有CPU映射/cache API覆盖。AHB导入再导出16轮、新建导出16轮、sRGB窗口12帧、普通APK120帧通过。Chrome本次网页采集未再出现原缓存/管线错误，不能覆盖历史所有路径。runtime-v5完成Wild Life 5813分/34.81 FPS，7.1/35.3秒采样正常，用户明确确认“这一轮画面正常”。随后手动回退Mesa、Direct目录恢复为空；boot/SF保持，zygote按计划重启。启动镜像未改，更多格式/能力声明/持久化仍待完成；详情见QUEST2-VULKAN-ONLY。

更新时间：2026-10-29。不包含原始日志、网络地址或凭据。

## 最近完成：共享存储

故障：Files、Chrome 下载及依赖 external_primary 的功能失败。/data ext4 已读写挂载，约 453 GiB 总容量；当前用户 10，用户 0/10 已解锁，但两个 emulated 卷均 UNMOUNTABLE。Settings 抛出 Volume external_primary not found。

根因：CONFIG_FUSE_FS=m，原启动流程未加载模块，缺少 /dev/fuse。运行中补载没有让失败卷自动恢复。修复在 probe/test2/init-googlebook-native 中于 Android init 前加载 FUSE 并检查设备节点，未格式化或重分区。

修复已写入当前内置 SSD，正常音频入口重启后两个卷均 MOUNTED。**用户确认系统“文件”打开下载/新建文件夹和 Chrome 下载均正常。** Android 接管后 /dev/fuse 为 root:root 0600，仍工作正常，不应据此盲目放宽权限。修复后 /data 约 450 GiB 可用。

现场 sm 独立 app_process 命令曾无输出并返回 0，不能据此判断挂载成功；以 dumpsys mount、实际 FUSE 挂载和应用访问为准。PID 1 的 /storage 绑定用户 0，从此命名空间直接访问用户 10 可能 Permission denied，不代表用户 10 应用故障。

## 当前待办

- 新跟踪已把实际阻断缩小：临时关闭预热后，SF以闭源Adreno启动；接入框架后因36×36 BGRA(format5)导入abort，SystemUI/登录app还混用了系统另一版本QGL编译器并SIGSEGV。独立75秒内核证据与busy→lazy回退均取得，boot保持，本轮无需手动重启；显式重启zygote后两个SystemUI实际maps已恢复Mesa。`combined-v4`用独立GLNEXT/QGL名字隔离配套编译器，两顺序/FP16/Vulkan/414读回私有回归过，实际HWUI尚未复验。下一步BGRA/RGBX语义与普通app命名空间绑定；不能以关闭预热或私有探针过关宣称系统替换完成。见 [QUEST2-SF-TRACE.md](QUEST2-SF-TRACE.md)。
- FP16独立适配已通过：`combined-v3`仅替换mapper3为`e9eaf753…`，保留真实分量与步长校验并隔离私有C++ helper ABI。FP16 384²连续150轮、13×7/全屏各3轮bit一致，RGBA8合成150轮、Vulkan/AHB、414读回和两顺序共存通过。第二次实际SF切换仍黑屏/卡住，用户手动长按恢复，已复核正常Mesa/boot_completed及关屏guard；pstore空、未留SF tombstone，根因尚未确定，不能宣称桌面通过或自主board crash。新watchdog持久化/忙挂载回退尚未设备验证。见 [QUEST2-FP16.md](QUEST2-FP16.md)，不要原样重复全局切换。
- 首轮真实SurfaceFlinger闭源切换已执行：Skia shader-cache预热要求384×384 RGBA_FP16(format22)，当前桥接拒绝后RenderEngine abort。独立watchdog恢复原Mesa哈希，SSH/框架恢复、boot ID保持；未写启动镜像。真实SF明确证明FP16现为启动必需，不能继续仅凭桌面缓存RGBA排除它。四种color格式真实PlaneLayouts已采集；下一步组件级FP16准入/步长及GPU采样验证，再重试SF。见 [QUEST2-SURFACEFLINGER.md](QUEST2-SURFACEFLINGER.md)。
- `combined-v1`14库已在同进程同时持有GLES context和Vulkan device，两加载顺序各GLES150轮+VK36帧、全屏各GL3轮+VK36帧全过；公共库唯一实例及13项真实GOT绑定通过，资源最终归零，原AHB回归正常。prepare逐字复制v4九库+fence-prune五专属库，不改任何ELF。见 [QUEST2-COMBINED.md](QUEST2-COMBINED.md)。下一步系统格式/DATASPACE、降低正常日志和真实SF可恢复切换；完整替换与性能功耗尚未验证。默认Mesa/启动镜像保持。
- GLES server-wait历史依赖累积已有实测候选：`gles-linked-fence-prune`只以原GSL wait(old,0)确认旧pending全部完成后释放该context的持有，再执行原clone/merge。未完成/错误保留依赖，无阻塞等待。64×64连续150轮、640×360/2880×1920各3轮异步合成像素全过、FD稳定11；414读回、36窗口帧、YV12三帧回归通过。GLES `1d3ba33e…`，prepare/QEMU/复现见 [QUEST2-SYSTEM-GRAPHICS-PLAN.md](QUEST2-SYSTEM-GRAPHICS-PLAN.md) 第7节。下一步合并v4公共库、同进程GLES/Vulkan验证和真实SF；以下累积失败为前版记录。默认Mesa与启动镜像保持。
- GLES多层合成探针已实现，64×64/全屏单轮像素与native fence通过；连续运行发现每轮2个已完成sync_file积累，直到EGL销毁才释放。CPU等待、以及CPU等待后仅import/destroy而不server-wait的两项3轮对照均无增长，将问题缩小至GPU server-wait内部持有。默认严格测试仍失败，尚不可接管长期SF；对照明确DIAGNOSTIC_ONLY。见 [QUEST2-SYSTEM-GRAPHICS-PLAN.md](QUEST2-SYSTEM-GRAPHICS-PLAN.md) 第7节。
- v77 Vulkan raw WSI 已完成有限实机适配：`v77-vulkan-ahb-v4` 以独立 mapper5 import 持有原 buffer，完整复制当前 loader 的 72 字节节点，按真实 donor image/memory 唯一 owner 检查后释放。64×64 连续150周期/1800帧，以及13×7、257×129、1920×1280、2880×1920各36帧全部像素/native fence通过；每周期视图/字节归零、sticky failure=0，原AHB回归通过。实机使用create时导入，deferred bind仅离线/mock覆盖；普通别名/其他格式仍有限制。见 [QUEST2-VULKAN-WSI.md](QUEST2-VULKAN-WSI.md)。正常Mesa/boot/框架保持，尚未系统替换。
- 闭源GLES读回已取得可验证修复：原库CPU转换选8通道/bank16，与实际缓冲区4通道/bank15不符；另有PBO优化截断非对齐offset和零高度范围计算下溢。当前最佳私有候选`gles-linked-zero-area`（GLES `c4796847…`）保留原阈值，用CPU4/bank15、PBO原fallback及局部零面积校验修正。基础414项+两组bank边界各54项，共522项全过；上传/采样五尺寸两phase各10项通过，YV12 client/NV12 PBO各3帧通过。默认Mesa、boot和框架保持，未写启动镜像；PBO回退可能降低性能，其他格式/SF与完整系统替换仍未完成。见 [QUEST2-READBACK-REFERENCE.md](QUEST2-READBACK-REFERENCE.md)。零阈值候选已实测无效，不提升为默认。
- v77原配Vulkan的AHB适配已打通有限RGBA路径：`v77-vulkan-ahb-v3`九库通过真实mapper5元数据生成124字节QTI视图，公开入口事务与donor最终acquire/release分开管理，base=0由donor管理mmap。属性/import各150轮通过；4×4奇数边界RGBA图案64×64连续150轮，13×7、257×129、1920×1280、2880×1920各3轮逐字节通过，引用/视图最终归零，基础compute/image/raster仍过。原HAL close=NULL曾被wrapper误拒，v3已正确缓存；不是GDPA问题。导出、raw WSI、其它格式及完整系统替换未完成，当前探针仍保留应用AHB引用。见 [QUEST2-V77-VULKAN.md](QUEST2-V77-VULKAN.md) 和 [生命周期审计](QUEST2-VULKAN-AHB-LIFETIME.md)。正常Mesa、boot和框架保持，未写启动镜像；下一步为WSI寿命/交换链与GLES多层native-fence合成，见QUEST2-SYSTEM-GRAPHICS-PLAN。
- Quest 2 同版参考已进一步补全：完整 v77 vendor 只读提取出906个普通文件（269359659字节），新增70项显示服务/SDM/allocator/配置/固件哈希清单，连同原18库复核通过。提取内容和原始反汇编留在out；只提交工具、清单和说明。见 [QUEST2-VENDOR-REFERENCE.md](QUEST2-VENDOR-REFERENCE.md)。旧ION、QTI mapper3及SDE私有接口仍需适配，A650固件仅离线参考，不替换Surface A680.1固件。
- 闭源 GLES 可见窗口已实测：私有namespace内app_process + SurfaceControl/JNI直接通过系统EGL生成640×360缓冲区，现有Mesa SurfaceFlinger进行CLIENT合成。两次各300帧；第二次同步首帧后截图的230400像素全部精确匹配，引用/映射归零，boot/框架PID保持。首图截早了，不计画面通过。**这不是SurfaceFlinger本身的闭源替换。** 正常启动和默认Mesa哈希未变。
- 内部RGBA纹理问题范围已收窄：640×360先画内部纹理、再由GPU采样到线性AHB的双读回通过；640×360 PBO读回通过，但264×136 PBO失败。不能继续把直接client-memory glReadPixels的失败直接等同于内部纹理损坏。新只读诊断核对原库Build ID/指令及实际context后，默认和两键零配置均读到字节阈值FFFFFFFF、比例阈值0；配置尚未解决失败。具体证据见 [QUEST2-READBACK-REFERENCE.md](QUEST2-READBACK-REFERENCE.md)。下一步定位CPU转换描述/路径策略，随后覆盖其他合成格式、SF自身、Vulkan配套加载与持久化。

- 闭源 GLES 多平面新进展：mapper3 按真实 mapper5 PlaneLayouts 解析 YV12/NV12，返回真实 Y/Cb/Cr 地址和步长；格式35仅在证实NV12布局后翻译为QTI具体格式0x109。两格式640×360各30帧，264×136、1280×720、1920×1080、2880×1920各3帧，均经外部YUV纹理→线性RGBA AHB→glReadPixels及CPU读回通过，4×4变色网格最大误差1，释放回零，mapper4哈希不变。**新的整套替换阻断点：原库内部RGBA纹理作为输出时，640×360纯RGB/YUV对照均有损坏；640×384及线性AHB输出通过。** 不能将成功的线性路径等同于内部纹理修复。ESX私有Direct候选配置没有改善此失败；并非已证实GMEM根因。下一步优先查内部纹理的布局/压缩/对齐，随后接可见窗口与SF。lockYCbCr适配已编译但该回调未在此矩阵中独立触发验证。详见 QUEST2-GPU-RESEARCH「YUV多平面与内部纹理阻断点」。

- Quest 2 原配 GLES 的 ANativeWindow/BufferQueue 路径已推进：参考 v77 eglSubDriverAndroid 的 flags bit3 判断，mapper3 对已验证的线性 RGBA fd-backed 缓冲区提供该兼容位；新增窗口代理按真实 dequeue/queue/cancel 与 donor 引用释放映射。Android 系统加载器下 legacy/core 两入口各100轮、1200帧精确读回通过，四个额外尺寸至2880×1920各12帧通过；legacy每轮映射/窗口引用均回到零。私有链接的共享图像/错误导入150轮回归通过。系统未公布 EGL_EXT_platform_base，EXT测试明确返回77（不支持，不计通过）。正常 Mesa/boot ID/框架 PID 保持。下一步仍是多平面视频、可见窗口和 SurfaceFlinger；ImageReader BufferQueue 不是桌面呈现证明。详见 QUEST2-GPU-RESEARCH 的「原配窗口导入与 BufferQueue 生命周期」。

- 最新闭源 GLES 已通过 Android 系统加载器：独立 mount namespace 中用系统 libEGL/libGLESv2 加载 v77 包装/桥接/原配库，无 LD_PRELOAD/LD_LIBRARY_PATH。EGL 1.5 core 150 轮、五尺寸线性 RGBA 双重读回与普通离屏路径通过，默认桌面 Mesa、boot ID/框架 PID 均不变。core/KHR 四种私有入口的共享引用与错误导入释放也各150轮通过。下一步是 ANativeWindow 交换、视频多平面、合成器和持久化，完整系统替换仍未完成。详见 QUEST2-GPU-RESEARCH「EGL 1.5 与 Android 系统加载器接入」；以下阶段记录保留历史证据，旧的 core/系统加载未覆盖已由本条更新。

- EGL 包装模块已新增：KHR 原生图像导入的导出符号和 getProcAddress 两入口统一代理；未启用探针手工代理的两组各150次循环实测通过，最终映射零残留。仍是私有 preload，EGL1.5 core、窗口交换与系统加载尚未覆盖，不是已完成系统替换。详见 QUEST2-GPU-RESEARCH。

- GLES 引用生命周期新进展：私有 native-buffer 代理已按 donor inc/dec 回收实际映射；早销毁 EGLImage 后纹理继续绘制，以及两个 EGLImage 共享同一 AHB 的 150 轮测试均通过。引用 2→1→0 和最终零残留各150次，框架未重启。代理仍仅接在私有探针，必须接入所有 EGL 导入/窗口路径后才能用于系统，不能混合代理与非代理使用来释放共享映射。详见 QUEST2-GPU-RESEARCH 最新章节。

- GLES 生命周期继续推进：已提供显式的完整 EGL 拆除后地址映射释放钩子；同一进程连续 150 次创建/三帧双重读回/成功拆除/释放，450 帧通过，150 个映射均释放，框架 PID 未变。钩子仅由私有探针在全部资源销毁后调用，不是单图像引用计数或已部署的系统包装层，不能据此接管桌面。详见 QUEST2-GPU-RESEARCH 后续记录。

- Vulkan Direct 完整 Wild Life 首轮完成：5881 / 35.22 FPS，12.8/40.6 秒两张采样正常，先前横带和粉色噪点未见；峰值 58.0°C。真实闭源映射和应用候选哈希已核对，结束已恢复 Mesa，boot/框架 PID 未变。未复跑或逐帧验证，不能宣称完整系统替换；详见 QUEST2-VULKAN-DIRECT 最新结果。

- 闭源 GLES 共享图像已推进：修复 QTI 错读 minigbm handle 的 CPU 基址、mapper3 lock/unlock 保留导入生命周期、物理高度语义后，同一 v77 配套库在 64×64、13×7、257×129、1920×1280、2880×1920 的 shader→glReadPixels→AHB CPU 对照均三帧通过。原 mapper4 逐字未变，正常系统仍是 Mesa。映射缓存仍为进程级有界实验实现，窗口生命周期、多平面和系统合成未完成，不能部署成默认桌面。详见 QUEST2-GPU-RESEARCH 最新章节。
- 闭源 Vulkan 已按反汇编确认的 RenderMode=Direct 生成独立路径候选；实际配置读取和基础计算/图像/三角形/AHB 读回通过，尚未重新跑 Wild Life，不能称为花屏修复。详见 QUEST2-VULKAN-DIRECT.md。

- Quest 2 原配参考已补全：v77 的 18 个 EGL/GLES/Vulkan、mapper/gralloc/metadata 库均固定哈希，新增 vendor 范围重建和离线审计脚本。原配布局函数与实际 minigbm 五尺寸对照通过，64×64 所需长度与实际分配相同，暂不支持空间不足解释；未证明地址/缓存/同步正确。另一个混合 v819 GSL 对照仅通过离屏和 import-only，不能称为原厂完整栈或 AHB 绘制通过。正常镜像仍是 Mesa，详见 QUEST2-GPU-RESEARCH 后续章节。

- 完整 Quest 2 v77 EGL/GLES donor 已取得并固定哈希；闭源 GLES 的 pbuffer/FBO 三帧像素读回通过。mapper3/ION 控制 FD 私有适配后，AHB 导入仍失败并出现 fence 超时、GPU hang，用户确认黑屏后手动重启。重启后自动 SSH 恢复、boot_completed=1、Mesa 哈希和 screen-only guard 正常；pstore 为空。未写启动镜像。不要原样重复 AHB 失败路径或部署桌面，详见 [QUEST2-GPU-RESEARCH.md](QUEST2-GPU-RESEARCH.md)。

- 闭源 v819.1 Wild Life 已实测：完成 6357 / 38.07 FPS，但前中后段均严重花屏，渲染正确性失败；该成绩不能与有效 Mesa 成绩比较。临时选择器已撤回，原 Mesa 哈希和完整读回复验通过。证据与复现见 [QUEST2-GPU-RESEARCH.md](QUEST2-GPU-RESEARCH.md)。

- Quest 2 闭源 GPU 研究在 `codex/quest2-proprietary-gpu`：v819.1 经 profile 规避和进程内 QTI→mapper5 桥接，计算/图像/三角形、多尺寸线性 RGBA AHB 读回及普通 APK 全分辨率呈现已通过；v690 依赖本机不存在的 KGSL。范围与复现见 [QUEST2-GPU-RESEARCH.md](QUEST2-GPU-RESEARCH.md)。桥接不支持通用格式，不能作为已完成的系统替换；当前桌面驱动、镜像及频率未改变，闭源 APK/库留在忽略目录。
- 2026-10-28：麦克风已完成并通过重启实测，详见 [MICROPHONE.md](MICROPHONE.md)。方向传感器、前后摄像头按用户要求暂缓，明确缺口记入 [TODO.md](TODO.md)。Wild Life 花屏已通过 A680.1 + 3DMark 专用 sysmem 兼容补丁缓解并设为默认；重启回归 5264 分 / 31.52 FPS，前中后段采样正常。GMEM 根因、更多应用与长期功耗仍待查，详见 [WILD-LIFE.md](WILD-LIFE.md)。
- 真正休眠修复暂停；用户改为要求“只关闭屏幕”，具体保护策略见下节。
- 音频修复已于 2026-10-27 恢复推进：停播/重新开声爆音、音量小、缺低频及声像问题仍开放，见 configs/audio-known-issues.json。
- 蓝牙其他配置文件、LTE 有 SIM 联网及长期稳定性尚未完成验证。用户已要求推进 Turnip Vulkan，当前实测与后续范围见图形章节。

## 关于设备信息

### 资源监控器温度和电池读数（2026-10-28）

用户报告 DiagnosticsPrebuilt 的概览数据不准。实机截图为 CPU 0°C、电池容量 0 mAh、健康度 0%，循环次数 620。内核 CPU TSENS 为 43–44°C；Android 默认 thermal_info_config.json 的 Sensors 数组为空，thermalservice 返回无传感器。BAT1 提供 energy_full_design=38.2 Wh、energy_full≈28.71–28.73 Wh、7.58 V 标称电压，但没有 Android healthd 所需的 charge_full/charge_now 等接口；充电状态、电量、电压和循环次数原本有效。

已新增 SQ2 Thermal HAL 配置：以八个 CPU 核心传感器的最大值作为 CPU 温度，统一毫摄氏度→摄氏度，GPU 单独发布；不添加 HAL 降频控制，保留既有内核温控。独立 sq2_battery_compat 模块为现有 BAT1 增加只读转换属性，保留原驱动、原事件及充电策略。设计容量约 5039 mAh，当前满充约 3790 mAh，健康度 75%；mAh 为能量除以标称电压的估算值。该界面的“电池容量”显示设计容量，当前电流的中文标签为“当前电量”，单位 mA。healthd 的故障健康状态仍 UNKNOWN；没有电池温度、制造日期等硬件信息，不能把缺失值说成真实 0°C，也没有伪造 GOOD 状态。

在线拔下充电器后，用户确认切换到电池供电；内核、Health HAL 和 UI 同步显示 Discharging、AC=0、约 −0.97～−1.06 A，截图为 −1068 mA。重新接上后恢复 Full、AC=1、0 mA，满电时零电流正常。CPU UI 显示 43.6–44.4°C 并随负载更新。制造商容量估计会略随固件更新变化，28.71→28.73 Wh 属于本次实读变化。

通过一次性启动项完成重启：资源助手自动 READY，模块哈希 d5a75996…，Android HAL CPU 读数约 52.9°C、GPU 49°C，电池容量和 75% 健康度自动恢复，用户确认重启后显示正常。CPU 仍为 schedutil、上限 1.7664/3.1488 GHz，CPU/GPU cooling state=0、仅关屏 guard=Y。测试包已设为默认，initramfs d519b958…；Image/DTB 保持 CPU 适配版本，临时菜单移除，旧镜像 e16a7a01… 留在本地 out/cpu-frequency。设为默认后未再次重启。sq2.monitor=0 可跳过此适配，救援入口不执行它。

持久化只改变 /init、新增助手、温度 JSON 与兼容模块；逐条审计确认音频、触摸、图形和凭据内容保持原样。构建缺原始 Module.symvers，8 个未定义符号都已对照运行内核导出并实机加载，不声称完整 modpost 校验。原始截图/日志保存在忽略的 out/resource-monitor；构建流程见 BUILD。

SettingsGoogle 的“型号”读取 Build.MODEL；CPU Info 由 DiagnosticsPrebuilt 的 HardwareInfoProvider 提供，其 DefaultCpuDataSource 读取 Build.SOC_MODEL。原镜像显示 XPS Googlebook，CPU 为 unknown。apply-device-info 在原 Android init 加载属性之前，为现有 build.prop 创建 RAM 文件绑定覆盖：型号 Surface Pro X、品牌/制造商 Microsoft、ro.soc.model=Microsoft SQ2。保留用户设置的设备名称、mica 产品代号、android-desktop 硬件分派、ABI、构建指纹及软件版本，不修改只读 EROFS 镜像。

内核版本页通过 Os.uname() 和 DeviceInfoUtils 正则解析；原构建日期“2026-10-27 00:00:00 UTC”缺少星期，解析失败。build-sq2-screen-only.sh 改为“Tue Oct 27 00:00:00 UTC 2026”，内核 release 保持 6.3.8-sq2-android1、配置及模块 ABI 不变。新 Image 已通过 QEMU 只关屏保护测试。实机正常重启后，设置页面截图确认 CPU 为 Microsoft SQ2（8 个线程）、型号 Surface Pro X，Android 版本子页显示 6.3.8-sq2-android1 和 #17 Tue Oct 27 00:00:00 UTC 2026。用户原有设备名称保留。

## 启动结构

- Surface Pro X SQ2、16 GB，已替换为实验用 512 GB NVMe；内核 6.3.8-sq2-android1，内置 NVMe 已启用。
- ESP 分区 6，userdata 分区 1；布局见 scripts/build-googlebook-internal-ssd.py。super 切片映射为只读 EROFS，适配经 RAM overlay 注入。
- ESP /EFI/BOOT/grub.cfg；内核/DTB/initramfs 在 /boot/sq2-android1/。菜单现在为正常音频、独立救援、关机，正常默认、3 秒；原无音频第 2 项已删除。
- 正常 initramfs 准备必要驱动、固件与 Android 挂载，不执行测试网络连接；用户随后要求保留自动 SSH，现由 ssh-framework-boot 等待 Android boot_completed 后后台启动 Dropbear。SSH 根目录仍是调试 initramfs。
- Android 命令：nsenter -t 1 -m -r -- /system/bin/<command>；Android 文件树 /proc/1/root；ESP /media/esp；本次日志 /logs。
- sq2.audio=1 启用音频。如需临时隔离音频，可在 GRUB 编辑正常入口去掉该参数，不再提供单独菜单项。

### 正常启动去除调试等待

正常启动的内核准备阶段不执行 network-up、start-ssh、keep-debug-route、wifi-framework-boot/rollback、memfd 自测、boot-kmsg 或 native-monitor。不再等待实验室 SSID 关联和 DHCP，也不强制打开 Wi-Fi 或覆盖已保存网络。helper 输出只写 initramfs RAM，避免同步写 ESP；必要的 QCOM QRTR/PD mapper/RMTFS 固件服务、ath10k、音频/输入/GPU/RTC/FUSE 等仍保留。触控早期初始化并行执行，boot_completed 后的触控恢复仍生效。

sq2.rescue=1 自动启用 debug，保留独立救援联网和 SSH；sq2.debug=1 可临时恢复完整 Android 调试启动。QEMU smoke 模式仍保留验证日志。用户随后要求保留自动 SSH：正常模式的 ssh-framework-boot 在 boot_completed 后启动公钥认证服务，使用 Android netd 自身路由。实机无需额外路由即可连接，故不维护诊断子网规则，也不打开 Wi-Fi、不关联 SSID、不运行 DHCP。sq2.ssh=0 禁用该助手。显式 debug 模式继续走旧调试路径，不重复启动 SSH 助手。最终正常入口重启后，已通过原公钥自动连接；启动参数没有 sq2.debug=1，且没有 network-up/monitor 日志。

此前禁用自动 SSH 的精简入口重启后，用户确认桌面、Wi-Fi、触摸和键盘正常；当时 TCP 22 不接受连接。未进行严格启动耗时基准测试，不宣称具体节省秒数。

## 图形与框架稳定性

最新默认部署为 Wild Life 兼容版 initramfs `fa64305d…`、Turnip `0b36eeba…`；由已验证的麦克风基线 `9dd3ff7a…` 仅替换 Vulkan HAL。一次性重启后自动加载，计算/图像/AHB 与完整 Wild Life 回归通过，随后提升同一镜像为默认并恢复原菜单，未再次重启。Image/DTB、麦克风 HAL `774cd7e7…`、IPTSd、CPU/GPU 温控保持。下文早期版本哈希为历史记录，当前完整哈希以 configs/deployment-status.json 为准。

scripts/patch-mesa-sq2-multiplane.py 修复 mapper5 辅助实例未导入 handle 导致元数据查询失败，以及单平面假设。RAII import/free，读取 FOURCC、modifier、plane layout，支持 1–4 平面并检查偏移/步长，返回原 handle 借用 FD，避免释放后 FD 失效。

此前 YV12 640×360 导入失败触发 SurfaceFlinger GaneshBackendTexture / SkiaRenderEngine::mapExternalTextureBuffer abort；后续 AudioService watchdog 为该轮级联现象，不应把全部重启归因于音频。新 RGBA、YV12/格式 35 GLES 读回通过。无音频视频观察 825 秒、音频开启观察 503 秒，框架 PID 未变化且用户确认有声；这是有限测试，不是长期保证。

保留 vendor.minigbm.debug=nocompression，解决早期文字/动画彩色损坏。源码 probe/graphics/；GLES 集成 build-sq2-mesa.sh、build-googlebook-graphics-overlay.py、pack-sq2-stability.sh。

### 2026-10-28 Turnip Vulkan

本机 WSL Ubuntu 24.04 使用固定 Mesa 26.2.4、NDK r28c、MSM 后端构建独立 Turnip HAL，沿用 mapper5 import/multiplane 补丁。驱动 SHA256 为 10b9e7f540311b8148cc88fe1f5c80531a680431539593ce3cd3663d336d0225；构建得到的 libdrm 与正在使用的版本完全相同，无需替换 GLES 或 libdrm。不要使用面向手机 KGSL 的预编译驱动。

硬件商品型号是 **Microsoft SQ2 / Adreno 690**，微软规格确认：https://news.microsoft.com/es-xl/surface-pro-x-fact-sheet/ 。当前设备树 compatible 为 qcom,adreno-680.1，Mesa 走 GPUId(680) 路径并显示 Turnip Adreno (TM) 680；测试日志 A680 指驱动识别路径，不是把 SQ2 硬件改称 680。用户指出 SQ2 为提频变体、最高 661 MHz；本次实机 devfreq 可用频率为 177/256/315/405/461/500/514 MHz，max_freq=514 MHz。661 MHz 尚未启用/验证，需进一步核对 SQ2 对应 OPP、电压及 GMU/时钟配置，不能只改名称或盲目拉高频率。

基线 ro.hardware.vulkan=none，系统加载器最初无物理设备。在线将 HAL 放入 RAM vendor overlay 的 lib64/hw/vulkan.none.so，按现有属性实现临时加载。系统 libvulkan 报告 Vulkan 1.3.354。直接 HAL 和系统加载器均通过 16×256 字计算校验及 64×64 optimal-tiled 图像清除/拷贝/逐像素读回；系统加载器额外通过 AHardwareBuffer 分配、mapper5 导入、GPU 清除、CPU 锁定后 4096 像素校验。

普通应用 org.surface.sq2.vulkanprobe（显示名 SQ2 Vulkan Test）通过系统加载器建立 2880×1920、4 图像交换链，两次各提交 120 帧，用户确认深蓝背景彩色三角形可见。此前黑屏源于锁屏/窗口隐藏，解锁后通过；不归因为 GPU 故障。本轮 SurfaceFlinger/system_server PID 未变，检查的内核日志无 GPU fault/hang。此为有限功能验证，不等于 Vulkan CTS 合规、游戏兼容性或性能保证；未切换桌面的 skiagl 渲染器。

scripts/pack-sq2-vulkan.py 基于当前 aa5d60f4… 镜像，仅增加 Vulkan HAL、修改 ro.hardware.vulkan=freedreno，并用 Vulkan 1.3 feature 替代 donor 的 1.4 宣称、移除未经本端验证的 dEQP 日期宣称。最后条目审计仅允许这五个路径变更；密码 SSH addon 保持最后，触控、音频 HAL/启动助手和 /init 均保持原字节。回滚基线已在忽略的 out/touch-heatmap/native-heatmap.cpio.gz 校验；新镜像 out/sq2-turnip/native-turnip.cpio.gz 哈希为 5a9c9df13e6e2d4116e0b8d4c532c14c073aebc5389f6c9266317d2ab9cd100d。最终部署结果见 configs/deployment-status.json。重启验证尚未完成，当前运行只读属性与 PackageManager 的旧 1.4 宣称要等新镜像正常启动后重新核对。

上述版本的启动验证及频率后续见下一节；后续仍需实际应用兼容性与持续负载验证。

### 2026-10-28 GPU 661 MHz 与温度支持

用户要求参考 8cx Gen 3 完成 SQ2 GPU 频率更新。对照固定 linux-surface b86f786… 与 Linux v6.12 的 SC8280XP 设备树，确认两者分别为 A680.1/SC8180X 和 A690.0/SC8280XP 路径，不能因商品名同为 690 就替换硬件身份、固件或整套电压表。SC8280XP 的 655 MHz/TURBO、690 MHz/TURBO_L1 仅作参考；本端新增 590/661 MHz 均保持原 514 MHz 已使用的 TURBO_L1=416，由本机 command DB/GMU 选择电压，未编造毫伏值。此配对是经有限实机验证的实验配置，尚不是提取出的 OEM 曲线。

`build-sq2-gpu-opp.py` 基于实际部署 DTB 7a4d1c73… 构造增量，原低频表及非目标属性保持。590 MHz 经一次性 GRUB 入口启动并通过 30 秒校验；首次发现无温度节点，未将该轮称为温度验证。检查发现原配置未启用 TSENS（受未启用 QFPROM 的 Kconfig 依赖影响）。重新取得固定内核源码，配置 olddefconfig 前后完全一致，仅构建 qcom_tsens.ko；52 个未定义符号均在目标内核导出表，vermagic 匹配后实机加载成功。模块 SHA256 dd85fbf8…，源码构建缺少原 Module.symvers，不能称为全量 modpost 校验。GPU-top 读数可用，GPU-bottom 未注册，本次不宣称所有热区完整支持。

`pack-sq2-tsens.py` 只增加匹配 TSENS 模块与 /init 的早期加载钩子，在 Android donor 模块加载之前接管。661 MHz DTB 额外添加 GPU-top 75°C passive trip、5°C hysteresis，绑定现有 devfreq cooling；TSENS 加载失败的 661 MHz 启动被阻止，原 DTB 可回退。最终正常启动使用 `sq2-gpu-661-thermal.dtb`（09b6d3a8…），救援仍用原 `sc8180x-surface-pro-x.dtb`（7a4d1c73…）。Image 保持 e7c7a3e5…，内核 release 6.3.8-sq2-android1 及其他模块 ABI 不变；这是内核侧设备树与模块更新，不是替换整个 Image 或升级内核版本。

重启还发现先前 Vulkan delta 缺少 permissions 父目录：文件虽在 cpio 审计中，内核并未成功解包。`pack-sq2-vulkan.py` 现明确写入缺失父目录并增加回归测试。修正与 TSENS 合并后的 initramfs 为 f8fb83358edeead8ac10c53e20b53b06b9e1597ab8268ab345a262a744691f50，密码 SSH addon 保持最后；仅指定图形/热管理路径变化，音频实验未混入，IPTSd 哈希仍为 e4610813…。

最终 661 MHz 组合已经从一次性实验入口重启，随后以相同启动参数提升为默认正常入口，未在提升默认后再重启一次。实机 ro.hardware.vulkan=freedreno，HAL 哈希匹配；PackageManager feature 版本为 4206592（1.3），旧 dEQP 日期宣称已消失。用户确认“已解锁联网，桌面/触摸/视频均正常”，WSL 公钥 SSH 可用，guard=Y。两个测试期间框架 PID 各自保持，检查日志未发现 GPU/GMU fault、hang 或 timeout。

带温度记录的 590 MHz 测试：30 秒，28462 轮，每轮 4096 字、每字 4096 次整数递推，逐字校验通过，GPU-top 41.3–44.4°C。661 MHz 测试：同样 30 秒，26626 轮，43.2–45.9°C；计算、图像及 AHardwareBuffer 读回均通过。轮数受并发应用/CPU 等影响，不是受控性能对比，也不宣称提频性能收益。devfreq 显示请求频率；当前 a6xx_gmu_get_freq 返回缓存值，不能把它称为实测 PLL。

降频规则通过温度模拟验证：模拟 76°C 时 cooling state 从 0 到 1，撤销模拟后回到 0；没有通过实际加热达到高温。结束时 min_freq=177 MHz、max_freq=661 MHz，空闲 cur_freq=177 MHz，模拟温度已撤销。长时间游戏、不同供电条件、实际高温及电池续航仍待验证。

回滚：把正常入口 devicetree 改回原 `sc8180x-surface-pro-x.dtb` 即回到 514 MHz；原救援入口直接使用该 DTB。旧 grub 和 DTB、两轮测试及镜像审计保存在忽略的 out/gpu-opp/，上一版本 initramfs 位于 out/sq2-turnip/native-turnip.cpio.gz。本轮未删除历史备份，ESP 写入均经过盘型号/UUID/标记确认、哈希/gzip、sync 和同卷原子替换。

### CPU 调频适配（2026-10-28，后续已完成）

用户询问 CPU 当前速度和最高频率。发现 qcom-cpufreq-hw.ko 已在 initramfs，但没有加载，/sys/devices/system/cpu/cpufreq 下无 policy。按实际 DTB 和匹配驱动定义，只读读取 0x18323000/0x18325800 的频率 LUT 与请求索引：小核域最高 1.7664 GHz，当前请求 index 12 = 1.6704 GHz；大核域最高 3.1488 GHz，当前请求 index 0 = 825.6 MHz。此为当时请求档位，不是实际周期计数，也不能证明 CPU 自动随负载升至最高频率。未加载 CPU 调频模块或改写 CPU 寄存器；接通调频驱动及负载验证作为独立后续项，不能把本轮 GPU 更新表述为 CPU 性能也已完善。

随后用户明确要求适配 CPU 频率。加载现有匹配模块后建立 policy0（CPU0–3）和 policy4（CPU4–7），但大核只开放到 2995.2 MHz；3072/3148.8 MHz 报 Voltage update failed。核对驱动发现是静态 DT OPP 缺项导致 dev_pm_opp_adjust_voltage 无法更新，并非已证明硬件电压不足。新增这两档并沿用原最高档 interconnect vote，实际电压继续读取本机硬件 LUT；不写入推测电压。

启动前由 cpu-frequency-start 加载模块，默认 schedutil，小核 300000–1766400 kHz、大核 825600–3148800 kHz。新 DTB 同时保留 GPU 661 MHz/75°C 温控，为两个 CPU cluster 新增 80°C 被动降频及 5°C 回差。镜像审计只改变 /init 和新增助手，音频、触摸、图形内容逐字未变；密码 SSH addon 保持最后。

一次性 GRUB 启动验证后，八个绑核进程各运行 20 秒确定性整数递推并全部校验通过，采样显示小核 1.7664 GHz、大核 3.1488 GHz，CPU 最高采样温度 53.3°C。负载结束后观察到较低频率，仍使用 schedutil。两集群分别模拟 81°C，cooling state 0→1，小核上限降至 1670.4 MHz、大核降至 3072 MHz；移除模拟后状态恢复 0、频率上限恢复。未诱发真实过热。用户确认重启后桌面、触摸、视频正常；WSL 公钥 SSH 恢复，框架 PID 在校验期间保持不变。

已将相同测试包设为默认：DTB sq2-cpu-gpu.dtb（a5a25e43…），initramfs e16a7a01…。Image/ABI 不变；原 514 MHz DTB 救援入口跳过 CPU 助手，普通入口也可通过 sq2.cpufreq=0 跳过。一次性测试菜单已移除，提为默认后未再次重启。旧镜像 f8fb8335… 与 GRUB 保留于本地忽略的 out/gpu-opp、out/cpu-frequency；ESP 同卷原子替换后空闲约 221 MiB。此为驱动请求频率与短时正确性验证，不是独立核心时钟测量、长期稳定性或性能/续航基准。

## 触摸及输入

IPTSd 3.1.1 加 scripts/patch-iptsd-sq2-touch.py 状态/连续性修复。configs/iptsd-sq2.conf 保留 Contacts AspectMax=2.0，触点检测与稳定性阈值默认；用户确认轻点、连续拖动、双指缩放正常且无自动点击。DFT ButtonMinMag=4000 是单独的笔配置。

2026-10-27 后续复现左上角幽灵点击：用户确认未操作的 30 秒采集出现 4 次短促点击，仅 IPTSd 虚拟触摸屏产生事件。临时带原始报告/触点状态记录的守护进程又采得 3 次；ARM 离线重放准确复现其帧号。原始 46×68 热图在 (row,col)=(0,0)、(0,67)、(1,0)、(2,0) 有近饱和尖点，周围未触摸格通常为约 180。高斯拟合偶尔将它们变成 valid/stable 触点。尚不能确定异常来自传感器、固件还是 SPI 传输，不应直接宣称硬件损坏。

两帧起触确认虽能消除第一份录制的点击，在线后仍抓到 2 次，已撤掉该实验。最终 scripts/patch-iptsd-sq2-heatmap.py 只针对 045E:0C21 触摸屏及 46×68 热图，在拟合之前修正上述四格：仅当归一化强度≥239/255 且高于原始邻居中位数超过 40/255 时，用邻居中位数插值。不裁掉屏幕区域，不提高接触检测阈值，不增加起触或抬起等待；其他采样格和硬件不变。

最终版将第一份 3610 帧静置录制的 3 次按下降为 0；同一份真实触摸录制中的左上角/中部输入仍能上报，但不能仅由按下次数证明手势质量。随后用户确认最终在线版本“手势正常，未见自动点击”。两轮在线采集中未再见原来的左上角点击，含其他位置的操作，不能称为两轮完全零输入。IPTSd 连续性及热图夹具测试通过。有限观察不能代替长时间静置、充电/电池条件和重启后的回归。原始录制、临时诊断程序、带凭据的镜像及备份仅放在忽略的 private/out。

本次用本机 WSL Ubuntu 24.04 构建，最终 IPTSd 为 e4610813…；已在 RAM 中运行，并写入当前 ESP 启动镜像 aa5d60f4…，完整哈希见 deployment-status.json。替换前镜像 4ea548ba… 已复制到本机 out/touch-heatmap/native-before-heatmap.cpio.gz 并校验；新镜像 gzip 校验及最后条目审计通过，实际只更换 bin/iptsd，保留新加入的操作者公钥，密码 SSH addon 仍放最后。sync 后同 ESP 原子替换，未重启，boot ID 不变且只关屏 guard=Y。下次自然重启后仍需核对有效二进制及实机手势，不把“写入镜像”表述为“重启验证通过”。

随后按用户要求从正常入口完成整机重启：boot ID 已变化，boot_completed=1，IPTSd 哈希仍为 e4610813…，仅关屏 guard=Y，nohibernate 保留。用户确认未操作的重启后 30 秒采集中，触摸屏及其他被采集输入设备均无事件（timeout 的 Terminated 行不是输入事件）；随后用户确认左上角轻点、连续拖动、双指缩放均正常，也未见自动点击。至此启动持久性及本次有限实机回归通过，长期静置与不同供电条件仍待观察。

本次重启后还分别验证 Windows 与 WSL 操作者密钥：显式禁用 password、keyboard-interactive、agent 回退，仅 publickey 认证，严格校验原主机身份，两者均取得 root。WSL NAT 当时无法直连设备，使用本机 Windows TCP 字节转发后，由 WSL 原生 OpenSSH 和 WSL 自己的私钥完成认证；这不是 WSL 直连网络通过。转发脚本和可复用连接配置在忽略的 private/sq2-ssh/，身份、地址、原始输出不入 Git。

输入节点发布时序/权限修复使 EventHub 接入设备。正常开机可用与休眠后恢复是独立结论，后者未解决。

只关屏内核首次启动后曾出现触摸完全无响应：SPI ready、resume_count=0、IPTSd 存活且 Android 已枚举设备，但连续操作采集无事件。保留同一二进制/配置重启 IPTSd 后用户确认恢复。新增 touch-framework-ready，在 boot_completed 后延迟 5 秒重新初始化一次；这是启动阶段的恢复措施，具体丢失原始触摸模式的原因尚未证实，不归因为真实系统休眠。

后续重启已执行 SQ2_TOUCH_FRAMEWORK_REINITIALIZED，用户确认触摸正常。键盘盖热插拔另有独立权限竞态：内核已创建 09AE/09AF/09B0 输入设备，但 EventHub 先于 ueventd 设置权限打开节点并报 Permission denied，此后没有重试。cover-input-watch 只匹配这些 SSAM 键盘盖节点，按 sysfs input 身份识别每次重新枚举，以 root:input 0660 权限重新发布 IN_CREATE；不能仅缓存 eventN，因为热插拔会复用编号。

用户再次拔插后确认键盘、触控板均能自动恢复。随后电源键关屏约 20 秒再亮屏、合盖等待后开盖，用户确认显示、触摸与键盘都正常。

## Wi-Fi 与蓝牙

Wi-Fi 使用内核接口及 Android 通用 HAL 适配：sq2-wifi-driver.c、sq2-wifi-hal.cpp。正常启动由 Android 自身恢复已保存网络；wifi-framework-boot 仅在显式 debug 启动用于调试连接及回退。保留实验室联网引导跳过逻辑。

蓝牙由 Linux hci_uart/btqca 初始化 WCN3998，sq2-hci-bridge 将真实 HCI_USER socket 桥接到 /dev/sq2-bt-h4，Cuttlefish AIDL HAL 接管。patch-sq2-bluetooth.py 包含关闭 serdev 防护、特定 baud ACK 处理及诊断；bluetooth-framework-boot 和 configs/sq2-bluetooth.rc 持久化。用户确认名称显示、配对成功；A2DP、通话等全流程未验证。不要在线反复卸载 QCA transport 模块。

后续启动曾在过早的 QCA 固件下载阶段出现异常回复，HCI_USER bind 返回 EBUSY，桥接退出且 Android 卡在 BLE_TURNING_ON。bluetooth-framework-boot 现等待 sys.boot_completed=1 后再延迟 8 秒启动控制器；桥接对 EBUSY/EAGAIN 最多等待 120 秒，其他错误仍失败。新流程只发布 HAL transport ready，不写 bluetooth_on，不调用 enable/disable；由 Android BluetoothManager 保存并恢复用户开关状态及飞行模式策略。开机加载控制器/注册 HAL 不等于替用户打开蓝牙开关。

已完成两次重启验证：保存值 1 时自动恢复 ON；保存值 0 时，控制器和 HAL 就绪后仍为 OFF，BluetoothManager 显示 never enabled。随后手动 enable 在约 0.85 秒转为 ON，恢复用户原来的开启状态；观察到的框架蓝牙崩溃数为 0。重复运行启动助手保持桥接 PID 与 OFF 保存值不变，不重新关闭已接管的 HCI 控制器。此验证覆盖开关/保存状态，不代表所有蓝牙配置文件已回归。

## 音频

### 当前有效版本：麦克风接入（2026-10-28）

WCD9340 双 DMIC → MultiMedia2 / CARD_0_DEV_1 已打通；策略发布 48 kHz S16 stereo 内置麦克风。通用 HAL 的 XML 转换会把该地址覆盖为 bottom，导致选错 card0/dev0；新增严格限定原始 donor 哈希的单指令补丁，仅跳过 IN_MICROPHONE 的地址覆盖。实际播放/录音传输、pacing、MonoPipe 保持 stock，勿叠加旧实验 patch-sq2-audio-hal.py。

当前实际 HAL 为 774cd7e7…，音频启动助手显式设置可执行标签、重启并检查 /proc/PID/exe，然后刷新 audioserver，消除了“路径已替换、运行仍是旧 inode”的竞态。用户确认 48 kHz 双声道、16 kHz 单声道录音/回放及原诊断应用正常；一次性启动后再次确认诊断录音回放正常。路由及进程哈希在重启后核对一致。不是 AEC/波束成形或全音频认证。

已将实测包 9dd3ff7a… 提为默认，恢复原正常/救援/关机菜单；原 d519b958… 基线保留在本地 out/resource-monitor。只改策略、HAL、音频启动助手和新增麦克风路由四项，内核/DTB/触摸/GLES/Vulkan/温控/电池及凭据不变，凭据 addon 最后。以下旧音频实验记录保留作历史，不能覆盖本节当前状态。完整构建、证据边界见 [MICROPHONE.md](MICROPHONE.md)。

ADSP → SLIM/WCD9340 → SoundWire/双 WSA881x。物理左右均有声；Android AIDL HAL 和 48 kHz S16 stereo policy 已接入。patch-sq2-audio-hal.py 去掉模拟器跳帧逻辑，校验原始/修改后哈希。audio-drivers-start、audio-speaker-route、audio-framework-boot 负责固件、路由、HAL。

调音问题仍开放。历史音频期间发生过整机重启；有限视频观察不能证明所有音频故障已解决。

2026-10-27 音频实验：RX7/RX8 从 raw 66 改为 72（-12 dB），PA 增益未提高，DAC/COMP/BOOST 配对端口保持。用户仍报告视频音量小、像通话。pmdown_time=0 的直接 ALSA 单次测试无爆音，但实际视频暂停立即爆音，因此已恢复原 5000 ms，未保留该策略。不要把一次无爆音视为修复。

新增 patch-sq2-audio-sequencing.py 在 SoundWire teardown 前静音 WSA881x，并在 SPKR PGA PRE_PMD 提前静音。实验模块已重启加载，但用户报告分开播放左右 WAV 时右侧开始即爆音，尚未证实补丁有效。五档音调测试后用户澄清从 160 Hz 起听不到，左右分别表现尚待核对；这不是校准频响测量。RX INT7/8 HPF 配置为 0.48 Hz。generate-sq2-audio-probes.py 提供渐入渐出、固定幅度测试 WAV；channel-continuity 在同一个 PCM 流中左/右/左/右切换，用于区分声道异常与重新开流。sq2-audio-wav-test 可经 Android 播放与 tinyplay 完全相同的文件，避免音源幅度不同混淆结论。不得盲目增加低频/PA 增益或关闭配对 SoundWire 端口。

音频后续对照结论：同一 PCM 流中左/右/左/右均无爆音，分两次开流且间隔 10 秒时第二次开声爆音。新静音模块下将三个音频 link 的 pmdown_time 设为 0 后，直接 ALSA 两遍、同 WAV AAudio 两遍均无爆音且音量接近；用户随后确认实际视频暂停等待 10 秒再恢复两轮也无爆音。audio-framework-boot 已在 HAL 启动前设置此策略，镜像已写入，尚未验证本次策略的重启持久性或长期效果。低频/音量问题未解决。新增 test-sq2-audio-reopen.sh 为临时 A/B 工具，退出会恢复原设置，不能替代启动策略。

此次 ESP 因历史备份占满导致临时镜像写入失败，原启动镜像未受影响。已将本次回滚镜像（fc5e7a7b…）复制到工作站 out/audio-fix-20261027/native-before-audio-pmdown.cpio.gz 并校验完整哈希；只移除 ESP 上该重复备份及本次失败的 .new 文件后完成原子替换。早期 ESP 备份仍保留。下一次更新应先检查可用空间。

### 2026-10-28 播放速率与均衡器 UI

用户报告连续播放破音，随后明确指出播放明显加速。撤回所有临时均衡及音量补偿后仍复现。旧跳过模拟器 pacing 的 HAL（99e2a625…）下，13.5 秒 AAudio WAV 整次调用约 7.27 秒，直接 ALSA 约 13.60 秒；发现 MonoPipe 非阻塞写入可能丢帧但仍确认完整帧数。增加 blocking writer 的实验 HAL（bf89ba97…）只有在核对 /proc/PID/exe 后才确认真正运行，两次同一探针耗时约 15.76/15.87 秒，包含启停开销，尚不能标为速率修复。

用户自行重启后确认“音频已恢复正常”。只读核查发现：实际运行的 HAL 是原版 fd219b1058eccc701de685cf59d6578e887c11182d877bb4e6c3a78bfd73e279，而 /etc 中及 APEX 挂载路径上的文件仍是 99e2a625…；启动时服务提前启动旧 inode 的竞态意味着路径哈希不能代表运行版本。该次正常播放观察中 AudioFlinger 为 48 kHz，timestamp rate 约 1，localSR 约 48001，4096 帧写入耗时约 85 ms。RX7/8 均为 72，无均衡效果链。ESP 镜像仍为 aa5d60f4…；本次未写镜像、未重启 HAL。保留此正常运行状态。

工作区 audio-framework-boot 的 SELinux 标签/强制重启/运行哈希检查及 patch-sq2-audio-hal.py 的 blocking writer 改动仍为未部署实验。**不能把它们直接打包成已验证修复**：强制重启会改变这次实际运行的原版 HAL。后续持久化应先明确选择并复核原版 HAL，而非自动部署实验版本。

用户要求推进系统均衡器并提供 UI。已从 Equalizer314 官方 GitHub release 安装 v0.0.22-beta 至当前用户，APK SHA256 6529e478d4ddc355e15a4179b0c41b0535e38334161a8161c389fb7e30a7724e（与发布资产 digest 一致），包名 com.bearinmind.equalizer314。中文参数式界面实机截图验证。它通过系统 DynamicsProcessing 实现均衡，不替换 HAL；MusicFX 已预装但本次从全局入口启动未留下可用界面。

已保存“SQ2 原声”和“SQ2 温和低频”预设，后者见 configs/sq2-equalizer314.json：125 Hz +1 dB、160 Hz +1.5 dB、200 Hz +1 dB，Q 均为 2，前置 -4 dB。三个 bell 响应会叠加，不能把单段数值当成最终频响；这是保守的相对低频补偿，整体响度可能下降，不是提高扬声器硬件增益。仅授予通知权限，频谱、混响及实验环绕保持关闭。已确认预设在 UI 显示，首次配置保持总电源关闭；等待用户通过底部中央电源按钮开启并反馈。后续仍需验证实际 effect chain、播放速度、听感和重启恢复，不把安装 UI 等同于均衡有效。

随后用户开启并反馈“低频改善且播放正常”。只读复核 AudioFlinger 全局 session 0 存在一条 DynamicsProcessing 效果链，Enabled=y、Suspended=n，控制客户端为 Equalizer314；输出 48 kHz，timestamp rate=1.00001、localSR≈48000.4。HAL 仍为原版 fd219b10…，boot ID 未变。证明此次实际视频的均衡已挂接及速率正常，重启自动恢复仍未验证。

注意：听感确认时用户已手动编辑为五段曲线（约 229 Hz/+8.41 dB、143 Hz/+9.79 dB、89 Hz/+10.50 dB、1079 Hz/+6.03 dB、5184 Hz/+3.56 dB），前置仍 -4 dB、自动增益与 limiter 关闭；不能把此次听感归因于 configs/sq2-equalizer314.json 的最初三段预设。保留用户设置未覆盖，实读 XML 和效果链快照备份于忽略的 out/audio-bass/eq314-user-confirmed.xml、eq314-active-audioflinger.txt。已告知频段叠加可能超过前置余量，若大音量破音先降低前置增益。后续不要自动重放初始化脚本覆盖用户调音。

## 合盖、电源与休眠（暂停）

### 现行策略：只关屏

用户要求禁用有问题的 s2idle，只保留显示关屏。scripts/patch-sq2-screen-only.py 在 pm_suspend 入口、冻结进程之前返回 EOPNOTSUPP。suspend.screen_only 默认开启且启动后只读，GRUB 显式指定 suspend.screen_only=1；nohibernate 同时禁用磁盘休眠。不会触发原先导致触摸失效的系统 suspend/resume 路径；显示和设备 runtime PM 保留。

lab-keep-awake 仍持有 partial wakelock 以减少自动休眠尝试，但内核保护才是最终屏障。删除强制点亮屏幕逻辑，只把旧调试用 2147483647 毫秒超时迁移为 120000 毫秒；已有其他有限设置保持不变。电源键、合盖和超时只使显示关闭，系统仍运行，关屏耗电高于真正休眠。

QEMU 已启动实际新 Image：只读开关为 Y，freeze/mem/disk 写入均失败，没有 PM suspend entry。/sys/power/state 仍可能显示 freeze/mem；它是支持状态列表，不代表策略允许进入。不要仅凭列表认为保护失效。

实机确认 guard=Y，freeze/mem/disk 同样被拒绝；共享存储仍 MOUNTED。关屏/合盖期间 SSH 持续可用，Android 显示 Asleep，但内核 boot ID 不变且 SPI resume_count 始终为 0。用户确认电源键及合盖两组恢复测试均正常。这验证的是“只关屏”，不表示真正 s2idle 已修复。

恢复旧策略必须同时回退 Image/initramfs/GRUB，或明确在后续受控研发中更改启动策略；当前不要关闭 guard。下面保留原始故障记录。

SSAM 状态来自 01:0e:01:00:01/state。patch-sq2-lid.py 将 closed 映射 SW_LID，保留 tablet switch；加载 tabletsw、gpio_keys、pm8941-pwrkey；lab-input-posture 按真实状态桥接 mica posture。用户确认合盖关屏和电源短按关/亮屏，**开盖未唤醒**。

真实 s2idle 曾全黑、SSH 消失及触摸失效。最后一次由用户长按重启，并非系统自行重启。

旧 SPI-HID 缺少 PM 回调；PM devices 阶段出现 -108 I/O/复位循环。patch-sq2-spi-pm.py 新增 IRQ/work 暂停/恢复、保留 HID 对象、暴露 resume_count；touch-resume-watch 恢复后重启 IPTSd。devices/platform PM debug 阶段能返回且设备 ready，但真实 20 秒关屏/合盖仍失败，不能标为完成。

其他线索：USB xHCI resume 曾重置；SSAM D0 转换与 display 通知待核查；stock suspend_notification_service 缺少平台支持；lab-keep-awake 框架锁未可靠阻止实际 suspend。当前不继续测试。

## 现场检查与保存

用户已要求当前实验设备允许 root 密码 SSH，保留公钥登录。原 Dropbear 编译时禁用了密码认证，现另建带静态 libcrypt 的服务端，使用原主机密钥。2222 临时端口及正式 22 端口均通过禁用客户端密钥/agent 的密码认证，id -u 返回 0；公钥连接仍工作。shadow 与明文测试输入仅保存在忽略的 private/out，不进入仓库。启动镜像追加本地密码 SSH addon，重打包注意保留最后覆盖顺序，详见 BUILD。密码变更后若需跨重启保留，须重新保存本地 shadow 并打包；诊断根文件系统内直接 passwd 只修改当前 RAM。

### 无密码锁屏

2026-10-28 调试期间用户已自行添加 PIN，随后将自动锁定延迟调整为 30 分钟，并明确继续保留该策略用于 GPU 调试。实读 user 10 的 lock_screen_lock_after_timeout=1800000。随后将 screen_off_timeout 从 120000 延长到 1800000，避免两分钟关屏打断测试；两项均为 30 分钟，不等于完全禁用锁屏。保留 PIN，不清除凭据。临时“不锁屏”资源/启动助手实验已撤回，原 Sq2Lockscreen overlay 恢复启用；以下为先前无密码锁屏适配历史。

当前用户 10 未设置 PIN/密码，关屏亮屏直接进入桌面。镜像的 android:bool/config_disableLockscreenByDefault 为 true；单独执行 lock_settings set-disabled false 未恢复锁屏。已创建 com.android.shell:Sq2Lockscreen fabricated overlay，将该资源覆盖为 false，并对用户 0/10 启用，再显式启用当前用户锁屏。

用户确认关屏再亮屏显示锁屏且能正常解锁；SystemUI 显示 KEYGUARD 和“点击或滑动即可解锁”。未创建或清除任何凭据，只关屏内核策略不变。cmd lock_settings get-disabled 在本次在线修改后仍返回 true，与实际 SystemUI 行为不一致，不能单独作为验证依据。

复现脚本 scripts/enable-sq2-lockscreen.sh 需在 Android root 环境中手动执行一次（诊断 SSH 中通过 nsenter -t 1 -m -r -- /system/bin/sh 执行已复制到 Android 的脚本）。它不随启动运行，不持续覆盖用户设置。overlay 已记录到 userdata 的 /data/system/overlays.xml，资源文件在 /data/resource-cache；后续实机重启发现该 fabricated overlay 被丢弃。现由 lockscreen-framework-boot 在 Android 启动后恢复默认资源覆盖，不调用 set-disabled，也不改动用户的密码或锁定选择。手动启用脚本仍用于首次显式选择无密码锁屏。使用 cmd power sleep 验证时需等待默认锁定延迟（本次等待 8 秒），不能用 1–3 秒立即唤醒认定无效。最终重启后无需手动执行恢复命令，关屏 8 秒再唤醒，Keyguard showing=true、inputRestricted=true；只关屏 guard 仍为 Y。回退资源覆盖可对用户 0 和实际前台用户执行 cmd overlay disable --user USER com.android.shell:Sq2Lockscreen；如需恢复原来的无锁屏选择，使用系统设置，不自动清除凭据。

使用自己的 SSH key 并确认主机指纹；连接脚本支持 -IdentityFile 或 SQ2_SSH_KEY。

```sh
cat /proc/sys/kernel/random/boot_id
nsenter -t 1 -m -r -- /system/bin/getprop sys.boot_completed
nsenter -t 1 -m -r -- /system/bin/dumpsys mount
nsenter -t 1 -m -r -- /system/bin/df -h /data
ls -l /dev/fuse
grep fuse /proc/filesystems
```

故障同时比较 boot ID、框架 PID 和日志，不只看开机动画。存储回归需实际应用操作，不只 root 写 /data。

本地 out/ 保留镜像、备份和证据，private/ 保留身份及凭据，均不入 Git。公开仓库保留可复用测试程序，不保留原始日志。部署哈希见 configs/deployment-status.json。


### 2026-10-30 SQ2 GPU700/432 纳入默认构建（离线）

用户明确要求把700MHz/corner432加入默认构建并提交主线，离线操作，不改现机。此前661/432与700/432完成Android启动，700实读boot fcda7cff…、max700、liveDT level432、guardY、45.1C；700/432未做负载或长期稳定性验证。737/432、775/432、850/432启动失败，不纳入默认。850完整日志最早GPU_SET握手超时，随后ringbuffer0/9与初始化-22，不能等同电压不足。现机默认仍原661/416；本轮没有SSH、部署或重启。

默认构建用sq2-gpu-boot-payload.py从固定a5a25e43…重现700/432精确DTB6db562c2…；仅661的corner改432并新增700/432，原CPU/温控/身份/固件保留。原514MHz DTB7a4d1c73…独立救援。build-source-boot输出两份DTB并记录audit；build-source-disk与deploy-experimental-usb分别校验菜单/载荷/manifest，拒绝陈旧或篡改文件。不含更高失败档位，也不宣称SQ1兼容。保留主线最新扬声器修复，以及USB/蓝牙/硬解/显示/凭据顺序等既有集成。原始日志、实验脚本与镜像均留忽略out，详见out/gpu-681-700/700-432/default-build-integration；不提交私有产物。

离线验证完成：6项GPU专项、合计64项测试（本地真实boot输入启用，0跳过）及4个接收参数的专项检查全部通过；完整默认boot构建成功，normal DTB6db562c2…/rescue7a4d1c73…，initramfs9bb36431…（含最新扬声器unity RX，私有SSH最后）。512MiB普通文件FAT ESP fixture中，两种显示默认均通过硬解、显示、USB蓝牙和GPU四组实际载荷校验；不是128GB整盘构建或烧录。使用已提交版GPU基础helper独立重现默认DTB成功，不依赖本地未提交高频生成器改动。离线四角色661/416→700/432→回滚661/416全exit0且恢复原hash。未连接SQ2；现机默认保持不变。错误与修正保留在忽略的验证日志，不通过改动无关测试掩盖。

## 2026-10-31：关闭4KB页面通用模式通知
用户要求禁用反复出现的4KB警告。实机通知确认来源为
com.android.settings/.development.PageAgnosticNotificationService，
标题“正在使用 4KB 页面通用模式”，不是应用16KB兼容警告。
通过pm disable --user 0及--user 10持久禁用该单独组件；force-stop
系统用户0的Settings清除已发布通知，dumpsys notification确认对应
NotificationRecord已消失。未重启，boot仍a384f5c1…；未改内核页大小、
应用兼容属性或启动镜像。重启后尚未实测；组件状态由PackageManager持久保存。
恢复可对两个用户执行pm default-state --user USER
com.android.settings/.development.PageAgnosticNotificationService。

**2026-10-31 SQ1 EL2准备独立工作树**：分支 codex/sq1-el2-preparation-2026103；固定slbounce c090a8cd…，仅编译ARM64 sltest（12e5a3e9…），PE检查通过。SC8180X无现成overlay，尚缺本机TCB签名/版本与实测EL2证据。未部署/重启；SQ1的661/65Hz默认与SQ2状态不变。详见SQ1-EL2-PREPARATION.md。

**2026-10-31 SQ2 EL2手动测试已准备，等现场结果**：用户指定.194并确认在场。合并Windows样本68b2b4a；微软TCB签名/哈希/8指令及sltest12e5a3e9…验证通过。独立EFI/SQ2EL2Test与手动首项已安装，默认Mesa不变、超时10秒，菜单5f69fe5d…，备份before-el2-manual-20261031为2cce07b4…。一次正常重启已发起，等待用户选测试、反馈绿条/错误并手动恢复；不自动测试/改SecureBoot/做休眠。详见SQ1-EL2-PREPARATION.md最新节。

**2026-10-31 SQ2 EL2接管测试出现绿条**：用户明确报告顶部绿条并停住。固定sltest12e5a3e9…代码在HVC1/EL2寄存器访问后画绿条，支持SQ2+SQ1签名TCB单CPU接管成功；无数值CurrentEL/多核/KVM/Linux启动证明。已告知长按关机再回原Mesa，一次SSH检查超时，恢复boot尚待确认；没有再启动实验或做休眠测试。

**2026-10-31 SQ2 EL2绿条后已恢复高通**：boot049964bb…，原baseline60/closed Vulkan READY、boot_completed1、guardY核对，菜单5f69fe5d…未变；用户手选高通，持久Mesa默认不变。新增单vCPU最小KVM MMIO42探针，静态ARM64 -Werror编译通过；现内核按预期/dev/kvm缺失退出1，未启动guest/未再重启。下步仍需独立SC8180X EL2内核/DT准备。

**2026-10-31 /dev/kvm实机推进仍待第二次画面结果**：已构建RAM-only最小KVM内核Image1a6c86d9…和initramfs63100b9e…；QEMU EL2内核创建/dev/kvm并执行guest MMIO42 PASS/EXIT0。实机首轮EFI chainloader unknown error，未进Linux；用户恢复后bootfaf4eb80…，v2启动器4efc6b7b…自行安装DTB并显示阶段错误，第二次正常重启已请求。一次后续SSH读回bootbd429ef0…、原Mesa正常且guardY；无法据此判断第二次测试是否执行/成功，等待用户屏幕反馈。默认菜单不变，实机/dev/kvm尚未声称通过。构建与代码2b52403，详见SQ1-EL2-PREPARATION.md，原始证据out/el2-kvm。

**2026-10-31 SQ2实机/dev/kvm与guest验证通过（用户屏幕确认）**：用户对v2明确反馈‘出现/dev/kvm且PASS、EXIT=0’。这是隔离EL2 tiny内核、单vCPU MMIO42实际执行通过；非QEMU替代，非临时系统SSH日志。收到报告后SSH恢复检查超时，本次恢复仍待确认，不能把先前bd429ef0…归到本次。正常默认/救援保留，无休眠/追加重测。Android正常内核、多核、外设与AVF终端仍未验证。

**2026-10-31 KVM实机PASS后正常系统恢复已核验**：用户确认恢复，SSH读回bootffa7053f…、原baseline60 Mesa2.7-60、boot_completed1、guardY/nohibernate；菜单f7244134…与Mesa持久默认不变。完成/dev/kvm单vCPU验证及恢复闭环，没有追加实验。

**2026-10-31 完整EL2内核8核/KVM通过，但NVMe未通过**：照片确认HOST_CPUS8、全CPU EL2、KVM PASS/EXIT0，同时/dev/nvme不存在；PCIe1c18000延迟probe。初版诊断遗漏tail，已修正新initramfs e743de2a…并增加SMMU/supplier信息。用户要求后续一次性GRUB自动选项：已装消费成功才选测试的白名单机制，默认Mesa不变，菜单6ef2e1f7…，fullram请求一次已发起重启，等待新照片与请求消费核验。完整Android KVM内核/331匹配模块及175744446B启动包仅本地准备，含实验性RAM Secretkeeper声明隐藏及protected_vm=0；未部署Android入口、未启动终端。详见SQ1-EL2-PREPARATION.md。
