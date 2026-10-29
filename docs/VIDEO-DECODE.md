# SQ2 视频硬解适配调查

2026-10-29，用户明确请求补齐硬解。状态：未实现；已临时加载媒体框架、VIDEOCC及其OF overlay，未更改启动镜像或 codec 配置。

最新目标：在Android上实现硬件解码器可用，完成标准为真实硬件解码帧验证、Android MediaCodec可枚举和调用、应用实际播放；只有内核模块加载不算完成。

## 桥接复位、实际版本与PAS内存设置阻断（2026-10-29）

在干净boot b7730aab…中直接加载软件PM VIDEOCC24ed5d48…，没有加载/卸载旧GDSC提供者。新增consumer overlay reset描述，对应SC8180X GCC的41/42/43及VIDEOCC0，并显式提供IRIS_AHB时钟；保持原六时钟顺序，AHB在三AXI之后、三core之前启用，逆序关闭。`reset_bridge` / `enable_ahb`默认关闭，读寄存器必须显式同时开启三域、六时钟、桥接reset和AHB。

首轮复位状态读取因6.3 qcom_reset_ops没有status回调返回-524，未执行电源/复位写入。增加只针对这四个已核对reset的regmap读取：核对DT reset ID、provider compatible和物理reg范围，从现有绑定provider取得regmap、读实际bit2，保留provider引用至读取完成，不推测初态。四个初态均为deasserted。随后三域上电、四reset按下游顺序assert、usleep_range(150,250)、同顺序deassert、原状态恢复和下电通过；该等待是调度范围，日志实际间隔可更长。再做桥接reset+AHB+六时钟全流程，probe_result=0、所有回收阶段返回0。

首次真实VPU MMIO读取通过：版本 `0x5010002f`（major5/minor0x10/step0x2f）、mask `0x1f6`、ctrl `0`。寄存器区为0xaa00000+1MiB，不覆盖VIDEOCC；没有触发整机无响应。三个core的初始驱动报告频率19.2MHz，GCC分支的rate=0是无父频率模型，不应当成真实停钟。随后只选择已有200MHz频率表档位，报告200000097Hz；初版精确相等检查误判-34，已收紧为标称值±1kHz校验并继续报告实值。复验读版本及恢复19.2MHz通过，不是PLL物理测频或性能测试，也未改OPP/电压。

新增消费者 `--firmware` 独立编译路径，链接既有固定固件/非devm内存事务层；`boot_firmware`默认false，须同时选择固定版本、桥接reset、AHB、三域六时钟和200MHz。它没有HFI/IRQ/用户缓冲区：若关闭失败，固件事务的全局所有者/模块pin及原overlay共同保留DMA、原始clock/reset/PM引用、虚拟设备和MMIO占用，直接返回绑定状态而不进入清理，限定重启恢复；成功停止才恢复CPU clock配置、频率、reset并释放资源。主机事务故障注入仍通过；这是私有bring-up的资源持有策略，不是已解决整个标准Venus/V4L2的remove生命周期。

支持查询先阻断启动：PAS9及对照PAS1/18/4均返回false，然而三个PAS remoteproc状态均running。该布尔包装同时折叠查询错误/调用不可用，不能判定PAS9不可用。对照原Venus的实际加载路径，将支持查询保留为诊断而非硬门槛；所有实际load/auth/shutdown错误仍原样返回，未伪造成功。保留首次-95预检查结果供审计。

随后实际加载试验（消费者eef6389e…，完整哈希传输匹配、当次导出missing=0）分配独占6MiB DMA物理区0xfd000000，并通过固件大小、SHA256和ELF校验。qcom_mdt_load返回-22，日志明确为 `error -22 setting up firmware`；按固定5dd1d028… MDT源码控制流，说明PAS init_image返回0，随后PAS mem_setup拒绝了本次物理区/5MiB跨度。**没有到auth_and_reset，没有固件运行或解码帧。** 随后的真实PAS shutdown返回0，DMA擦除/释放和辅助设备注销通过，owner=NULL；频率/CPU clock配置/reset/时钟/电源均回收，probe_result=-22。此地址已释放，不能复用或认为安全固件接受它。

试验后消费者refcnt=0并正常卸载，辅助内存设备不存在。boot ID仍b7730aab…、boot_completed=1、screen_only=Y、PID2263/2102保持；无重启/ESP/正常启动镜像/codec配置修改。软件VIDEOCC与自pin overlay保持临时驻留、域/时钟没有消费者持有；不要热卸载提供者或读全局pm_genpd_summary。有界collector均正常结束，完整日志在out/video-decode/iris1-phase（含query-real-kmsg.txt）。下一步核实SCM mem_setup契约/可接受的独占加载区，不借用CAMERA/未知保留内存，不把init_image成功当作认证或固件启动通过。

## 分阶段资源实测与提前硬件交接阻断（2026-10-29）

在9435bddc…同一boot上，先验证有限独立kmsg collector（83dcdf5d…）的进程、KMSG_READY和实际smoke记录，再运行v4的0域/0时钟/不读MMIO阶段。三个电源域及六时钟引用获取/回收通过，boot_completed=1、screen_only=Y、PID2242/2098保持。collector使用O_DSYNC写私有/data临时文件，120秒/2MiB上限，主动停止后KMSG_END完整；日志已取回忽略目录。

新增消费者单独编译模式 `SQ2_IDENTIFY_EXISTING` / `build-sq2-iris1-phase.sh`，保留overlay/VIDEOCC，使用driver_override把已经完成零资源测试的节点转交给sq2-iris1-phase。消费者的probe内所有资源在返回前回收，可按阶段卸载消费者，不拆时钟提供者。新只读probe_result参数报告实际probe结果；insmod为0仅证明driver注册，不证明probe成功。

原VIDEOCC（fd6adde7…）分阶段实测：1、2、3个域的上电和下电均返回0；三GCC AXI时钟和MVSC主核时钟使能及释放通过。加入第五个VCODEC时钟时，内核报告 `video_cc_mvs0_core_clk status stuck at 'off'`，clk_prepare_enable返回-16，回收返回-16；没有读取VPU MMIO，设备保持响应。有限collector完整记录了时钟warning和调用栈。此错误可正常返回，不能据此认定它就是上一轮v3整机无响应的根因。

核对固定下游venus_hfi.c：其__venus_power_on在时钟开启后才__enable_hw_power_collapse；当前6.3 gdsc_enable对HW_CTRL域在返回前即打开硬件触发，可能在固件尚未提供硬件票时提前掉电。新增 `build-sq2-videocc-sw-domains.py`，固定原videocc源码119d847a…，只把vcodec0/1_gdsc的HW_CTRL改为0。保留真实时钟halt检查、不伪造成功、不改时钟频率、寄存器地址或reset。它暂留软件PM控制，**没有实现固件运行后的HW_CTRL交接或验证长期功耗**。

候选VIDEOCC模块24ed5d48…和带probe_result的消费者263415c4…均传输全哈希匹配，符号闭包missing=0。旧消费者卸载后切换时钟提供者（overlay未动），三域+六时钟、不读MMIO的实际probe_result=0；每次使能、反向关闭/下电和最终回收均有独立日志，全部返回0。这验证了当前启动前条件下的软件控制差异，非固件/解码或全驱动成功。消费者随后卸载；clk_summary确认七个video/GCC相关分支enable/prepare计数均0（包含未开启的IRIS_AHB），idle报告19.2MHz不能当作活动解码频率。

后续读取全局pm_genpd_summary时，cat进程陷入R态内核循环，SIGKILL未退出；Android/SSH仍响应。不能称临时GDSC提供者热卸载生命周期已安全，且不能再次读取该汇总或仅凭模块引用0热替换提供者。已保存日志、sync后发起默认入口受控重启清理；随后SSH复核新boot b7730aab…、boot_completed=1、screen_only=Y、PID2263/2102，旧诊断任务和所有临时视频模块/节点已清除，pstore空。默认闭源Vulkan启动参数保持，未改启动盘；本次重启是操作者执行的受控清理，不是自主崩溃或自动回退。桌面目视未另行确认。下一次在干净启动后直接加载软件控制候选，避免先加载/热卸载旧GDSC提供者。下游VPU5还在使能时钟前执行AHB→AXI桥接reset assert/deassert（150–250µs间隔），尚未适配；必须先核对桥接reset/总线时钟，再读VPU寄存器。没有执行MMIO、PAS认证、固件启动或真实解码。

## 首次电源/版本识别试验失败（2026-10-29）

为独立核实资源接入，新增 `sq2_iris1_identify.c` / `build-sq2-iris1-identify.py`，从本次运行FDT（be17f33f…，与文件形式DTB哈希不同）生成VIDEOCC加私有identify消费者的临时overlay。三路GCC AXI时钟使用SC8180X真实ID227/225/226，VIDEOCC使用4/2/3，三个GDSC使用0/1/2；寄存器申请限定0xaa00000+1MiB，不覆盖ab00000 VIDEOCC。数值以固定sdmshrike下游及当前内核头/驱动交叉核对，尚未取得SQ2实际版本读数。没有固件、SCM、HFI、IRQ或reset写入。

离线合并确认本地GDSC引用准确，移除新增两节点后原树全部属性与保留区逐字语义不变。新模块预检missing=0。旧临时VIDEOCC/overlay引用计数均0，按先驱动后overlay顺序移除；该卸载仍出现OF node refcount泄漏警告，不能称清理完全，后续避免重复拆装旧overlay。第一候选6c3988f0…被Linux 6.3以“symbols in overlay, but not in live tree”拒绝，未进入探针；已临时恢复旧模块。修正为去除导出__symbols__、保留__local_fixups__后的v3（7004d8af…）再次测试。

**v3测试后SSH失去响应，用户确认整机无响应并手动长按重启恢复。** 主机测试连接最终reset，未捕获任何SQ2_IRIS1_IDENTIFY结果，未取得硬件版本或回收通过证据。原串行远程命令缺少逐步输出，不能确认卡在旧模块拆卸、新overlay、GDSC、时钟、MMIO还是回收；不能把计划执行的步骤当作已执行。未调用视频PAS/启动固件，未写ESP、镜像或codec配置。不能原样重复v3。

用户确认桌面恢复后，已用保留主机校验/用户密钥的SSH复核：新boot ID9435bddc…，boot_completed=1，screen_only=Y，system_server/SF PID2242/2098，pstore为空，临时iris1/VIDEOCC/overlay模块及VIDEOCC节点均不在；正常闭源Vulkan启动参数保持。这是用户恢复操作，不是自动恢复通过。默认curve25519/chacha SSH握手曾reset，显式选服务端支持的ecdh-sha2-nistp256/aes128-ctr后连通；未改SSH服务/凭据/主机校验，不据此判断握手故障根因。

诊断v4（cd5ef936…）仅本地编译：增加每个资源申请、上电/下电、时钟开关、MMIO前后标记；默认power_domains=0、enable_clocks=0、read_registers=false。必须显式选择阶段，禁止非法组合；read需要三域六时钟全开。新overlay固定驻留至重启，不能强制rmmod。该版本未传输/执行，不是已修复候选。下一次实机需先运行独立主机端有界kmsg采集并给每条远程命令加开始/结束标记，从不进行上电/MMIO的阶段定位，再决定下一步。原始报告/日志留out/video-decode/iris1-identify；core/HFI清理问题保持开放，独立探针没有解决整个Venus驱动的寿命管理。

## PAS固件内存事务层（2026-10-29，未接入调用）

新增 `sq2-fw-transaction.h` 和 `sq2-venus-fw.c/.h`，随 `--firmware-lease` 编入隔离venus-core。该实现尚无启用的调用点，**不是可加载/启动SQ2 VPU的完整适配**。它提供固定固件校验、独立辅助DMA设备、低4GiB/1MiB对齐的6MiB分配、PAS9 load/auth/shutdown及内存所有权策略；全局单例加mutex序列化，不借用core的devm生命周期。正常持有或隔离时都保留模块引用。启动前检查Surface compatible、SCM可用/PAS9支持、固定固件SHA256/ELF与固定MDT源码版本。

事务在调用可能触及secure world的load之前标记TOUCHED。load或auth失败均尝试shutdown；仅shutdown返回0才擦除/释放DMA、注销辅助设备并释放模块引用。shutdown失败保留内存/设备/模块、记录原错误并进入QUARANTINED，后续start/stop不再调用PAS或复用地址；全局所有者不含core指针，能够在上层遗失句柄时继续持有隔离分配。隔离恢复限定重启，不把失败当成已停止。成功shutdown才允许下一次重新分配，原启动错误与shutdown错误分别保留。

主机故障注入直接使用生产事务头，覆盖分配/load/auth/shutdown的全部16种成功失败组合：任何secure touch后都由独立oracle要求先确认shutdown才能release；检查隔离后拒绝重试、无双重释放，及100次正常启动/关闭复用。ASan/UBSan通过；Iris1寄存器mock同时通过。隔离模块编译通过，缺原Module.symvers及完整ABI证明的限制保持。日志/候选清单在 `out/video-decode/iris1-lease-v2/`，未传到实机或执行PAS。

**尚未把该层接进venus_boot/shutdown。** 原core.c的probe错误清理、恢复、remove会继续释放HFI队列和devm资源；仅保留固件DMA不足以证明整个VPU关闭失败时安全，不能简单接入或用remove返回错误来阻止内核的devres释放。下一步必须一并处理这些资源及设备移除语义，再补SQ2资源表/电源域/IOVA边界，最后启动固件。实机仅只读复核，boot ID仍2f0c17ee…、PID2220/2046、boot_completed=1、screen_only=Y。

## Iris1 HFI差异候选（2026-10-29，未部署）

固定OnePlus下游提交补取 `drivers/media/platform/msm/vidc/vidc_hfi_io.h`，SHA256 ebc34f34…，已加入参考清单。与原venus_hfi.c核对：VPU5和sdmshrike均使用4xx消息，CPU CS/IC及wrapper基址沿用旧布局；独立差异包括时钟使能后CPU_CGC_DIS/CLOCK_CONFIG清零、没有CVP时DSP QTBL/UC镜像CPU队列、保留无关位的中断掩码读改写（仅清0x14）。不是Iris2/v6寄存器布局。

新增 `prepare-sq2-venus-iris1.py` 和 `build-sq2-venus-iris1.sh`：固定core.c/core.h/firmware.c/hfi_venus.c/io头哈希，在新目录复制源码并添加独立 `iris1` 资源标志；HFI仍要求4xx。封装于 `probe/video/venus-iris1.h`，在初始firmware boot前和后续HFI恢复时配置CPU时钟，在原CPU队列映射后镜像DSP队列，在boot_core中单独处理IRQ mask。原平台match/resource表逐字不变，所有既有平台的iris1默认为false，**没有可匹配SC8180X的新设备描述，尚不能驱动本机解码器**。其余Iris1电源/reset/互联、DMA IOVA边界和固件寿命仍未完成，不能直接装载并添加新节点尝试解码。

隔离编译得到venus-core/dec/enc三模块。首次core SHA256 add55d36…（随后输入清单增加core.c保护，不改变驱动源码）；缺原Module.symvers，modpost警告已保留。对同boot的实机导出做三模块符号闭包检查missing=0，不能当作完整ABI或硬件通过。未传输或加载这些候选，默认系统未变。产物/编译日志/预检在 `out/video-decode/iris1-hfi/`。

ASan/UBSan主机mock直接编译候选实际helper和原寄存器头，检查五次写入的完整地址/值/顺序，以及1024组掩码保留无关位；通过。具体地址由固定下游头独立核对。此项只验证软件寄存器序列，未在SQ2上执行，也不证明整个HFI协议可用。sdmshrike DTS与sm8150同样声明0xaa00000/0x200000、SPI174和非安全SID0x1300/mask0x60；该社区下游文件尚不是SQ2实测资源证明。

## 固件搬运与读回实测（2026-10-29）

继续自远端 `codex/sq2-video-decode` 的 `30aee11`。当前工作站缺少上一轮thermal/video缓存，已从SHA256为275f05c5…的既有内核归档恢复独立 `/root/.cache/sq2-thermal/kernel`，重建foundation；没有构建或替换正常启动Image。

新增 `sq2_video_firmware_stage.c`、共享ELF校验头和 `build-sq2-video-firmware-stage.sh`。只接受Surface compatible、显式allow=1和现有1159200字节/4e93d345…固件；任何MDT调用前核对完整SHA256及ELF32 ARM、小端、紧接ELF头的程序头表、重定位标志、源范围、非重叠加载区和6MiB上限。此限制对应当前6.3 MDT解析器的实际假设，非通用ELF加载器。构建固定MDT源码哈希5dd1d028…，拒绝直接SCM/auth依赖，调用 `qcom_mdt_load_no_init`，不调用PAS初始化。

模块SHA256 `03c0cd37aa61a29c0a008be8de1c149655846e851933810e42f18940c833a57b`；23个未定义符号全部匹配当次运行内核导出，vermagic为6.3.8-sq2-android1 SMP mod_unload aarch64。符号闭包不代替完整ABI验证，原Module.symvers缺失限制保持。`audit-sq2-video-modules.py --module ... --output ...` 可单独审计探针而不覆盖foundation报告。

实机传输哈希匹配后，insmod allow=1返回0。新分配6MiB标准DMA内存，物理地址0xfd200000（本次值，已释放，不能复用）；三个LOAD段、5MiB跨度的数据逐段比对和BSS全零检查通过，MDT返回的重定位基址也匹配。辅助设备无OF/IOMMU/DMA转换，DMA地址等于物理地址，1MiB对齐且完整区间低于4GiB。返回前擦除并释放分配、注销辅助设备；随后rmmod返回0，两项sysfs均不存在。不带allow参数的独立负向加载返回EPERM，未创建设备或保留模块。

前后boot ID均2f0c17ee…，system_server/SF PID均2220/2046，boot_completed=1、screen_only=Y，VIDEOCC保持绑定。未写ESP、启动镜像、设备树或Android codec配置，没有重启。完整预检和日志留在忽略的 `out/video-decode/firmware-stage/`。

主机C测试覆盖合法重定位/BSS/hash段排除、截断头表、错误架构/字节序、非连续程序头、超长段表、源溢出、filesz>memsz、段重叠、32位地址溢出、超大跨度、无可加载段和非重定位段，全部通过。边界校验测试与实机copy/readback是不同证据。

**没有调用PAS初始化、认证或启动，也没有HFI或解码帧。** 本次只补齐真实固件搬运/读回证据；不能直接给现有Venus加devm分配后启动。下一步仍需完成Iris1电源/HFI/资源适配及固件关闭失败时的分配保留、恢复和remove策略。已释放的连续内存地址不是可永久占用的reserved-memory。

## 通用媒体框架首次实机加载

### 固件连续内存实测

`probe/video/sq2_video_memory_probe.c` / `scripts/build-sq2-video-memory-probe.sh` 新增独立分配探针。匹配6.3.8构建，13个未定义符号均匹配当次运行内核__ksymtab；模块SHA256 `d581fe6a5d089d434cb90e6d68cab69bfe5bd118c609445881ee8d04d9b9b2fd`，实机传输哈希一致。显式allow=1及Surface compatible保护，创建无OF/IOMMU/地址转换的临时platform设备，以32位coherent DMA mask调用标准dma_alloc_coherent；不调用SCM、不访问视频寄存器。

实机insmod成功：6MiB分配返回物理/DMA地址均为0xfd000000，1MiB对齐且完整区间低于4GiB；整区0xa5及0两种填值读回一致。函数返回前已dma_free_coherent并注销辅助设备，随后rmmod成功，核对module/sysfs辅助设备均不存在。boot ID仍2f0c17ee…、boot_completed=1、screen_only=Y。原始结果在out/video-decode/memory-probe-result.txt。

这证明本次运行可通过标准DMA接口获取独占连续内存，不证明PAS9允许在此处运行固件；不能硬编码或再次使用已经释放的0xfd000000。后续实际加载必须持有新分配直到远端处理器关闭确认成功，并处理认证失败、恢复和驱动移除。现有Venus core.c在错误清理、remove及恢复路径忽略venus_shutdown返回值，不能直接改为devm分配后自动释放，否则固件关闭失败时存在释放仍被硬件使用内存的风险。固件加载生命周期适配仍待完成；本轮没有启动固件。

固件/时钟进一步核对：临时挂载debugfs只读采样后已卸载，bi_tcxo=19.2MHz、video_pll0=399999902Hz，mvsc/mvs0/mvs1及iris_ahb均注册但enable计数0；这不是频率设置/硬解负载通过。VIDEOCC保持绑定，boot ID不变。

固件判断纠正：不能凭venus-5.2目录名把本机文件认定为SDM845固件。已通过SSH取回实际文件，SHA256 `4e93d34504da96320775476bbd509aa06f985b48fb781d4436c8eb812b929928`，1159200字节，内部版本为 `VIDEO.IR.1.2-00042-PROD-1`，确为Iris1版本字符串。三个LOAD段均可重定位，跨度5MiB，链接基址0x0f500000不是物理加载地址要求。[固定Surface提取脚本](https://github.com/linux-surface/aarch64-firmware/blob/9f07579ee64aba56419cfd0fbbca9f26741edc90/scripts/getfw.py)明确从qcdx8180包提取qcvss8180.mbn并复制到venus-5.2/venus.mbn；这支持现有路径的来源解释，但本地原Windows采集清单缺失，不能独立证明该二进制的OEM来源。应优先验证这份现有固件，而非先替换OnePlus固件；尚未执行PAS认证/启动。

社区OnePlus7TPro固件仓库 `sm8150-linux-mainline/firmware-oneplus-hotdog` 提交 `5e5c534784a848bc39a02c421cc5273d79678fb3` 的venus.mbn：SHA256 `9d4d4b4d26745a89b37847ebcc3a1f96a00d77e5a791bc3cb71a90e5c992bc4b`，1142816字节，内部版本VIDEO.IR.1.2-00035-PROD-1。inspect-sq2-video-firmware.py解析结果同为三个可重定位LOAD段、5MiB跨度。仅离线对照，未验证OEM签名、SQ2认证或兼容性，未发送至设备。

本轮运行态复核boot ID仍为2f0c17ee…、screen_only=Y、VIDEOCC仍绑定。内核启用DMA_CMA，CMA共32MiB、采样空闲约26MiB；但dma_alloc_from_contiguous/cma_alloc等不是运行内核导出，不能直接用独立模块调用。后续可核对无IOMMU辅助设备的标准DMA分配或独立启动预留；CMA空闲量本身不证明满足固件物理地址/认证要求。未占用任何现有保留区。

特别注意：Surface原DTS明确把0x8b700000的5MiB区标为CAMERA，不是视频固件区；不能因大小相符就占用。当前DT没有明确的Venus reserved-memory。下一步需要建立可证明独占的5MiB加载区（重启预留或受控分配并核对SCM约束），并核实SQ2原固件来源，不能用现有未知reserved块冒充空闲内存。此为待完成适配项，不是已判定无法实现。

VIDEOCC首次实机接入（2026-10-29）：内核确认OF_DYNAMIC/OF_OVERLAY及导出可用，改为临时overlay避免改启动盘。`build-sq2-video-clock-overlay.py`从已审计时钟DTB生成嵌入式overlay模块，要求allow=1、本机compatible为microsoft,surface-pro-x、目标节点不存在及三个外部phandle路径匹配。模块76e1340e…，时钟驱动fd6adde7…，传输后完整哈希一致；overlay全部未定义符号匹配运行内核导出。

实机手动加载overlay返回ret=0/id=1，随后insmod videocc-sm8150成功，`/sys/bus/platform/devices/ab00000.clock-controller/driver`实际指向video_cc-sm8150。boot ID未变、boot_completed=1；未写ESP、未重启、未加载视频固件。ueventd最初因临时模块不在其索引而自动加载失败，之后手动加载绑定成功，不应误判为probe失败。模块/overlay仍在运行；不要在时钟驱动仍绑定时卸载overlay，最简单恢复路径为正常重启。此项只证明VIDEOCC probe接入，尚无视频解码帧或Android硬解组件。

VIDEOCC候选：新增 `build-sq2-video-clock-dtb.py`，固定当前a5a25e43… CPU/GPU DTB，仅添加 `/soc@0/clock-controller@ab00000`。参考上游SC8180X节点（Linux提交062dc4693e2c10d77de06f61e6f3faf37c0a8383的sc8180x.dtsi），采用SC8180X+SM8150双compatible、0xab00000/0x10000、RPMH CXO、MMCX域和现有LOW_SVS OPP。当前6.3 GCC在probe直接开启0xb004 VIDEO_AHB和0xb040 VIDEO_XO，尚无新版GCC_VIDEO_AHB_CLK导出；旧VIDEOCC只消费bi_tcxo，故候选不捏造iface时钟ID。需要实机验证该旧驱动组合，不能由上游节点存在推断通过。

已生成out/video-decode/sq2-video-clock.dtb，SHA256 59966cdf5aacfddc2f9023d42285106bf8e339c67218ca9673458e5f1cdbcf41。全树对照验证原节点所有属性及保留内存不变，仅增加上述节点；未部署、未启动，不含解码器资源或固件启动。后续先验证VIDEOCC probe/父时钟/电源域，再接Iris1硬件节点。

2026-10-29：新增 `audit-sq2-video-modules.py`，读取当次运行内核__ksymtab导出，与九个候选模块的未定义符号及候选间依赖做闭包检查，missing=0；这不是完整ABI证明。六个通用模块的vermagic均为6.3.8-sq2-android1 SMP mod_unload aarch64，传输后SHA256与本地一致。

按顺序手动insmod：videodev、videobuf2-common、videobuf2-memops、videobuf2-v4l2、videobuf2-dma-contig、v4l2-mem2mem，六个均返回成功并在/proc/modules为Live。dmesg确认Linux video capture interface v2.00。boot ID前后一致，boot_completed=1，开始前screen_only=Y。模块仅来自retained root的/tmp/sq2-video-framework，未持久化；正常重启不会加载它们。

本轮尚未加载venus-core/decoder/VIDEOCC，未增加设备树节点或启动固件，没有/dev/video解码实例；Android仍没有硬解组件。该实测只关闭了通用V4L2框架可加载性的缺口。原始符号及报告位于out/video-decode，不纳入Git。

## 已核实

- 运行内核为 6.3.8-sq2-android1；配置快照关闭 CONFIG_MEDIA_SUPPORT，SM8150 VIDEOCC 也未启用。实机无 /dev/video*、/dev/media*，无 Venus 驱动或模块，运行设备树未找到 video/venus 节点。
- 匹配源码位于 WSL /root/.cache/sq2-thermal/kernel；旧 BUILD 中 probe-build/kernel 路径不存在。该源码 Venus core.c 没有 SC8180X/SM8150 match；videocc-sm8150.c 只有 SM8150 compatible。
- Android media.player 实际枚举 AVC/HEVC/AV1 为 c2.android 软件解码器。vendor XML 的 OMX.qcom 条目不是硬解已经接通的证据。
- 本机存在 qcom/venus-5.2 固件和多个 Android donor Venus/VPU 固件，但尚未确认任何一份适用于 Surface SC8180X 的认证、内存布局与 HFI。文件存在不等于可直接启动。
- Android vendor 含 QTI C2 服务、libqcodec2_v4l2codec.so 和 libv4l2r.dylib.so；尚未验证它们与主线 Venus 的 ioctl、缓冲格式及 Codec2 ABI 兼容。
- Bilibili HD 2.0.1 的一次画质列表日志确认有效 VIP、选择 qn=112、显示 1080P；4Ksubhead_state=2 的语义未查明。进程映射 libijkffmpeg.so，当时系统资源管理器未记录其 codec 实例。不能仅据此断言其隐藏 4K 的确切条件。用户报告同一 BV1NGZtBwELa 在 Chrome 可播放 4K；Chrome 的编码/解码路径尚未实测。

## 实现前的具体缺口

### 当前主线：855参考 + Venus/V4L2 + 开放Codec2

后续推进（2026-10-29）：实机只读确认screen_only=Y、现有venus-5.2/venus.mbn SHA256为4e93d34504da96320775476bbd509aa06f985b48fb781d4436c8eb812b929928，尚未证实该固件适合Iris1。补取同一OnePlus提交的msm_vidc_platform.c、venus_hfi.h、hfi_packetization.c等实际存在文件并更新哈希。sm8150_data明确选择VPU_VERSION_5；venus_hfi初始化消息使用HFI_PACKETIZATION_4XX，同时上电/中断函数选择vpu5_ops。这是区分硬件版本与消息版本的直接证据，不能把VPU5数字简单映射为HFI版本，也不能只添加compatible后套现有v4电源流程。

配置修复：MEDIA_SUPPORT_FILTER使底层配置菜单不可见，原disable被默认值覆盖。现在关闭该过滤、在首次olddefconfig展开后再次禁用MEDIA_CONTROLLER/DVB/USB和PCI媒体驱动/调谐器，强制检查MEDIA_CONTROLLER与USB音频耦合未开启。构建前清理独立media目录产物以免旧模块残留混入清单。此更改仍为compile-only；缺Module.symvers的限制保持。此次没有加载模块、触发固件或修改DTB；不能称为硬件启动尝试通过。

用户选择此路线，Quest闭源材料保留为辅助参考，不再优先接入其服务。已从 LineageOS/android_kernel_oneplus_sm8150 固定提交 `32f26bf34ddf17df8e224ae0703656b9fe0c1a8d` 取得 `sm8150-vidc.dtsi` 和 `venus_hfi.c`，文件哈希见 `configs/sm8150-video-reference.json`。这是OnePlus设备的社区维护下游内核，不能称为Surface官方参考。

855 DTS提供：寄存器0xaa00000/0x200000、GIC SPI174、mvsc/mvs0/mvs1电源、三路GCC AXI及三路VIDEOCC时钟、非安全SMMU 0x1300/mask0x60、IOVA池0x25800000+0xba800000。这些仅为SM8150数值，尚未验证可用于SC8180X，不写入实机DT。

新增 `build-sq2-video-foundation.sh`，从现有配置在独立 `/root/.cache/sq2-video-foundation/build` 启用媒体/V4L2/Venus及SM8150 VIDEOCC，构建通用模块，不生成或安装Image/DTB。首轮编译完成；配置审计发现媒体自动选择额外启用I2C_MUX/USB音频媒体控制关联，随后关闭媒体子驱动自动选择和MEDIA_CONTROLLER并重编。构建缺原Module.symvers，允许modpost未解析符号警告，因此只证明编译，不能装载或视为ABI验证。产物、配置差异和日志留out/video-decode/foundation。

二轮编译完成：I2C_MUX额外变化已消除，但Kconfig仍自动启用MEDIA_CONTROLLER及SND_USB_AUDIO_USE_MEDIA_CONTROLLER，显式disable未能消除；不要把候选配置当作可直接替换运行内核的配置。下一步需核对其选择关系及既有USB音频模块ABI。

尚缺：SC8180X资源交叉核实、Iris1对当前Venus HFI/电源实现的补丁、匹配固件、实机V4L2帧验证及Codec2接入。构建基础模块不代表这些适配已完成。已有默认镜像与eDP工作均未改动。

### Quest 2 闭源路线：2026-10-29 离线核验

用户指定参考此前 Vulkan 的开发过程，以 Quest 2 XR2 Android 闭源视频栈为用户态 donor，SM8150 下游实现仍作为 Iris 1.x 硬件参考。

当前工作区缺少先前完整 vendor 材料，已重新下载固定 OTA prefix/vendor 两段，使用原重建器核验131项操作及 vendor SHA256 44f90742…，重新只读提取906个文件。没有验证完整OTA签名，也没有刷写任何分区。

新增 `scripts/inspect-quest2-video-reference.py`，从固定分区身份及提取清单逐文件复核长度/SHA256，再用 readelf/strings 做离线分析。实跑核验54个视频相关文件，原始报告保留在 `out/quest2-gpu/video-reference-audit.json`；固定文件哈希见 `configs/quest2-v77-video-reference.json`。不执行 donor、不接入系统服务。

- 32/64位 `libOmxVdec.so`、`libqcodec2_v4l2codec.so` 均存在；QTI Codec2服务和 OMX服务本体为32位。64位解码库存在不代表有可直接使用的64位服务。
- C2库包含 `/dev/video32`、`/dev/video33`、私有 `V4L2_CID_MPEG_VIDC_VIDEO_BASELAYER_ID` 及 NV12/TP10/RGBA UBWC字符串。OMX库另有 `/dev/video/q6_dec`，依赖 `libion.so`、`libqdMetaData.so`、`libc2dcolorconvert.so`、`libplatformconfig.so`。
- donor XML列出 c2.qti AVC/HEVC/VP8/VP9 decoder，非secure AVC/HEVC最大4096×2304。此为Quest声明，不能作为SQ2实测能力或复制进当前系统。
- 字符串证明候选含这些接口路径，不证明每个路径都在本机运行时必然触发。须通过私有探针补足调用证据。

后续分层推进：先独立进程验证64位原库依赖/初始化，记录实际open/ioctl与内存分配契约；同时以SM8150参考建立SQ2硬件后端。只在有真实解码结果后才接Codec2服务。Vulkan mapper桥接尚未覆盖视频的全部UBWC/10bit/ION语义，不能原样复用或返回虚假ioctl成功。尚未生成可部署硬解候选，默认系统不变。

需要可信 SC8180X/Iris 1.x 硬件参考来确定寄存器区域、IRQ、SMMU stream ID、电源域、时钟/reset、互联带宽、固件内存保留与 HFI 版本。不能把 SM8250/SC8280XP 的设备树或资源表改名后直接启动。

1. 获取并固定本机或同 SoC 的下游源码/固件参考，核实上述资源；研究 SC8180X VIDEOCC 上游改动的可回移性。
2. 独立目录构建媒体模块和必要内核增量，保留现有 screen_only、NVMe、FUSE、音频、触摸和温控。仅构建通用 Venus 模块不足以完成 SoC 适配。
3. 独立候选 DTB/启动入口验证固件启动和 V4L2 枚举，再用已知 H.264/HEVC 样本做真实解码及像素/帧数检查，逐步升至 4K。
4. 对照 QTI C2 与 AOSP v4l2_codec2 接口，接入可用的真实硬解组件；验证普通应用的 MediaCodec、缓冲导入、seek/释放和软解回退。
5. 最后复验 Bilibili 画质与实际 codec。硬解接通本身不保证服务端开放 4K。

参考：[AOSP v4l2_codec2](https://android.googlesource.com/platform/external/v4l2_codec2/)、[上游 SC8180X DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sc8180x.dtsi)、[上游 Venus](https://github.com/torvalds/linux/blob/master/drivers/media/platform/qcom/venus/core.c)。后两者是可变参考，不是已固定/测试的补丁输入。

原始日志含用户与播放签名信息，不纳入 Git。其它工作产生的 DISPLAY-REFRESH 和 eDP 未跟踪文件保持不动。
