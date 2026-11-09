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

## 2026-11-09 当前执行：v18 解除真实 flush 锁阻塞，继续整栈收尾

主线仍mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18。Bili切换视频/seek异常已真实复现，
三次FLUSH_ALL在SEQ_CHANGED回调争用inst.lock时超时；直接V4L2复现了相同1秒阻塞。
v18持队列锁、等待时释放状态锁，协调DRC输出flush真实回包并保留错误/END隔离，已正常启动；
相同客户端五组0，0.235–0.550ms，两组sequence回调在等待期间完成。完整normal/BT709回归四独立审计通过，150+60精确帧、120+30物理输出回调。
用户随后听音确认修复，记录为本轮听感恢复；HAL未修改，原HAL一次skip确认4096帧和一次PCM XRUN
仍是独立限制，不宣称其根因消除，不恢复旧pacing/MonoPipe/增益实验。
本组联合回归已完成，继续原厂OMX错误收尾、合法新sequence的HDR缓存、候选完整联合再提升Main10。
内屏SDR/HDR→SDR与严格DPU像素/fence、KGSL/下游msm_vidc/QTI C2、PM/性能/SQ1-SQ2范围保持。
唯一回退保持，full_*false，原始日志/二进制仅ignored out。详情见HANDOFF/BUILD最新节。

## 2026-11-09 当前执行：v17 已修 END 确认，继续错误收尾/HDR 与整栈目标

正常v16与P010候选都出现过Venus quarantine；真实损坏码流证明一条独立根因：
旧session_error否决实际成功END。v17只修该条件，保留客户端错误及真实超时/负回包/core_error隔离，
已正常启动，原OMX/桥/mapper/HWC/Image/固件不变，唯一回退保持。
新实机成功END确认与DMA归零已观测；负例OMX收尾仍超出10秒观察窗，runner1/timeout124保留，
之后同PID自然回到零实例/缓冲，无服务重启/provider重载。不能称完整错误恢复或原DRC/PM超时已修。

同boot正常16客户端/BT709联合验收已通过，下一组推进原厂OMX错误收尾及同流HDR缓存：
真实新sequence需重置旧mastering/CLL，同时保留同CVS合法SEI持久性、同buffer身份和所有权；
不要按每帧无SEI清空、合成空FW记录或只改mapper。Main10继续过滤，待完整联合/metadata通过后再提升。
内屏无HDR，验收SDR/HDR→SDR；物理120输出/回调和灰阶90的证据范围见前节，
严格DPU像素/PQ高光精确曲线/面板色准未过。
继续KGSL/下游msm_vidc/真实QTI C2、fence实际故障、PM/性能与SQ1/SQ2，全栈目标和full_*false保持。
源码手册未提交推送，私有产物/原始资料仅ignored out。

## 2026-11-09 当前执行：先修联合 DRC 和同流 HDR 状态，再提升 Main10

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

下一组顺序：
1. 用干净正常v16与最新成对候选比较相同1080p DRC；joint-v2九帧超时/quarantine已由正常重启恢复，保留反例。
2. 按真实新sequence语义修正OMX静态HDR缓存，保留同buffer身份/所有权和合法SEI持久性。
3. 重跑同流30帧metadata/像素/PTS、独立HDR/AVC及完整联合组，然后才广告Main10/持久部署。
Main10能力声明/持久部署须等DRC和同流HDR状态修复、完整联合验收后再推进。
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete仍false；
继续严格DPU像素/fence实际故障、KGSL/下游msm_vidc/QTI C2、PM/性能与SQ1/SQ2目标。
源码手册尚未提交推送，私有库/固件/凭据/原图/原始日志仅ignored out。

## 2026-11-09 当前执行状态：原生 P010 Surface 候选通过，转物理 SDR tone-map

主线保持mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18。
已修字节/像素stride与NV12→P010实际S_FMT/padding重协商，v2最小分配回调保持旧ABI。
真实RAM候选的HDR10/HLG各6帧原生P010及AVC30帧，共42帧像素/PTS对新FFmpeg参考精确；
实际AHB静态HDR/dataspace正确，三个独立decoder节点EOS/stop/delete通过。
65项相关主机检查及真实布局ASan/UBSan通过。失败记录及具体证据见HANDOFF/BUILD。

内屏只有SDR。下一执行顺序：
1. 把已验证的原生P010带入物理Surface/SF/HWC，验证真实HDR→SDR映射及同流HDR/SDR/
   bit-depth切换；不能以本次ImageReader或跨decoder metadata为空代替。
2. 补正常联合回归并持久部署；当前正常仍v16，Main10 profile仍过滤，未广告候选能力。
3. SPS-only同尺寸变化已在HFI入口事件头确认无新增SEQ_CHANGED；比较实际下游HFI配置，
   不再盲试编号/CSD、不在桥伪造VUI。继续严格DPU像素/真实fence故障及原整栈缺项。
4. KGSL、下游msm_vidc、真实QTI C2、PM/性能和SQ1/SQ2必须继续，不以兼容候选代替原目标。

本组无部署/重启/EFI变化，正常服务/桥已恢复、七服务健康，Venus错误/insts/vb2_bytes为0，
无试验绑定/进程/kprobe/trace实例。最后Awake、总wake locks=0，用户设置/凭据未改。
full_*仍false；源码手册尚未提交推送，原始私有记录仅out。

## 2026-11-09 当前执行边界：内屏 SDR，DRC 能刷新真实 VUI

主线继续mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18。用户确认内屏不支持HDR：
内屏按SDR及正确HDR→SDR映射验收，不将内屏HDR直显列为完成条件；外接HDR按实际能力。

成组实机对照已排除同尺寸SPS编号复用/分离、OMX/桥及独立CODEC_CONFIG的简单解释：
这些条件下raw VUI均停在首段601。真实分辨率重配则9帧颜色6→1→6、SDK4→1→4、
像素/PTS精确；同一服务生命周期trace复验69帧，序列回调为1/1/3（含初始化）。
函数追踪不等于raw HFI捕获，不能据此声称固件普遍不支持。反例及审计见HANDOFF。

接下来以真实HFI消息/属性和匹配下游配置定位同尺寸更新缺口，不再盲试编号或CSD，
不在桥从SPS合成VUI冒充修复。继续Main10/HDR→SDR、严格DPU像素、真实fence故障、
KGSL/下游msm_vidc/真实QTI C2、PM/性能及跨SQ1/SQ2验收；full_*仍false。

正常v16-bounds、唯一回退和用户设置保持，本组未部署/重启/修改EFI。
诊断服务/桥/trace实例已撤，七正常服务健康、Venus错误/insts/vb2_bytes为0。
源码/手册尚未提交推送；私有产物与原始记录仅out。

## 2026-11-09 当前执行状态：v16 fence 输入与短命令修复已部署验收

用户已确认内屏不支持HDR：内屏按SDR验收，HDR内容验证解码/元数据和HDR→SDR映射；HDR直出仅按外接sink实际能力启用与验收。

主线保持mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18，成组推进并实机验收。
默认v16-bounds c967ade0…已正常重启，唯一回退不变；最终composer-v22配display-v20。
共享readFence现在验证sync_file类型，并在layer/client-target/output-buffer三入口
保留错误、不在失败后改cache/HAL；有效负状态保持合法。另修client-target零长度越界。
实际源码66个FD/短命令场景修复0不匹配，相关34项主机检查通过。

受控kernel sw_sync输入实测了非法FD拒绝/DRM不变、已报错、pending正常signal、
pending报错；30帧release归属/复用和15次协议错误恢复通过。
这是标准测试源，不是实际GPU/VPU硬件故障；模块未打包、未热卸，重启后实际不存在。
正式GPU30帧/27复用和29次pending信号在DPU wait内发生通过；正常16客户端/
150精确解码帧、4物理窗口120输出/回调、多窗缩放/UI遮挡/退窗和自动BT709通过。
两路60个非空输出完成配对完整，交叠778ms/17切换；一条零字节返回日志配对未全，
严格完整trace失败保留，不宣称吞吐或同时硬件执行。具体边界见HANDOFF最新节。

下一执行顺序：
1. 继续追踪SPS-only 601→709→601 native-extradata/HFI配置不更新，比较真实HFI消息与
   下游msm_vidc，不在桥从SPS生成虚假VUI。
2. 扩展真实GPU/VPU生产者故障、多点合并、永久pending/超时及错误帧策略；
   当前负状态输入按完成继续提交，不等于上游错误传播或失败图像丢弃策略。
3. 推进严格DPU像素与更多物理视频格式、HDR/secure/videoUBWC、KGSL、
   下游msm_vidc、真实QTI C2、PM/性能和跨SQ1/SQ2验收。

正常重启至少120秒再SSH；恢复用户等待lock_settings实际可用，保留凭据与用户wake locks。
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete仍false。

## 2026-11-09 当前执行状态：v15 自动 BT709 闭环已部署并验收

继续 mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18，按用户要求成组推进、实机验收。
默认SQ2Nat现为v15（029a1a8a…），已正常重启；唯一SQ2QDbg回退5b9f7012…保持。
启用原生VUI extradata并在init关闭继承的SF treat_170m_as_sRGB覆盖：
真实709元数据→原厂OMX→ACodec→mapper→HWC0x10c10000 DEVICE→KMS709 limited已通过。
未采用无效的现代dataspace指导候选，未在桥强制颜色或从SPS生成假的固件记录。

正式v15有ByteBuffer/Surface各30帧像素/PTS精确、物理709窗口30输出/回调；
normal-v8另有16客户端、150精确解码帧及4物理窗口120输出/回调，
多视频缩放/UI遮挡/退窗回收三个独立审计通过。52项相关主机检查通过。
v7-vui的退窗CRC失败和v4指导反例保留；不放宽既有审计。
这不等于严格DPU像素/色准、性能、PM或所有流内颜色变化通过。
混合客户端功能/像素通过，但本轮桥trace不完整，不新增实际输出交叠或吞吐结论。

下一执行顺序：
1. 追踪同分辨率SPS-only 601→709→601未更新的native-extradata/HFI配置边界；
   独立FFprobe证明输入变色，v3真实元数据保持首段。比较实际HFI消息/下游msm_vidc，
   不把该证据泛化成所有固件不支持，也不在桥伪造VUI掩盖。
2. 补真实fence错误、严格DPU像素、更多物理视频格式及HDR/secure/videoUBWC。
3. 继续KGSL、下游msm_vidc、真实QTI C2、PM/性能与跨SQ1/SQ2验收。

保持动态发现用户和资源；已授权无密码用户跳过锁屏，当前disabled=true/showing=false，
凭据及用户wake locks保持。框架重启要等lock_settings真实可用再恢复原用户，
不能只凭system_server PID认为服务已就绪。正常重启至少等120秒再SSH。
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete仍false。细节见HANDOFF最新节。

## 2026-11-09 当前执行状态：v14 已正常启动，继续修复 VUI 传播

执行主线保持 mapper5 + 原厂gralloc + 源码QTI HWC/SDM + Linux6.18，成组推进并做真实验收。
默认SQ2Nat现为v14-color（6e75d8c8…），唯一SQ2QDbg回退保持；不新增备份或启动项。
真实VPU DMA-BUF→HWC DEVICE→内核release→refill/free已在BT601与显式BT709各60帧
通过，均含320×240→1920×1080→320×240物理DRC。新增内核标准SDR属性与实际601/709
CSC状态、成对display/composer-v20已正常重启；normal-v6的16客户端/150精确解码帧、
4物理窗口120输出/回调、多视频缩放/UI遮挡/撤窗均通过。细节与限制以HANDOFF最新节为准。

下一执行顺序：
1. 追踪真实VPU VUI/extradata→原厂OMX color-aspects→HIDL/ACodec→gralloc/dataspace；
   当前真实BT709码流仍报告BT601，ByteBuffer缺少color元数据。先抓原始区分性证据再修，
   不用强制709/改参考/放宽独立审计替代传播。保留sdr-color-v1/v2失败记录。
2. 在正常v14上补真实色彩切换、更多物理格式/严格DPU像素、fence错误注入和产品边界。
3. 继续既定KGSL、下游msm_vidc、真实QTI C2与HDR/secure/videoUBWC/PM/性能及SQ1/SQ2目标。

显式测试向量的BT709直显不能记为自动VUI完成；DQBUF同步完成不能冒充导出的VPU fence。
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete仍false。
保持动态发现用户/资源；已授权的无密码用户跳过锁屏已实机核对。正常重启至少等120秒再SSH，
本轮首次成功观测145.3秒。保留凭据与用户应用wake locks，不热卸载provider。

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

# SQ1/SQ2 通用开发规范

## 当前高通显示栈任务的执行方案（2026-11-08 用户确认）

当前主线为 **mapper5 统一框架接口 + 源码 QTI HWC/SDM + Linux 6.18 显示适配**。
保留已验证的缓冲后端，参考小米 11 的源码显示实现；mapper4 是具备同等回归和明确工程收益
后才考虑的备选。执行顺序为显示后端衔接 → 完整 QTI HWC/原生视频 → 按需求补齐缓冲能力
→ 整栈验收，必要依赖同步处理。KGSL、下游 msm_vidc、统一内存、HDR 和 PM 范围保持。
**每轮合并有依赖的源码改动、集成和验证，尽早跑完整链路，出现具体问题后再 debug；
单个接口或探针通过不是本轮收尾条件。** 修复后继续阶段目标，保留必要回归与真实验收。
具体执行与验收以 [高通显示与视频栈整体移植方案](QUALCOMM-FULL-STACK-PORT.md) 的
“当前执行方案（2026-11-08 用户确认）”为准；状态与证据见 [HANDOFF](HANDOFF.md)。
用户已恢复移植目标为 active，后续按此方案与开发节奏继续。


## 当前实施状态与设备实验规则（2026-11-09，正常显示包v7）

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

## 历史实施状态（2026-11-09，正常显示包v5）

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

## 历史实施状态（2026-11-09，system-v16）

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

## 历史实施状态（2026-11-09，system-v14）

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

用户已允许跳过实验恢复时的无密码锁屏。每次按运行时发现当前用户，核实无安全凭据及
lockscreen disabled，再 wake/dismiss-keyguard；本轮用户编号仅属观测，不能固化为用户 10。
等待 input/window 就绪及 system_server 稳定，单个恢复步骤失败不得中断剩余清理；不清密码、不改变持久 PM 策略。
临时 PARTIAL wake lock 走真实 system suspend AIDL，先取得锁再停 zygote，结束即释放；
它只为有界实验保活，不能充当休眠修复证明。
正常重启后等待 **120 秒** 再尝试 SSH，期间继续本地工作；窗口内不因过早连接失败请求人工唤醒。

## 适用范围和优先级

本规范落实 2026-11-03 用户要求，与根目录 AGENTS.md 的 `SPX-PORTABLE-SQ1-SQ2`
规则共同约束源码、构建、安装与正常启动。新规则优先于历史按调试样机组织的实验流程。
交付目标是**同一份镜像在任意 SQ1/SQ2 设备运行**，不是为两台已登记样机各生成一份镜像。
下面是开发与交付要求，不是“全部机型/设备已验收”的声明。

## PORT-01：禁止单机身份绑定

不允许以调试机的序列号、NVMe 型号/序列、MAC、IP/SSH 地址、主机密钥指纹、
原盘 UUID/ESP 标记、Android 用户编号、某次私有采集 SHA、工作区绝对路径筛选可构建/
可启动的设备。换一台 SQ1/SQ2 不应要求改源码、加入白名单或按该台重新封装。
测试记录可以带 HOST/SERIAL/UUID 等现场参数；连接测试设备不等于绑定产品。
历史 `sq2` 文件名或目录名可因兼容保留，但不得据此拒绝其他受支持 SQ1/SQ2。

## PORT-02：区分机型能力与单机身份

通过可验证的 SQ1/SQ2 型号/SoC 和硬件能力选择正确的 DT、驱动与默认档位，允许
有硬件依据的型号/修订差异。兼容性检查不应变成只放行某台已测机器。
动态发现当前 provider 和设备资源，不固定一次启动的 phandle、eventN、ALSA cardN、
用户编号、分区枚举顺序或构建机路径。发现多个候选时明确处理歧义，不猜测目标。
固定的真实硬件寄存器、协议 SID/资源约束及有依据的 VID/PID quirk 不等于单机绑定；
应验证其硬件适用范围，不能为所谓通用而盲删资源边界或冒充其他 SoC。

## PORT-03：通用镜像不携带调试机身份

内核、固件和模块是匹配的共用组件；单机密码/shadow、SSH 私钥和主机身份、Wi-Fi
凭据、IMEI/NV/FSG 校准、账号/userdata、触摸噪声/坐标测量结果不是通用构建输入。
目标设备唯一数据须来自目标自身或在本机首次配置/校准；不得把某台采集的 FSG/NV
复制给任意其他机器，也不允许为换设备先替换私有文件重打包。
软件网络开关/运营商设置由目标 Android 管理；未取得必需的本机数据时应保留正常启动、
明确报告并安全禁用对应可选能力，而非伪造数据或以未知错误阻止整机启动。
实验室凭据/授权输入可以用于明确标注的私有实验包，须与通用交付分开，继续禁止入 Git。
固件再分发和功能支持的限制须另行如实说明，不能以规范代替来源/兼容性检查。

## PORT-04：同一标准产物，默认行为一致

标准整镜像入口仍是 scripts/build.py，同一产物含 SQ1/SQ2 所需的匹配组件并在运行时
选择；保持高通默认与 Mesa 回退两个入口。机型默认参数差异可配置，不把样机的
频率、内存/磁盘容量、安装路径或登录用户当作所有设备必须满足的条件。
构建只输出新文件，不连接设备取私有数据、不写盘、不重启；安装/现场调试是独立步骤。
首启扩容、SSH 身份生成/显式配置、触摸校准等应在目标设备完成，保留其已有数据。
现存实验镜像即便能在两台样机运行，只要仍含单机身份/必需私有移植数据，就不能标为
“任意 SQ1/SQ2 的通用交付”。

## PORT-05：安全检查与完整性必须保留

正常启动保留模块 ABI、固件/硬件兼容、SMMU/内存/时钟资源边界、产物 SHA 完整性、
screen_only/nohibernate、固件一次启动和不热卸活跃 provider 等保护。
产物自己的 SHA 用于验证本次构建，不得误用为调试样机历史输入的唯一准入白名单。
写盘前核对本次实际目标盘、ESP、挂载与根源；可使用现场读取的 UUID/标记作防误写
校验，但不得把某台原盘的固定 UUID/序列写成通用启动条件。不可取消这些防误写
步骤来复用旧 USB 脚本，更不能触碰工作站系统盘或格式化 userdata 修复挂载。

## PORT-06：跨设备回归与交付验收

每个影响启动/驱动/打包的改动须补齐以下矩阵，并记录“通过/失败/未测”及实际输入：

| 维度 | 必须覆盖的变化 |
|---|---|
| 机型与菜单 | SQ1、SQ2；高通默认、Mesa 回退；同一镜像 |
| 单机身份 | 不同序列、MAC、地址/SSH 身份、用户编号和原盘标记；不重编或改白名单 |
| 运行时资源 | 不同合法 phandle、event/card 编号、资源发现顺序；异常/歧义须安全退出 |
| 存储/内存 | 不同受支持容量、全新初始化与原数据升级，保留数据和已有用户设置 |
| 私有数据 | 无调试机凭据、使用目标自己的校准/调制解调器数据；缺失时安全降级 |
| 回归与恢复 | 启动、显示/触摸、音频、网络、硬解、保护开关及副本回滚；不得由单点外推 |

离线 fixture 需使用不同合法身份/资源值，不能把一台机器的常量改成另一台便算通用。
实机尽可能覆盖每种型号的多台样本；不能穷举每台设备，因此“任意设备可运行”是设计和
交付要求，测试结论须注明样本、配置及尚未验证的硬件修订。原有失败证据不得抹去。
当前遗留整改项见 TODO 顶部；在整改和对应验收前不得重新标注旧实验包的支持范围。


## PORT-07：启动后遇相关问题才按需清理 Chrome 图形缓存（2026-11-07）

本节与 AGENTS.md 的同日最新规则一致，取代旧“所有应用图形缓存必须清理并备份”及“每次切换先清 Chrome”的要求；先推进启动测试，不设预启动缓存清理/回执门槛。

1. **范围仅 Chrome**：按运行时发现各 Android 用户/工作资料中的已安装 Chrome 和浏览器资料目录；不扫描、分类或清理其他应用，不将其他 Chromium 浏览器或 WebView 自动纳入。
2. **仅图形派生缓存**：只处理已确认可重建的 GPU/Shader/Vulkan pipeline/OpenGL program/ANGLE/Dawn/Graphite 缓存。示例为 GPUPersistentCache、GPUCache、DawnGraphiteCache、DawnWebGPUCache 及 Chrome 自身系统图形缓存；路径、归属和内容用途须确认，未知项不删。
3. **直接删除，不备份缓存**：不复制、归档或改名保存缓存内容。不生成缓存恢复包；删除不可回滚，Chrome 会按新后端重建。日志可记录路径、数量与结果，但不得包含缓存内容副本。
4. **保留浏览器数据**：不得清空应用数据、整个 cache/code_cache 或网页普通缓存，不动账号、Cookie、历史、密码、凭据、下载、书签和设置；不运行 pm clear。
5. **启动后按需，不是切换必清**：Mesa↔高通双向切换及普通启动均不自动或强制清理；仅在启动后遇相关问题需要处理时，停止 Chrome 后清理，完成后重启 Chrome。图形驱动切换本身必要的消费者停启独立处理，不因 Chrome 清理无条件停止所有应用/桌面。
6. **保留系统恢复能力**：6.18 Mesa 默认、原驱动和回退入口保持；代码/配置恢复与缓存备份不是一回事。既有历史缓存备份不因本规范自动删除。
7. **执行与证据一致**：停用旧全应用扫描/备份控制器；全应用或 Chrome-only 清理回执都不是启动准入条件，不自动执行预启动清理。按需清理仅记录 Chrome 图形缓存、无缓存备份及实际删除/跳过/失败，不冒用回执，不把修改规范当作真机清理或图形功能验收。
