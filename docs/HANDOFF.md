# SQ2 开发交接

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
