# 构建与部署

会话P010主机测试：`g++ -std=c++17 -fsanitize=address,undefined -fno-pie -no-pie -Iprobe/video tests/sq2_v4l2_session_test.cpp -Wl,--wrap=open,--wrap=close,--wrap=mmap,--wrap=munmap,--wrap=poll,--wrap=ioctl -o TEST`后运行。覆盖NV12/P010生命周期与异常geometry/payload，非实机证明。open末尾显式P010参数仅用于未来10-bit消费者，现C2默认仍NV12；预算未扩大。

P010 helper离线验证：`python3 tests/test_sq2_p010_selection.py /root/.cache/sq2-video-p010-v3/venus`提取候选实际C函数并结合已记录固件格式表做ASan/UBSan测试，覆盖HEVC/VP9 P010成功、缺格式拒绝、其它codec拒绝、NV12不变、默认数组索引及真实P010 sizeimage。4K P010为25067520B，当前16MiB单帧限制仍需端到端适配；此测试不是硬件通过。

P010原生候选：当前prepare脚本生成新stage带SQ2限定P010/TP10-UBWC路径；仅/root/.cache/sq2-video-p010-v3/venus编译版本有效，前两版格式索引错误废弃。尚未打包/部署或做新实机符号审计。以Main10 header构建原生runner时自动选择P010；`decode-test --reference-test ref.p010`可在QEMU验证双字节行宽与padding。默认8-bit同样支持此参考检查。Android C2输出尚未扩展10bit，不得用此候选直接提升系统能力。

Main10素材：`python3 scripts/build-sq2-hevc-test-vector.py --bit-depth 10 --out NEW`生成真实10-bit低位图样、Main10码流、ref.p010和CRC。默认8-bit输入/参考保持不变。现NV12 runner会编译拒绝10-bit header；须先实现P010原生输出与byte stride校验，不可直接喂给旧runner或作为硬解通过证据。

MPEG2事件诊断：sample-hfi-events.sh只采既有HFI消息1秒，不提交硬件工作，已确认0x1008 unsupported-setting事件洪泛。sample-mpeg2-first-error.sh是尚未实跑的下一轮诊断，必须干净native启动及独立有界collector先就绪，首个该错误自动停trace以保留此前set_property记录。当前坏会话已发正常入口重启，恢复状态见HANDOFF，不要继续当前会话重试。

MPEG2原生向量：`python3 scripts/build-sq2-mpeg2-test-vector.py --out NEW [--size 320x256]`，缺省320x240；用`-DSQ2_MPEG2_VECTOR -INEW`编译sq2-v4l2-decode-test.c。严格保留尺寸/CRC检查。两个尺寸均未通过，320x240缺裁剪，320x256无输出且close未确认；不要在当前驻留会话继续重试。sample-mpeg2-geometry.sh只采集固定HFI事件结构标量、独立trace instance/25秒timeout，详见HANDOFF。

VPx组件像素测试：`python3 scripts/build-sq2-c2-component-test.py --codec vp8|vp9`分别输出codec2-integration/component-vp8或component-vp9/interface-test；缺省AVC保持component-backend。使用独立原生向量与实际C2GraphicView布局重组NV12校验。设备Android namespace内、已有resident后端时，以既有LD_LIBRARY_PATH运行`--decode`（外层30秒timeout）。VP8/VP9与AVC均六帧CRC/EOS/stop/release通过；不能据此宣称普通APK路径通过。

VPx Android候选：`python3 scripts/build-sq2-c2-aidl-service.py`构建四组件store；`--interface-test`构建独立parameter-test（不覆盖store二进制），检查VPx profile接受/拒绝及AVC/尺寸/pool回归。`bash scripts/build-sq2-video-app.sh 320x240 vp8|vp9`生成WebM资产的300帧APK，runner对应vp8-small/vp9-small。当前仅设备store和参数检查通过，未替换运行owner、未播放VPx；完整能力范围仍待扩展，不得提升默认镜像。

能力v3：prepare-sq2-venus-iris1.py生成新目录时包含逐codec尺寸枚举和严格限定的SQ2 VP9空EOS处理。`gcc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -Iprobe/video tests/sq2_vp9_eos_test.c -o TEST`验证实际helper的错误保留。`sample-vp9-eos.sh packet`仅用于驻留native下的一次有界取证，输出拒绝覆盖，退出移除专用kprobe/trace instance。`pack-sq2-video-capabilities.py BASE /root/.cache/sq2-video-caps-v3/venus NEW --revision 3`固定本轮模块hash；缺省revision1保留原候选。v3尚未实机加载，不能视为VP9已修复；见HANDOFF。

VPx原生向量：`python3 scripts/build-sq2-vpx-test-vector.py --codec vp8|vp9 --out NEW`，`aarch64-linux-gnu-gcc -O2 -static -DSQ2_VPX_VECTOR -INEW probe/video/sq2-v4l2-decode-test.c -o NEW/decode-test`。IVF逐包验证尺寸/PTS/边界后剥离容器，输出头含真实码流与参考CRC。现阶段VP8完整通过，VP9像素通过但EOS错误，见HANDOFF；禁止只因像素匹配忽略EOS错误标记。

能力候选增量打包：`python3 scripts/pack-sq2-video-capabilities.py out/video-decode/boot-candidate/experimental.cpio.gz /root/.cache/sq2-video-caps/venus OUTPUT.cpio.gz`。固定a5038428…基线和已审计模块哈希，只覆盖core/dec及其SHA256SUMS，所有其它最终条目不变，密码SSH addon最后。产物仍为私有实验镜像，脚本不部署、不重启；旧C2启动服务保持原版，新Android owner/mapper未由此持久化。现场身份和一次性启动回退须另行核对，不能复用USB写盘脚本。

能力解析候选：prepare-sq2-venus-iris1.py现在把真实SYS_INIT能力解析限定到SQ2分支，要求完整属性计数并事务式发布。`gcc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -Iprobe/video -Itests/sq2_caps_host tests/sq2_caps_transaction_test.c -o TEST`，`TEST sys-init.bin`验证实际表；同编译参数构建sq2_hfi_properties_test.c验证walker。sys-init.bin是parse-sq2-hfi-caps.py的load()返回字按little-endian u32保存，留在out，不提交原始日志。候选stage /root/.cache/sq2-video-caps/venus编译通过，仅编译；缺Module.symvers的modpost警告仍需实机导出闭包核对，禁止热卸现有native/PAS。

HEVC实验：`python3 scripts/build-sq2-hevc-test-vector.py --out NEW`生成6帧Main无B帧及独立NV12参考；`aarch64-linux-gnu-gcc -O2 -static -DSQ2_HEVC_VECTOR -INEW probe/video/sq2-v4l2-decode-test.c -o NEW/decode-test`，只在已有resident native后端运行，不重新启动PAS。`bash scripts/build-sq2-video-app.sh SIZE hevc [30|60]`生成10秒、300/600帧含B帧APK；test-c2-player.sh标签hevc-small/hevc-4k60/b/c自动选择HEVC组件。Main10尚未接入。部署/结果以HANDOFF最新记录为准，旧打包器尚未更新新双组件/私有mapper，不能直接打包发布。

AVC profile实验：build-sq2-video-app.sh SIZE [baseline|main|high]，Main/High带B帧，输出目录带profile后缀；vector.json保存实际profile/level和帧型。build-sq2-h264-test-vector.py --out NEW --profile high生成6帧显示序参考及解码序PTS；新版sq2-mediacodec-frames.c兼容旧header并使用新PTS。私有缓存当前16项/192MiB，详见HANDOFF，旧128MiB描述为历史。

私有映射缓存候选：先`python3 scripts/prepare-sq2-minigbm-map-cache.py`（新stage拒绝覆盖），再`bash scripts/build-sq2-minigbm-map-cache.sh`。依赖固定minigbm源码及out/video-decode/map-cache-libs中的本机donor/libdrm/libcutils；仅donor目前固定哈希，未作为公共可复现构建发布。产物不安装。sq2-map-cache-private.c通过新mount namespace隔离bind库，参数0/1运行allocation-only probe；--serve仅用于实验C2，要求/data/local/tmp/sq2-c2-owner具有执行位且匹配已测hash，不能作为正式启动发布。禁止用LD_PRELOAD跨sphal共享候选，实测会缺retain符号。当前私有内存A/B成功，真实4K和完整同步/资源生命周期回归仍待测，见HANDOFF。

AIDL 实验入口：`scripts/prepare-sq2-c2-aidl-headers.py` 使用已克隆的硬件接口/原生框架缓存与 SDK35 aidl，必须同时生成版本和 hash 方法；`scripts/build-sq2-c2-aidl-service.py` 构建独立 store，依赖已采集的设备库。无参数仅本地枚举，`--serve` 尝试正常 default1 注册；`--serve-local` 仅用于有界实验 Binder 客户端测试，不是正式 VINTF/MediaCodec 接入，禁止默认开机启用。客户端源码 `probe/video/sq2-c2-aidl-client-test.cpp`。

独立 C2 实验组件：`probe/video/sq2-c2-v4l2-component.h` 使用真实 C2Work/C2BlockPool 与 `sq2-v4l2-session.h`；测试入口 `sq2-c2-component-test.cpp` 无参数只释放对象，带参数才运行向量测试。当前仅独立进程验证，不安装服务；构建入口 `python3 scripts/build-sq2-c2-component-test.py` 校验固定 AOSP 提交及本机四库哈希；需要已准备的 NDK、平台 libc++ 头副本和测试向量。完整命令见本地 `out/video-decode/codec2-integration/component-backend/build.txt`。后续须补依赖自动准备、异步输出、完整参数/能力和 AIDL/MediaCodec 验证，不能当作成品部署。

V4L2 runner新增 `--dmabuf /dev/video0`：system dma_heap为输入和输出提供进口fd，显式DMA_BUF_SYNC，沿用严格六帧CRC/EOS检查；无模块装载/固件重启。已在同boot驻留原生core验证，证据 `out/video-decode/venus-transport/dmabuf-frames`。`tests/test_sq2_v4l2_runner.py` 覆盖fd生命周期与分配失败清理；不是gralloc/Codec2实现。

原生V4L2实验runner：用aarch64-linux-gnu-gcc -static并加-I out/video-decode/h264-vector构建probe/video/sq2-v4l2-decode-test.c。--format-only仅设置格式；--buffers-only追加REQBUFS/QUERYBUF/mmap但不QBUF/STREAMON，两者均close。默认模式才尝试六帧，曾实机失联，不能默认重跑；配置ioctl日志须重定向Android持久/data目录并保留fsync。tests/test_sq2_v4l2_runner.py以实际main+wrapped syscalls验证不送帧边界，不是硬件证明。venus-dec的sq2_stage_trace=1仅显式诊断、最多128条；默认关闭。新boot仅一次PAS，不热卸clock/overlay；本机已有resident core时不要运行freshboot装载脚本。

真实H264帧探针：`build-sq2-h264-test-vector.py --out NEW_DIR`用ffmpeg/libx264生成6帧320x240 baseline Annex-B及独立NV12参考/CRC；构建设置`SQ2_HFI_DECODE_VECTOR=.../sq2-h264-vector.h`，同一`build-sq2-iris1-phase.sh --hfi`会固定header dfe3c475…并额外测试dec helper。模块需显式hfi_decode=1且hfi_session/hfi_caps/完整HFI条件成立，默认不解码；bufreq/输入/UBWC-DPB/线性OUTPUT2及scratch都在独立coherent DMA内，直到真实PAS停止后才释放。21c883a3…已实测6/6 Y/UV CRC一致；合成dec19项、caps40项、ring/事务测试通过，缺Module.symvers的审计边界不变。只允许每boot第一次固件启动，不卸载提供者。

离线证据与能力对齐：先用parse-sq2-hfi-caps.py解析本轮raw kmsg，再运行`report-sq2-video-capabilities.py --caps CAPS_JSON --kmsg KMSG --vector VECTOR_JSON --android MEDIA_PLAYER_TXT --output NEW_JSON`。核对全部六帧唯一/尺寸/两个CRC、EOS及关闭标记，拒绝缺失/不一致，报告固件声明与Android实际枚举，不自动改XML。六种decoder的紧凑声明矩阵在configs/sq2-video-decoder-capabilities.json；只有H264小向量验证，不代表上限、其它编码或Android组件已通过。

HFI能力体/session候选：同一`build-sq2-iris1-phase.sh --hfi`现额外复制sq2-hfi-caps.h并运行tests/sq2_hfi_caps_test.c；ring测试增加整包复制/生产者写入。消费者参数`hfi_caps=1`需hfi_handshake，`hfi_session=1`需hfi_caps；默认不启用。build会覆盖out/video-decode/iris1-phase，先备份旧候选。已在干净boot首次固件启动实测SESSION_INIT/END error0；同boot第二次固件启动失败，故每boot只跑一次。离线解析：`python3 -I scripts/parse-sq2-hfi-caps.py KMSG --output NEW_JSON`，拒绝覆盖输出，遇未知属性即停止；只读日志，不连设备。

已实测Windows HFI profile：`build-sq2-hfi-overlay.py --windows-reference --with-interconnects --videocc-phandle LIVE --stage NEW_DIR`显式选三条精确2300/2320/2340 SID及属性；没有选项时保留legacy1300/mask60。`build-sq2-iris1-phase.sh --hfi`消费者按该属性使用24/192表和16MiB UC，将IOVA限于原Android与Windows推断非安全池交集，不改SCM阈值。只发布CPU邮箱，并给CTRL_INIT写、VERSION写（仅legacy）及状态轮询分别打标。93cd5b7d…+a8e7c9ff…实际SYS_INIT成功和真实关闭/回收0，非帧解码/MediaCodec。窗口、参数与运行状态见VIDEO-DECODE、configs/sq2-video-hfi-status.json；每次build覆盖out固定目的前先保留旧候选。不能热卸载VIDEOCC/overlay。

独立SID主机DMA对照源码仅保留取证。两profile及哈希回滚副本实测16MiB/4096页映射与全页解除映射0，但overlay移除API虽0却产生OF引用计数/泄漏告警，未修复；不可重复热拆。builder现默认拒绝，只有取证重建显式`--allow-known-overlay-lifetime-warning`才可生成；不得把此开关当作修复或常规执行入口。后续使用已驻留HFI overlay的dma_probe，避免该拆除路径。真实VPU通信证据另见HFI成功记录。

Windows参考指令检查：`python3 scripts/inspect-sq2-windows-video-reference.py --driver PRIVATE_QCDXKM8180_SYS --iort PRIVATE_SQ1_IORT_DAT --output NEW_JSON`。输入严格限14369cb4…驱动及acfe9033…IORT；输出拒绝覆盖。固定检查24/56/192队列表、UC16MiB及基址表达式、CTRL_INIT写1和十项视频相关IORT映射。SQ1/SQ2保存驱动分别通过且结果相同，9项正负例检查通过；只读离线，不部署、不证明固件DMA/硬件帧。证据地址及语义边界见SQ1-VIDEO-REFERENCE。

SQ1/SQ2 Windows只读视频参考采集：管理员原生64位PowerShell执行 `scripts/Collect-SurfaceVideoReference.ps1 -ExpectedProcessor SQ1 -CheckOnly` 先核对型号，再去掉CheckOnly并给新的OutputDirectory。默认仅导出图形/相关平台驱动、PnP资源及白名单ACPI，排除许可证/凭据；不改旧Collect-SQ2型号保护，不安装驱动、不重启。只在子进程需要时使用ExecutionPolicy Bypass，不改持久策略；归档必须留private并按manifest逐项核验。SQ1实测及与SQ2同字节驱动/SID差异的证据边界见SQ1-VIDEO-REFERENCE.md；不是Android硬解安装器。

固定固件静态检查：`python3 scripts/inspect-sq2-video-firmware-protocol.py` 对out/test2/firmware/qcom/venus-5.2/venus.mbn只读校验完整哈希、ELF32 ARM及六组协议指令，报告在out/video-decode/firmware-analysis/protocol.json；不执行/部署固件，不等于硬件DMA验证。新消费者的 `hfi_empty_ping=1` 只允许完整HFI模式，以初始rx_req=0和固件空队列写1作为直接共享队列写入判据，再继续SYS_INIT；编译/预检通过，尚未实测。CTRL_STATUS=1可早于队列解析，不能算完整HFI成功；不要在未取compact日志时重试或改猜测的IRQ位。见VIDEO-DECODE。

HFI控制启动与表头对照：只写邮箱候选已实测CTRL_STATUS=1，但SYS_INIT未消费，仍非硬解通过。`hfi_compact_table=1`仅在hfi_handshake模式比较原24字节表头与后期288字节扩展，默认保持288；环及SFR仍在同一独占3MiB范围。首轮24字节实测连接中断，原日志未取回，不能解释成该布局失败。新消费者构建还固定参考venus_hfi.c哈希；固件调用后且真实停止确认成功、时钟关闭后，对本机HANA版本脉冲四路桥接，再恢复初态/关域/撤销ICC，保留所有原始错误及post_reset日志。这个退出补齐仅编译未部署，不是已验证的二次启动修复。当前先取回sq2-hfi-compact-976c.kmsg；不要连续重试。见VIDEO-DECODE。

ICC前置请求：`build-sq2-hfi-overlay.py --stage NEW_DIR --videocc-phandle LIVE_VALUE --with-interconnects` 固定6.3 SC8180X驱动/绑定头哈希，添加cpu-cfg、video-mem、firmware-mem路径；不修改已有provider。`--hfi`消费者用 `allow=1 vote_interconnects=1 icc_probe=1` 独立检验三条请求及撤销，不上电/reset/MMIO/PAS。此阶段已实测0。带 `trace_delay_ms=100` 的三域桥接、六时钟/AHB/版本读取也实测0；延迟只让独立collector有机会落盘，不是总线挂死的超时保障。完整HFI随后仍失联、日志待取，不可称已修复。后续候选已删去所有额外MMIO/邮箱快照读回，只按下游写邮箱，确认PAS关闭后将独占邮箱清零再释放映射；增加邮箱写入前后标记与有界延迟。新版本仅编译/符号审计，禁止重用旧全流程二进制，先取回sq2-icc-only-19ed.kmsg定位。见VIDEO-DECODE及sq2-video-interconnect-reference.json。

独立HFI队列：`build-sq2-hfi-overlay.py --stage NEW_DIR --videocc-phandle LIVE_VALUE`只给已绑定VIDEOCC增加独立SMMU消费者（默认不装）；必须与live树合并核对，不能用raw FDT推测运行期phandle。`SQ2_IDENTIFY_STAGE=NEW_DIR bash scripts/build-sq2-iris1-phase.sh --hfi`加入专用compatible检查、DMA/队列库和ASan/UBSan ring边界测试。`dma_probe=1`只做分配/映射/CPU读回并释放，与电源/固件参数互斥。`hfi_handshake=1`需要完整桥接/三域/六时钟/AHB/200MHz/read_registers/boot_firmware/pil_gap；`protect_iova=1`只允许该模式，按固定sdmshrike虚拟地址池设置VPU保护阈值。首轮控制启动超时并安全回收；保护调用后扩展TZ读回导致失联，新源码已去除TZ阈值/reset读取。**禁止使用65709696…旧候选，禁止非安全读取e1020..e1034；修订候选尚未部署，不是硬解安装器。** 继续使用独立有界collector、实际probe_result及真实PAS关闭结果；失败关闭时保留全部队列/映射。见VIDEO-DECODE。

PIL窗口与私有内存owner：`build-sq2-scm-trace.sh` 在新的SQ2_SCM_TRACE_STAGE构建有界只读观察器和GET_MEM_AREA query；固定三份SCM源码/头ABI。`build-sq2-iris1-phase.sh --firmware` 现链接query库，额外固定Surface板级DTS哈希。`pil_gap=1 inspect_gap=1` 只在零域/零时钟/不读寄存器条件下检查独占申请、映射及释放，不写数据；真正加载须另选boot_firmware、pil_gap及完整桥接/AHB/200MHz条件。地址不可参数化，只允许已有板级文档空隙中的固定6MiB，并复核live SCM/DT/资源树。私有加载/认证/关闭通过，未持久化或覆盖V4L2/HFI。不要将新owner应用于CAMERA或其它reserved区；明细见VIDEO-DECODE及sq2-video-pil-memory-reference.json。

桥接/PAS消费者：新overlay提供四reset与额外AHB；`build-sq2-iris1-phase.sh --firmware` 才链接PAS事务层，固定MDT源码哈希并运行故障注入，默认所有危险阶段仍关闭。新目录通过SQ2_IDENTIFY_STAGE选择。reset_bridge需要三域，enable_ahb需要三域且至少三AXI时钟；read_registers要求完整桥接/AHB/六时钟；core_hz仅允许200000000并核对±1kHz，boot_firmware要求以上全部条件和实读版本5010002f。guard与实际probe_result必须复核；insmod成功不等于probe成功。实际试验在PAS mem_setup以-22停止，shutdown/回收通过，未到认证。不要据此持久化或更换未知加载区；完整结果和运行态在VIDEO-DECODE。

分阶段消费者：`bash scripts/build-sq2-iris1-phase.sh`，可用SQ2_IDENTIFY_STAGE选新的构建目录。它只绑定已有私有节点，不创建/移除overlay或VIDEOCC；默认仍不启域/时钟/MMIO，必须以独立有界kmsg和实际probe_result判断。`python3 scripts/build-sq2-videocc-sw-domains.py --stage NEW_DIR` 只生成固定源码的两codec GDSC软件控制候选，不改时钟检查/频率，不安装。该差异通过三域六时钟启停实测，但HW_CTRL运行期交接未实现，不能部署长期默认。热卸载时钟提供者后读pm_genpd_summary曾陷入内核循环，禁止复用该过程；下一次需干净启动直接加载候选，保留启动盘原样。桥接reset/HFI/PAS/Android接入仍开放。

Iris1独立识别探针：`python3 scripts/build-sq2-iris1-identify.py --stage NEW_DIR` 固定out/video-decode/running-base.dtb的be17f33f…身份，使用dtc/fdtget/fdtput构建临时overlay及模块。删去__symbols__导出但保留本地引用重定位，以兼容当前无symbols启动树。v3导致一次用户手动恢复，不能原样重复；v4新增逐步标记、默认0域/0时钟/不读MMIO，仅编译未测。禁止据overlay/符号检查通过认定电源或版本可读。下次先独立运行有界kmsg采集和远程逐步标记；不要无日志重复上电。新overlay模块驻留到重启，旧VIDEOCC/overlay拆卸曾有OF引用泄漏警告。具体失败/恢复见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

PAS内存所有权部分候选：`SQ2_IRIS1_STAGE=/root/.cache/sq2-video-iris1-lease-v2/venus SQ2_IRIS1_DEST=out/video-decode/iris1-lease-v2 bash scripts/build-sq2-venus-iris1.sh --firmware-lease` 在新目录编译独立事务层，先运行ASan/UBSan故障注入和寄存器mock。新目录必须不存在；状态/源码哈希记录在candidate.json。该层尚无启用调用点，不能用它单独启动固件；core/HFI资源在关闭失败时的整体保留仍待适配。不得将主机故障注入说成实机PAS故障验证。

Iris1 HFI部分候选：在foundation构建目录就绪后执行 `bash scripts/build-sq2-venus-iris1.sh`。默认在 `/root/.cache/sq2-video-iris1/venus` 生成源码副本，拒绝覆盖已有候选；可用 `SQ2_IRIS1_STAGE` 选另一个新目录。固定源文件哈希，先运行实际helper的ASan/UBSan mock，再构建core/dec/enc，日志和清单在 `out/video-decode/iris1-hfi`。该候选没有SC8180X match/资源表，不是可安装解码器；不要据符号闭包通过就加载固件。完整限制见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

视频固件copy-only探针：先在固定内核归档恢复的 `/root/.cache/sq2-thermal/kernel` 执行 `bash scripts/build-sq2-video-foundation.sh`，再执行 `bash scripts/build-sq2-video-firmware-stage.sh`。后者先运行主机ELF边界测试，固定MDT源码哈希，再构建独立模块，输出到 `out/video-decode/firmware-stage/`，不连接或安装设备。可用 `SQ2_VIDEO_KERNEL` / `SQ2_VIDEO_BUILD` 显式选择对应源码/构建目录；缺原Module.symvers，仍需对当次实机导出审计。`audit-sq2-video-modules.py --module out/video-decode/firmware-stage/sq2_video_firmware_stage.ko --output out/video-decode/firmware-stage/preflight.json` 使用含screen_only=Y的当次kallsyms采集。传输后核对完整哈希、vermagic与固件哈希，再临时insmod allow=1；返回前已释放内存及设备，结束rmmod并检查残留。无PAS调用或VPU启动，不能作为硬解安装器；实测和限制见 [VIDEO-DECODE.md](VIDEO-DECODE.md)。

eDP 速率表候选见 [DISPLAY-REFRESH.md](DISPLAY-REFRESH.md)。`build-sq2-edp-rates.sh` 使用独立 msm 源码副本；无 Module.symvers 时必须执行 `restore-sq2-msm-depends.py` 恢复原模块依赖声明，不能仅验证运行内核已有符号就部署。原始模块须为固定哈希。`pack-sq2-edp-rates.py` 仅改变 msm.ko 和测试用 init 钩子，保留密码 addon 最后；候选尚未通过硬件验证。脚本和安装器是固定阶段输入，不可直接复用到未知镜像或磁盘。

用户于 2026-10-29 要求菜单仅保留闭源 Vulkan、原 Mesa、当前 eDP 调试三个入口，覆盖此前保留独立救援/关机菜单的偏好。默认仍是闭源 Vulkan，测试使用一次性 env 和 240 秒回退。诊断 Wi-Fi 中 flyer-test 不适用于当前环境；后续测试用有界本地日志，不开启这套诊断联网。无引用旧 initramfs 在本地 out 中校验备份后才能从已核验的设备 ESP 删除，不能删除共享镜像。

闭源Vulkan启动候选：`build-quest2-vulkan-formats.py --boot-runtime` 生成runtime-v8，功能沿用v7，仅关闭成功调用统计及逐次BGRA导出提示，保留错误/所有权检查。`pack-sq2-closed-vulkan.py BASE BUNDLE OUTPUT --preserve-lab-ssh` 固定当前fa64305d…基线及v8九库哈希，新增独立候选目录与 `/init` 钩子；所有原vendor覆盖文件（含Mesa）、凭据和其它助手保持字节一致，密码SSH addon仍最后。不部署、不操作磁盘，输出拒绝覆盖。

`closed-vulkan-start` 在可写RAM vendor覆盖层就绪、Android init之前核验哈希并接入系统HAL；`sq2.vulkan=mesa`、`sq2.vulkan=0` 或救援模式跳过，保留Mesa。Direct配置由 `sq2-closed-vulkan.rc` 在Android post-fs-data后复制到原私有路径，没有启动daemon/持续日志/网络操作。初始化顺序参考[AOSP init文档](https://android.googlesource.com/platform/system/core/+/refs/heads/main/init/README.md)。此候选尚无CTS认证，首次部署必须独立文件/一次性菜单，保留正常及救援入口，不能因构建成功就提升默认。

离线检查：`SQ2_VULKAN_BOOT_TEST_IMAGE=out/quest2-gpu/boot-runtime-v8.cpio.gz python3 tests/test_closed_vulkan_boot.py`，使用候选中的真实助手，在临时目录验证正常接入、三个回退参数保持原文件、损坏库先拒绝后零修改。测试不抽取密码或修改主机挂载。仍须实机重启验证，完整状态见QUEST2-VULKAN-ONLY。

颜色格式候选：WSL中 `python3 scripts/build-quest2-vulkan-formats.py --bgra-export` 从固定runtime-v5生成runtime-v7（拒绝已有输出）；无参数v6仅为BGRA导出失败的中间诊断。整套九库及Direct配置同步，私有runner选择 `SQ2_VK_RUNTIME=7`。临时系统runner也支持v7并将旧Mesa1.3 XML临时改为真实1.1，开始/回退均刷新zygote；仍不写启动镜像。AHB格式、大小、轮次及超时结果见QUEST2-VULKAN-ONLY的runtime-v7节。不得把新格式AHB读回等同于其交换链或完整CTS通过。

当前目标：保留 Mesa EGL/GLES，仅替换系统 Vulkan 后端。闭源 EGL/GLES 研究保留在分支，不再切换桌面 GLES。

新增诊断候选：`build-quest2-vulkan-app-diag.sh --image-info`（版本4）记录native准入失败参数；`--native-srgb`（版本5）启用有条件的RGBA8 sRGB支持。窗口探针 `SQ2_VULKAN_WINDOW_SRGB=1` 检验线性0.25→sRGB137及alpha。版本5配合私有Direct配置已跑完Wild Life且用户确认正常，但不是默认驱动。系统测试显式 `--start-manual` 关闭120秒自动回退，务必保存输出的手动回退命令；默认 `--start` 保持原策略。详见QUEST2-VULKAN-ONLY。

最新进展：多队列重复释放已有候选修复；普通 APK 的外部句柄错误已定位为 `/proc/self/mem` 权限拒绝，改用有边界检查的 `process_vm_readv` 后，2880×1920 三角形完成120帧，截图正常。Chrome可正常打开，前八个被捕获的 Vulkan 管线创建成功，但后续部分管线仍有链接错误，不能称为Chrome整体失败，也不能据网页可见认定全部Vulkan路径通过。

历史Wild Life初始化失败已由上方sRGB + Direct结果更新，原始失败仍保留在 [QUEST2-VULKAN-ONLY.md](QUEST2-VULKAN-ONLY.md)。

`python3 scripts/build-quest2-vulkan-runtime.py` 生成runtime-v2；加 `--ahb-allocate` 生成runtime-v3。输入为固定counter-v1九库；v3还需当前平台链接库 `out/quest2-gpu/platform-link/{libc++,libhardware}.so`，脚本固定其哈希，输出拒绝覆盖。使用NDK和Mesa android_stub/u_gralloc头；生成九库、Direct配置、SHA256SUMS及标明未通过CTS/启动验证的candidate.json。通过 `build-sq2-vulkan-probe.sh` 构建新AHB测试后应更新探针哈希清单，同步可执行文件与清单；不能仅替换清单。测试矩阵、已通过结果和未完成项见同页。

脚本来自迭代实验室，依赖缓存和分阶段产物；全新克隆不能直接生成完整可启动 SSD。不要删除哈希或磁盘身份保护以绕过缺失输入。

缓存适配候选：在固定runtime-v3及平台链接库就绪后，WSL执行 `python3 scripts/prepare-quest2-cached-memory.py --scoped` 生成runtime-v5；不带该参数的v4仅供分阶段诊断。脚本核对GSL指令与全部输入哈希，执行实际缓存选择指令的512组QEMU检查，拒绝已有输出。v5增加私有GSL分配转发和基于真实HOST_CACHED属性的线程局部作用域；不得单独混用库。同步九库、Direct配置及SHA256SUMS，私有runner/临时系统runner选择 `SQ2_VK_RUNTIME=5`。`SQ2_VULKAN_MEMORY_TYPES=1 SQ2_VULKAN_MEMORY_CYCLES=150` 配合私有runner `--offscreen` 验证内存；与AHB、窗口、真实应用测试串行执行。分配跟踪仅限 `SQ2_GSL_ALLOC_TRACE=1` 私有离屏诊断，不能打入正常启动。该候选依赖当前MSM内核实际支持CACHED_COHERENT，尚无持久化安装器，不能宣称所有缓存维护或外部图像已适配。

`bash scripts/build-sq2-msm-cache-probe.sh` 构建独立内核分配探针和清单，任何一种分配失败返回非零；不提交GPU命令，不代替一致性测试。Vulkan矩阵另支持 `SQ2_VULKAN_MEMORY_BYTES=16777216 SQ2_VULKAN_MEMORY_CYCLES=16`，大小必须为4KiB整数倍、范围4KiB至16MiB。重建主探针后同步二进制与LF格式的VULKAN_PROBE_SHA256SUMS；不能用PowerShell默认CRLF清单使文件名多出回车。默认4KiB和16MiB实测结果见QUEST2-VULKAN-ONLY。

实际SF跟踪与配套编译器命名隔离见 [QUEST2-SF-TRACE.md](QUEST2-SF-TRACE.md)。`build-quest2-kmsg.sh`生成有限独立collector，SF试验需先复制到retained /tmp并匹配固定hash；不是正常boot服务。`prepare-quest2-private-compiler.py`生成combined-v4，仅用于`SQ2_COMBINED_REVISION=4`私有回归，尚不可作为桌面安装包。

FP16候选combined-v3的固定输入、私有helper隔离、build/prepare及运行入口见 [QUEST2-FP16.md](QUEST2-FP16.md)。runner使用`SQ2_COMBINED=1 SQ2_COMBINED_REVISION=3 --fp16`；v2因私有ABI冲突废弃。独立FP16验证通过，第二轮真实SF仍卡住且需手动恢复，当前不可部署为正常镜像。

真实SF临时切换/回退脚本及首轮FP16阻断证据见 [QUEST2-SURFACEFLINGER.md](QUEST2-SURFACEFLINGER.md)。`build-quest2-color-info.sh`构建allocation/metadata-only探针，用于格式1/2/5/22真实component检查；没有GPU提交，不是格式支持证明。不要重复未修正的SF失败候选。

14库合并候选及同进程双API探针见 [QUEST2-COMBINED.md](QUEST2-COMBINED.md)。WSL执行 `prepare-quest2-combined.py` 和 `build-quest2-combined.sh`，新runner设 `SQ2_COMBINED=1`、`SQ2_COMBINED_ORDER=egl-first|vulkan-first`，入口 `--combined`。它保留正常Mesa，仅在私有namespace同时绑定两套加载入口。

GLES pending fence候选：WSL执行 `python3 scripts/prepare-quest2-fence-prune.py`，需要已准备的精确zero-area候选、AArch64 GNU工具链和QEMU。输出拒绝覆盖，固定哈希，先检查实际stub指令的五类控制流。私有runner选 `SQ2_GLES_CPU_LAYOUT=4-bank15-fence-prune`；异步多层合成150轮与全屏实测见 [QUEST2-SYSTEM-GRAPHICS-PLAN.md](QUEST2-SYSTEM-GRAPHICS-PLAN.md)。这不是启动镜像安装器。

v77 Vulkan raw WSI 私有候选构建及实机复现见 [QUEST2-VULKAN-WSI.md](QUEST2-VULKAN-WSI.md)：新增 `build-quest2-vulkan-window.sh` 与 `--vulkan-window-create/--vulkan-window` runner 入口；当前验证版本选择 `SQ2_V77_AHB_VIEW=1 SQ2_V77_AHB_REVISION=4`，探针目录为 `vulkan-window/`。仍需完整同步系统 namespace helper/清单并串行执行，不是正常启动安装器。

## 环境与输入

Windows PowerShell + WSL Ubuntu 24.04（root），ARM64 GNU 交叉工具链、QEMU user/system、Python 3、Meson/Ninja、cpio/gzip、Git、Android NDK r28c。部分脚本含工作站路径及固定输出目录，迁移需显式调整。

| WSL 缓存 | 用途 |
| --- | --- |
| /root/.cache/sq2-probe-build/kernel | 内核源码及补丁 |
| /root/.cache/sq2-probe-build/kernel-android-out | 当前配置、模块、gen_init_cpio |
| /root/.cache/sq2-probe-build/googlebook | 系统及 donor 解包/挂载 |
| /root/.cache/sq2-graphics/src/mesa-26.2.4 | Mesa |
| /root/.cache/sq2-graphics/build-gles | GLES 构建 |
| /root/.cache/sq2-graphics/android-ndk-r28c | Android 工具链 |

来源见 configs/sources.lock.json、graphics-sources.lock.json、probe-inputs.sha256。操作者自行取得合法镜像/固件。

当前模块构建使用的完整配置快照保存在 configs/sq2-internal-kernel.config，包含内置 NVMe 和模块化 FUSE。它记录当前构建目录状态（含后续 PM 模块配置），不是重新构建相同 Image 哈希的充分条件；仍需匹配源码、补丁及工具链。

build-test3-ssh.sh 要求 SQ2_AUTHORIZED_KEY_FILE 指向自己的公钥；主机身份及 known_hosts 留在忽略的 private/sq2-ssh/。Windows SSH 安装脚本要求 -PublicKeyPath，离线 MSI 自行下载，见 scripts/SSH-OFFLINE-README.txt。

## 构建链

1. 恢复镜像：fetch-googlebook.py → inspect-recovery.py / extract-googlebook-stage.py，验证固定哈希。
2. 历史基础：build-probe.sh、build-test2-kernel.sh、build-android-test-kernel.sh。**最后一个是旧 USB profile，会重设配置并要求 NVMe 关闭，不能直接重建当前内置 SSD 内核。** 当前配置保留 CONFIG_BLK_DEV_NVME=y、SoC、Binder、memfd、FUSE、文件系统能力。
3. 图形：build-sq2-mesa.sh 应用 gbos-vm mapper5 与多平面补丁；build-sq2-minigbm-bridge.sh、build-googlebook-graphics-overlay.py 准备 overlay；读回测试在 probe/graphics/。
4. 触摸：build-test3-iptsd.py 应用连续性和 SQ2 热图尖峰补丁 → build-touch-fixup.sh；检查最后 cpio 成员中的有效 IPTSd/配置。热图修正仅限 045E:0C21 的 46×68 数据；不叠加已撤回的两帧起触实验。源码缓存升级时也需显式应用 patch-iptsd-sq2-heatmap.py。
5. 连接：fetch-connectivity-reference.py、fetch-wifi-hal-source.py 等准备 donor/headers；build-sq2-connectivity.sh 编译桥接/HAL；不能任意更换 donor HAL 版本。
6. 音频：build-sq2-audio-tools.sh、patch-sq2-audio.py、patch-sq2-audio-hal.py 及 policy/路由，保留和校验原始 HAL。
   当前增量还需运行 build-sq2-audio-sequencing.sh，在现有 NVMe 配置上构建静音时序模块，输出 out/audio-sequencing/ 两个模块供打包脚本使用。完整 modules modpost 校验符号；不要仅构建孤立 .ko。audio-framework-boot 同时设置 pmdown_time=0，当前有限听音结果见 HANDOFF。
7. 电源/输入：build-sq2-lid-power.sh 合并现有配置并编译模块，保留 NVMe；SPI PM 为未完成实验，用户暂停真实睡眠测试。
8. SSD：build-googlebook-internal-ssd.py 准备布局；pack-googlebook-internal-ssd.sh 合并旧基线/触摸/当前 addon。旧归档有固定输入哈希，缺少时仍需复原前置阶段；并非从源一键构建。

## 增量打包及安装

v77 Vulkan AHB适配：先运行 `bash scripts/build-quest2-v77-ahb.sh`（使用已有原件、HIDL和GLES ION构建），再运行 `python3 scripts/prepare-quest2-v77-vulkan.py --system-loader --ahb-view --adapters out/quest2-gpu/v77-ahb-adapters --output out/quest2-gpu/v77-vulkan-ahb-v3`。输出必须是不存在的新目录；当前v3已存在，不要删除旧证据强行覆盖。九库及SHA256SUMS复制到设备同名私有目录；同步新probe、namespace helper及两份对应manifest和runner。设置 `SQ2_V77_AHB_VIEW=1`（当前默认revision3）后用runner `--vulkan-properties` / `--vulkan-import`，两者不提交GPU；`--vulkan`做完整compute/raster和AHB非均匀网格读回。`SQ2_VULKAN_AHB_CYCLES=1..150`选择上述各模式的AHB轮数，`SQ2_VULKAN_AHB_WIDTH/HEIGHT`选择尺寸；测试必须串行。旧v1/v2因wrapper误要求原HAL的close回调而初始化失败，不应作为当前测试入口。原HAL close=NULL已在v3正确缓存处理；见QUEST2-V77-VULKAN与生命周期审计。仍没有正常系统安装器，raw WSI/export尚未支持。

Quest 2 v77同版Vulkan：`prepare-quest2-v77-vulkan.py --system-loader --output out/quest2-gpu/v77-vulkan-system`生成七库私有候选，固定原件哈希、mapper3/ION依赖、RUNPATH与未改变的可执行section。同步候选及SHA256SUMS，另将`build-sq2-vulkan-probe.sh`生成的探针与VULKAN_PROBE_SHA256SUMS放入私有`v77-vulkan-probe/`；最新runner的`--vulkan-offscreen`只在私有namespace绑定HAL并通过原系统libvulkan加载。`--vulkan`目前会在AHB属性检查失败，不能放宽校验继续；详细地址证据见QUEST2-V77-VULKAN。没有正常系统安装流程。

精确GLES读回：`build-quest2-readpixels.sh`生成闭源/显式Mesa两种探针与READPIXELS_SHA256SUMS，私有目录`readpixels/`。runner `--readpixels`执行414项矩阵；`SQ2_READPIXELS_WIDTH/HEIGHT`选择单尺寸，`SQ2_READPIXELS_RELAY=1`或`SQ2_READPIXELS_UPLOAD=1`另做五尺寸、两phase的精确采样/上传AHB CPU对照。`SQ2_YUV_TRACE_COPY=1`启用版本受限的进程内CPU传输跟踪，必须只用于无并发donor调用的私有探针，日志留out；不要加入正常启动。

当前最佳私有候选按顺序在WSL生成：`python3 scripts/prepare-quest2-cpu-layout.py --bank-bit 15` → `python3 scripts/prepare-quest2-pbo-copy.py` → `python3 scripts/prepare-quest2-zero-area.py`。每步固定输入哈希并拒绝已有输出；最后一步需aarch64-linux-gnu-gcc、qemu-aarch64，必须通过176组指令检查才输出。最终`gles-linked-zero-area/`和清单同步到设备同名私有目录，runner用`SQ2_GLES_CPU_LAYOUT=4-bank15-zero-area`选择。基础414+1024×128和2048×128各54项共522项全过，上传/采样各10项及YUV回归通过；仍不是正常启动安装器。PBO改走原fallback，性能未量化。`4`、`4-bank15`、`4-bank15-pbo`只用于分阶段对照；`SQ2_GLES_READBACK_POLICY=1`的独立0/0阈值实验失败，不能与CPU布局候选混用。完整证据见QUEST2-READBACK-REFERENCE。

Quest 2同版完整参考：`extract-quest2-v77-reference.py IMAGE NEW_OUT_DIR`先核对固定vendor分区哈希，再以只读debugfs逐个提取普通文件并生成inventory.json；不挂载、不创建镜像内符号链接、不操作设备。`inspect-quest2-graphics-reference.py`现在同时校验原18库与新增70项显示参考，需要先生成`out/quest2-gpu/vendor-reference-v77`。来源和接口边界见 [QUEST2-VENDOR-REFERENCE.md](QUEST2-VENDOR-REFERENCE.md)。

闭源GLES可见窗口：`bash scripts/build-quest2-visible.sh`生成独立dex JAR/JNI库/VISIBLE_SHA256SUMS；只将这三项复制到Android的`/data/local/tmp/sq2-closed/visible/`。同步最新系统探针、namespace helper、SYSTEM_PROBE_SHA256SUMS和`run-quest2-gles-system.sh`后，从retained root运行`sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-gles-system.sh --visible`。runner先复制zygote classpath，再进入私有挂载空间直接exec app_process；不能在里面调用会重新nsenter PID1的run-android-java。窗口只显示5秒，外部25秒超时，结束完整释放并退出。采样必须等到日志VISIBLE_FRAME，固定启动后2秒可能还没有图像。它验证闭源producer→现有Mesa SF，并不替换SF。

内部纹理读回诊断：`SQ2_YUV_RELAY_OUTPUT=1`增加内部纹理→线性AHB采样；`SQ2_YUV_PBO_READBACK=1`改用像素包缓冲区。这两项是定位工具，PBO小尺寸仍失败。`SQ2_YUV_LOG_READBACK_SETTINGS=1`启用版本/指令受限的只读context阈值记录；`SQ2_GLES_GPU_READBACK=1`在私有/data/vendor tmpfs写两个零阈值候选，最终字段仍为FFFFFFFF/0，没有解决直接读回。不要把这些诊断变量加入正常启动。见 [QUEST2-READBACK-REFERENCE.md](QUEST2-READBACK-REFERENCE.md)。

YUV探针：上述构建另生成 `yuv-info`（只分配并解析元数据）和 `yuv-readback`。系统清单现在包含系统RGBA探针、namespace helper、窗口探针和YUV探针四项，需完整同步。独立入口 `SQ2_YUV_LINEAR_OUTPUT=1 sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-gles-system.sh --yuv` 默认640×360 YV12三帧；`SQ2_YUV_NV12=1`请求格式35，`SQ2_YUV_FRAMES=30`进行连续创建/销毁，宽高用 SQ2_YUV_WIDTH/HEIGHT（网格要求8的倍数）。不设置 LINEAR_OUTPUT 则使用原库内部RGBA纹理，当前640×360仍失败，必须保留这一对照。`SQ2_GLES_DIRECT=1`只在私有namespace的/data/vendor临时tmpfs中写ESX候选配置，目前未改善该失败，不能当成修复或正常部署配置。

窗口交换探针：`build-quest2-gles-probe.sh` 另生成 `gles-window`，须与 `gles-readback-system`、`egl-private-namespace` 和新的三项 `SYSTEM_PROBE_SHA256SUMS` 一起同步至设备的私有 gles-v77 目录。重新生成并同步 gles-linked 候选（包含窗口代理和 mapper3 fd-backed 兼容位），不能只换探针后使用旧库。运行 `SQ2_WINDOW_ENTRY=core SQ2_WINDOW_CYCLES=100 SQ2_WINDOW_DESTROY_CURRENT=1 sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-gles-system.sh --window`；legacy入口同样可选，宽高用 SQ2_WINDOW_WIDTH/HEIGHT，默认每轮12帧。通过真实 ImageReader/BufferQueue 校验交换、fence、像素和释放，不创建可见桌面窗口。EXT入口需系统宣告 EGL_EXT_platform_base；本机未宣告，返回77并明确不计通过。所有 GPU 探针串行运行。

Quest 2 闭源 GLES 的最新系统加载验证：运行 `build-quest2-gles-probe.sh` 后，`prepare-quest2-gles-linked.py` 校验固定输入并生成新的私有 ELF 依赖组合（已有输出目录时拒绝覆盖）。`run-quest2-gles-system.sh` 配合编译的 `egl-private-namespace`，只在独立且递归 private 的 mount namespace 中覆盖 EGL 目录，通过原系统 libEGL/libGLESv2 运行 `gles-readback-system`。必须同步 SYSTEM_PROBE_SHA256SUMS，并为系统探针和 namespace helper 设置0755；不写全局 vendor 或启动镜像。具体复制路径、验证结果及缺口见 QUEST2-GPU-RESEARCH 的「EGL 1.5 与 Android 系统加载器接入」。这个流程不是桌面切换/持久化打包器。

### 独立 Turnip Vulkan 构建

后续 v77 mapper3 修复已让五种线性 RGBA AHB 尺寸通过；必须重建 `build-quest2-gles-probe.sh` 并同步 libqti-mapper3-bridge.so、gles-readback、PROBE_SHA256SUMS，不能用旧桥接跑 `--ahb`。`SQ2_GLES_WIDTH/HEIGHT` 允许 1–4096（默认 64），每尺寸三帧严格像素检查。`--ahb-cpu-readback` 单独验证真实 GPU→AHB CPU 写入，不宣称覆盖 glReadPixels；完整 `--ahb` 同时检查两条读回。`--ahb-import-only` 不调用绘制，但驱动清理可能仍提交命令。新 DRM trace 库仅供私有探针诊断，不能进正常启动；其 no-submit 模式的像素失败是预期结果，不能用作通过证明。Vulkan Direct 候选构建和实测边界见 QUEST2-VULKAN-DIRECT。

Quest 2 同固件参考可用 `rebuild-quest2-v77-vendor.py` 从已下载的 OTA prefix/vendor range 重建仅供解包的镜像；仅允许 out 下的新 .img，验证全部操作与分区哈希，不操作设备。`inspect-quest2-graphics-reference.py` 固定校验 18 个原配图形库并在 out 输出依赖/反汇编。`build-quest2-layout-reference.sh` 构建私有布局查询工具，比较原配 adreno_utils 与当前 minigbm 的真实分配，不调用 EGL 绘制。输入、结果和限制见 QUEST2-GPU-RESEARCH；这组工具不是启动镜像打包器。

完整 Quest 2 v77 EGL/GLES 是独立隔离实验：准备 `out/quest2-gpu/q2-v77/lib64` 的固定输入（`configs/quest2-gles-v77-inputs.json`），运行 `fetch-quest2-mapper-sources.py`、`generate-quest2-mapper-headers.sh`、`prepare-quest2-gles.py`、`build-quest2-gles-probe.sh`。只将生成的私有 `gles-v77` 目录复制到 Android `/data/local/tmp/sq2-closed/`；`run-quest2-gles.sh` 从 retained initramfs root 运行，默认离屏。旧桥接的 `--ahb` 曾触发 GPU hang；当前地址/锁/物理高度修复及包装层已通过文档中的线性 RGBA 矩阵，不能使用旧二进制原样重试或直接接入 SurfaceFlinger。原始 ION 库只有控制 FD 适配，分配接口仍返回 ENOTSUP；不是通用 ION→DMA-heap 转换。此增量没有正常启动打包流程，也未替换桌面驱动。完整证据见 QUEST2-GPU-RESEARCH。

Quest 2 闭源 donor 的隔离测试不使用 Turnip 打包器。完整步骤、固定来源和边界见 [QUEST2-GPU-RESEARCH.md](QUEST2-GPU-RESEARCH.md)：校验 donor → 构建进程局部 QTI/mapper5 桥接 → prepare-quest2-runtime → 固定 AdrenoTools → 独立 readback 与 APK。线性 RGBA AHB 和普通窗口呈现已通过；`--offscreen-only` 仍明确不含 AHB/WSI。闭源测试 APK 默认 1800 帧，正常 Mesa APK 仍 120 帧。第三方二进制、APK、私钥、日志不进 Git；不可把该实验桥接作为通用系统 mapper 放入正常启动镜像。

`scripts/build-sq2-turnip.sh` 在 `/root/.cache/sq2-graphics/build-turnip` 独立构建 Android/MSM Turnip，输出 `out/sq2-turnip/runtime/vulkan.freedreno.so`，不覆盖已验证的 GLES。它复用 Mesa 26.2.4 源树、mapper5/multiplane 补丁及 NDK r28c；需要 Python venv 中 Meson、mako/PyYAML、Ninja、Bison/Flex/pkg-config，下载并校验的 libdrm 2.4.133 缓存。mapper5 上游补丁固定至 gbos-vm 提交 3b08ed29a73858644c65f24e06ee2a7cfb7e3774。不是无缓存的一键下载流程。

后续 Wild Life 兼容补丁由 `patch-sq2-turnip-gmem.py` 应用，仅对 A680.1 + 3DMark 选择 sysmem，保留其它应用的 GMEM 路径；这是绕过措施，验证状态见 [WILD-LIFE.md](WILD-LIFE.md)。`pack-sq2-turnip-update.py BASE DRIVER OUTPUT --driver-sha256 HASH` 严格以麦克风基线 9dd3ff7a… 为输入，只替换 Vulkan HAL并保持凭据 addon 最后；不能用首次启用 Vulkan 的旧打包流程覆盖后续音频修复。

`scripts/build-sq2-vulkan-probe.sh` 需要 glslangValidator、patchelf，构建计算、图像及 AHardwareBuffer 读回探针；运行参数可为 HAL 路径或 `/system/lib64/libvulkan.so`。`scripts/build-sq2-vulkan-app.sh` 另需 JDK keytool、zip 及 `$SQ2_GRAPHICS_WORK/sdk/android-35/android.jar`、`sdk/android-15/{aapt2,zipalign,apksigner}`（官方 build-tools 35.0.0 归档内目录名），构建普通 NativeActivity 测试 APK。签名密钥保存在忽略的 private/sq2-vulkan；测试应用使用普通系统加载器、FIFO 交换链，画满 120 帧后停止提交。应用必须处于解锁可见状态。

`scripts/pack-sq2-vulkan.py BASE DRIVER OUTPUT --expected-base-sha256 HASH --preserve-lab-ssh` 只生成增量合并镜像及最后条目审计，不操作磁盘；本地带密码 SSH 的基线必须显式保留其最终 addon。包内仅更改 Vulkan HAL、属性及对应 feature XML，不打包工作区未部署的音频实验。先验证 gzip/hash 与审计，再按下述设备身份保护流程安装。下次启动仍保留 skiagl 桌面及 nocompression 配置；Vulkan 可由应用使用。

当前只关屏内核由 build-sq2-screen-only.sh 在已有内置 NVMe 配置上增量构建 Image，不更改配置或模块 ABI。patch-sq2-screen-only.py 添加启动后只读的系统休眠屏障；smoke-sq2-screen-only.py 用 QEMU 启动实际 Image 验证拒绝 freeze/mem/disk。GRUB 同时传入 suspend.screen_only=1 和 nohibernate。新内核需要与包含新版 lab-keep-awake 的 addon 一起部署。

pack-sq2-power-connectivity.sh 在已有缓存下打包 /init、蓝牙/输入脚本、PM 模块和 HAL；当前 /init 包含 FUSE 加载。输出是 addon，不能独立启动。

当前完整 initramfs 为已验证的图形/触摸/音频基线加 addon。不要向任意旧归档反复追加。scripts/audit-sq2-initramfs.py 扫描全部 gzip/cpio 成员，以最后条目为准；gzip -t 检查压缩。重建需核对有效 /init、libEGL、IPTSd、音频/蓝牙 HAL 和内核 ABI。

Install-GooglebookInternalSSD.ps1 是有身份保护的特定实验盘安装器，仅用于授权整盘安装。修复存储挂载/HAL 不需格式化 userdata。旧 USB/外置 SSD 脚本属于不同阶段，不可混用。

在线更新先核对盘和 ESP，备份原 initramfs，写同 ESP 临时文件，检查 hash/gzip，sync 后原子替换，保留正常启动、独立救援和关机项。正常入口在 Android 启动后自动开启公钥 SSH，不执行诊断 Wi-Fi 连接；sq2.ssh=0 可单次禁用。需要完整启动日志或救援联网时使用救援或 sq2.debug=1。

## 验证

### 内置麦克风

见 [MICROPHONE.md](MICROPHONE.md)。当前采用 `patch-sq2-microphone-hal.py` 从 stock donor 生成仅修正输入设备地址的 HAL，不能使用旧播放 pacing/MonoPipe 实验替代。`build-sq2-mic-app.sh` 生成标准 AudioRecord 录音/回放 APK；`pack-sq2-microphone.py` 以 d519b958… 资源监控镜像为唯一基线，逐项限定四项音频变化并保持凭据 addon 最后。一次性启动通过后已提为默认 9dd3ff7a…。48 kHz stereo、16 kHz mono 及原诊断应用、重启回归由用户确认。

### 资源监控器：CPU 温度与 Surface 电池

`configs/sq2-thermal-info.json` 为 Pixel Thermal HAL 映射 SQ2 的八个 CPU 核心传感器，虚拟 CPU 取最大值，毫摄氏度乘 0.001 转为 °C；GPU-top 单独发布。配置只提供读数，不创建 HAL cooling requests，保留内核 CPU/GPU 温控。`resource-monitor-start` 在 Android init 前将配置绑定到 vendor/etc/thermal_info_config.json。

`scripts/build-sq2-battery-compat.sh` 依赖 TSENS 流程已准备的匹配源码/配置，构建独立只读模块 sq2_battery_compat.ko。它为已有 BAT1 添加 charge_full、charge_full_design、charge_now、charge_counter、current_now、state_of_health sysfs 属性，保留原 Surface 驱动、电源事件和充电控制。容量用 energy×1000000/voltage_min_design 转为估算 µAh；电流用 power×1000000/voltage_now 转为 µA，放电取负；健康度取满充能量/设计能量的百分比。没有读到的温度/制造日期/故障健康状态不伪造。Android desktop HAL 使用 charge_now，故仅有 charge_counter 不足以修复容量。

构建环境缺原始 Module.symvers，使用 KBUILD_MODPOST_WARN=1，不声称完整 modpost 通过；加载前必须校验 vermagic 及所有未定义符号的运行内核 __ksymtab 导出。本次 8 个符号匹配且实机加载成功。`scripts/pack-sq2-resource-monitor.py BASE OUTPUT` 限定 e16a7a01… CPU 适配基线，只添加上述配置/模块/助手并插入 /init 钩子，保持凭据 addon 最后。`sq2.monitor=0` 跳过此适配；救援入口不会执行。先进行在线 HAL 和真实充电器拔插验证，再测试一次性启动项并设为默认，不打包未验证的音频工作区改动。

### SQ2 CPU 调频增量

`scripts/build-sq2-cpu-opp.py BASE.dtb OUTPUT.dtb` 以已验证的 661 MHz/TSENS DTB（09b6d3a8…）为基础，为大核补上硬件 LUT 已有的 3072/3148.8 MHz OPP。继承原 2995.2 MHz 档位的 interconnect vote；电压由匹配的 qcom-cpufreq-hw 驱动读取本机硬件 LUT，不填写推测电压。两个 CPU cluster 各增加 80°C 被动降频、5°C 回差，绑定对应 cpufreq cooling device；原有 CPU/GPU 属性和内存保留区逐项不变。

`scripts/pack-sq2-cpufreq.py BASE HELPER OUTPUT` 从 f8fb8335… 已部署镜像只替换 /init、添加 bin/cpu-frequency-start，并确保本地密码 SSH addon 最后。启动助手加载原镜像已有的匹配 qcom-cpufreq-hw.ko，使用默认 schedutil，不固定频率；`sq2.cpufreq=0` 或救援入口跳过。不要为了此更新重新打包工作区未验证的音频实验。

实机先用独立 initramfs/DTB 和一次性 GRUB 项启动，保留原正常入口作为下一次启动回退。`probe/graphics/cpu-frequency-check.c` 用 aarch64-linux-gnu-gcc -O2 -static 编译到目标 /tmp/cpu-frequency-check；`scripts/test-sq2-cpu-frequency.sh` 将八个线程分别绑核，运行 20 秒可校验整数递推，采集调频/温度，85°C 中止并清理负载，不修改 governor/min/max。`scripts/test-sq2-cpu-cooling.sh` 分别模拟两个 cluster 的 81°C，检查降频状态及移除模拟后的恢复；退出始终清除模拟温度。驱动读回频率是请求档位，不是独立测量核心时钟。

### SQ2 GPU OPP / TSENS 增量

`scripts/build-sq2-gpu-opp.py BASE.dtb OUTPUT.dtb --max-mhz 590|661 --thermal-guard` 使用 WSL 的 python3-libfdt，严格限定原 DTB 哈希 7a4d1c73…，添加 590/661 MHz 的 TURBO_L1 档位。`--thermal-guard` 为 GPU-top 增加 75°C 被动降频及 5°C 回差，绑定既有 MSM devfreq cooling。它审计全树属性和内存保留项，不替换 SC8180X/A680.1 身份、GMU 固件和原低频表。参数来源及证据边界见 configs/sq2-gpu-opp.json。

`scripts/build-sq2-tsens.sh` 使用固定 linux-surface b86f786… 源码归档（SHA256 275f05c5…）和保存的完整内核配置，仅构建匹配的 qcom_tsens.ko。Kconfig olddefconfig 后配置必须逐字相同；无需替换已验证的 Image/其他模块。原构建缓存缺失，因此 modules_prepare 后没有完整 Module.symvers，脚本明确使用 KBUILD_MODPOST_WARN=1：**此构建不是完整 modpost 校验**。部署前必须比对目标 vermagic 和全部未定义符号对应的运行内核 __ksymtab 导出；本次 52 个符号全部匹配，并成功实机 insmod。CONFIG_MODVERSIONS 关闭，不可把此流程用于其他内核版本。

`scripts/pack-sq2-tsens.py BASE MODULE OUTPUT --expected-base-sha256 HASH` 从经校验的镜像只新增匹配 TSENS 模块及 /init 的早期加载钩子，保留密码 SSH addon 最后。高频配置加载 TSENS 失败时停止启动；原 514 MHz DTB 可回退。Vulkan 打包器已补齐 permissions 父目录条目，修复文件存在于 cpio 审计却未被内核解包的问题；回归测试为 tests/test_pack_sq2_vulkan.py。

实机负载工具 `scripts/test-sq2-gpu-opp.sh HZ` 在保留的 initramfs root 运行，需新版 vulkan-readback 及有效 GPU 温度节点。它临时约束 min/max，执行 30 秒 4096 字×4096 次整数递推并逐字校验，再做图像/AHB 读回；80°C 停止，退出恢复原调频上下限。cur_freq 为驱动报告值，不能称为直接测量 PLL。正式部署前需一次性实验启动、降频规则验证及界面回归；测试菜单使用独立 DTB，原正常/救援 DTB 保持可用。

本机调试密码 SSH：build-sq2-password-ssh.sh 使用 Dropbear 2026.94 和本地缓存 libxcrypt-4.4.38.tar.xz（上游 besser82/libxcrypt release）交叉构建支持 crypt 的静态服务端，不重新生成主机密钥。pack-sq2-password-ssh.sh 要求 SQ2_SSH_SHADOW_FILE 指向操作者自己的本地 shadow 文件。生成的 out/password-ssh/addon.cpio.gz 含凭据，禁止发布。当前设备在已验证完整镜像后追加该 addon；以后重打基础/音频 addon 时也要将密码 SSH addon 放到最后，否则会退回旧服务端/账户配置。公开基础构建仍只支持公钥。

```sh
python3 -m unittest discover -s tests -v
python3 -m compileall -q scripts tests
bash -n scripts/pack-sq2-power-connectivity.sh
sh -n probe/test2/init-googlebook-native
python3 scripts/test-iptsd-sq2-touch.py /root/.cache/sq2-probe-build/iptsd-3.1.1
python3 scripts/test-iptsd-sq2-heatmap.py /root/.cache/sq2-probe-build/iptsd-3.1.1
```

PowerShell 脚本通过 parser 检查；设备验证另见交接文档，不把语法/QEMU 检查等同于硬件通过。

C2早期声明候选：probe/test2/video-codec-manifest-stage尚未接入/init或镜像。仅sq2.video.c2=experimental启用，rescue优先退出，要求screen_only=Y、RAM vendor overlay及目标不存在；用临时文件原子安装default1 AIDLv2声明。测试python3 -m unittest discover -s tests -p test_sq2_c2_manifest_stage.py -v运行实际helper（只替换路径至临时目录），覆盖默认/关闭/rescue、有效XML、guard/overlay失败、拒绝覆盖。**不得单独部署声明：应配套可启动的系统Binder服务，再做一次性启动测试，避免框架发现声明后等待不存在的服务。** 此候选不声称MediaCodec可用，不自动启固件、不改正常入口。

上述早期helper现已扩展为声明、service二进制、configs/sq2-c2-experimental.rc成套安装；先核验/etc/sq2-c2-experimental/SHA256SUMS及目标不存在，再写RAM overlay，声明最后原子发布。实验rc使用已知kernel域/root（仅实验室），class hal/oneshot、系统优先LD_LIBRARY_PATH、正常--serve，不使用--serve-local。服务失败不无限重启。私有bundle在out/video-decode/codec2-integration/boot-service，service SHA4e7d98a7…与设备原候选一致。测试fixture补齐bundle/目录，三组测试通过；尚未打包部署或验证init启动/SELinux/MediaCodec。现机getenforce实际Enforcing，尽管cmdline含permissive/enforcing=0；不可仅凭启动参数推断策略状态。正式部署前须验证实验域在init下可用，及新启动后恢复原生Venus后端的可复现步骤。

已从当前a101设备/tmp/sq2-eos-a101取回15文件到out/video-decode/recovered-native，原SHA256SUMS逐文件全通过。新增video-decoder-lab-start从其真实加载顺序提取：仅experimental参数启用，要求Android完成/guardY/固定内核及固件，拒绝resident核心/provider，mkdir串行化并在overlay触发probe前标记本boot已尝试PAS；不热卸载，不运行测试向量，不持续日志。新助手仅语法检查，尚未接入/init或部署；须与独立有界collector及一次性启动回退一起测试，不能替代已通过的run-eos.sh证据。启动包需要包含完整已核验native目录；C2服务注册不依赖/dev/video0，但实际配置/解码须等待后端ready。

完整C2实验镜像：scripts/pack-sq2-c2-experiment.py BASE SERVICE_BUNDLE NATIVE_BUNDLE OUTPUT固定当前默认5ba59bd9…，校验已恢复服务4e7d98a7…及native SHA256SUMS；仅增加实验文件与早期init钩子，原有文件除init外字节保持，密码SSH addon最后。当前out/video-decode/boot-candidate/experimental.cpio.gz SHA256 a503842819c41206b6018cf34cd00e7b17b138f56e7c5b78db42c5ab9a5e0e9a，gzip及最终条目审计通过，未部署/启动。sq2.video.c2=experimental才发布服务/声明；后端刻意不自动启动，开机后以独立有界collector调用video-decoder-lab-start，每boot仅一次PAS。接下来需验证设备身份/ESP空间、备份菜单和设置一次性回退，再测试真实init注册/发现；仍非MediaCodec可用完成。

NDK MediaCodec帧验证：sq2-mediacodec-frames.c需链接libmediandk及设备system/lib64/libbinder_ndk.so并启动Binder线程池。测试向量头在私有out/video-decode/ndk-vector；使用原SHA94c7974e…码流，不将恢复的二进制提交。参考生成：ffmpeg -nostdin -v error -i clip.h264 -pix_fmt nv12 -f rawvideo ref.nv12。取回明确来自同轮日志的六个sq2-ndk-frame-N.bin，运行python3 scripts/verify-sq2-mediacodec-frames.py FRAME_DIR REF_NV12；仅适用日志确认320x240/stride320/slice240/color19的六帧I420。不能用旧残留文件代替失败轮输出。2026-10-29硬件和软件实际比较通过；独立--csd后续试验失联，恢复取证前不要重跑。

普通APK视频测试：WSL bash scripts/build-sq2-video-app.sh，生成out/video-decode/player-app/sq2-video-test.apk及10秒300帧AVC baseline asset。签名材料仅private/sq2-video。默认显式选择实验codec，Intent字符串codec可选c2.android.avc.decoder作对照；--user取am get-current-user。test-c2-player.sh需实验服务/native已就绪且专用媒体XML不存在，临时广告/刷新媒体列表、播放/截图/检查回调后回退。应用按PTS限render-ahead，不能据其通过宣称组件bufferpool背压无缺陷。APK保持屏幕亮仅在测试Activity存活期间，runner结束force-stop撤销。
