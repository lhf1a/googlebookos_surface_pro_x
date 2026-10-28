# Mesa EGL/GLES + 闭源 Vulkan

## 范围

### 继续调试：sRGB 准入与手动回退（2026-10-28）

拉取 `cd6a5ee` 后在应用启动前连续采集日志，确认 Wild Life 的首个阻断为 wrapper 的 `eligible=0`，随后 `vkCreateSwapchainKHR=-1000072003`。新增 `--image-info` / 诊断版本4记录到：二维、format=43（R8G8B8A8_SRGB）、单层/单mip/单采样、2880×1920×1、flags=0、usage=0x10。原适配只接受 UNORM，不能将这次拒绝归因为 donor 不支持。

`--native-srgb` / 诊断版本5仅在 `SQ2_VK_NATIVE_SRGB` 构建开关下接受同布局的 RGBA8 sRGB；保留原始 VkImage 格式和全部 native handle、metadata、owner校验。没有将 sRGB 偷换为 UNORM。wrapper SHA256 `db92112e775733180c8e803d54f4a12659911f7927c6d721c70395cc8d83d885`。

窗口探针 `SQ2_VULKAN_WINDOW_SRGB=1` 请求格式43，RGB以线性0.25清除，CPU核对sRGB编码137（容差1），alpha保持线性精确检查。12帧、每帧4096像素全过，present/fence及最终native引用归零，默认Mesa/boot/framework保持。格式语义参考 [Khronos格式定义](https://docs.vulkan.org/spec/latest/chapters/formats.html)。

版本5系统测试已越过交换链初始化并跑完 Wild Life，显示6455 / 38.65 FPS，但19.3秒采样严重黑白花屏，因此分数不能作为有效性能结果，系统替换仍未完成。sRGB准入和复杂渲染正确性是两项独立验证。

随后使用同一版本5九库，在已重定向的私有 `/data/sq2vk-v77t/qgl_config.txt` 放入同版已分析的 `RenderMode=Direct` 配置（SHA256 `1f93a7a1…`），重开3DMark。Wild Life完成 **5876 / 35.19 FPS**，25秒采样场景正常，用户明确确认“画面正常”。没有修改全局GPU配置路径，没有继承旧v819.1的跑分。本轮未取得配置文件系统调用读取跟踪或逐renderpass模式记录，因此结论限定为同一候选加此配置后的实测改善，GMEM根因仍未关闭。测试后手动回退Mesa并移走本轮临时配置，boot ID和SurfaceFlinger PID保持；未写启动镜像。下一步为更多窗口格式/生命周期及Chrome失败管线、无诊断构建与持久化验证。

按用户要求，Vulkan-only测试支持 `--start-manual`：不启动120秒watchdog，打印 `ROLLBACK_COMMAND`，操作者通过该状态目录中的脚本执行回退。默认 `--start` 仍保留120秒机制。两种模式均只临时绑定Vulkan入口，保留Mesa EGL/GLES、不写启动镜像；手动回退仍重启zygote清除应用映射，SurfaceFlinger保持原进程。版本4手动回退已实测恢复两个Mesa哈希，boot ID不变。

后续兼容矩阵应覆盖UNORM/sRGB、BGRA、FP16、不同image用途/flags、同步与生命周期；未知私有布局继续拒绝，不能以直接移除所有准入条件替代适配。

按用户最新要求，闭源 EGL/GLES 的代码、上传跟踪和失败证据保留在分支；正常桌面和 GLES 继续使用 Mesa。当前只推进系统 Vulkan 后端替换。不要再用 combined 桌面试验覆盖 EGL/GLES。

当前状态：**普通Vulkan APK窗口已修复，Wild Life 在 sRGB + Direct 候选上完成且用户目视正常；Chrome历史管线错误和更多外部格式仍待处理。未写启动镜像、未提升默认。**

### 运行候选与 AHB 分配/导出（2026-10-28）

`build-quest2-vulkan-runtime.py` 从固定 counter-v1 九库构建，不启用 APP_DIAGNOSTICS/PIPELINE_DIAGNOSTICS，不捕获用户着色器；原 wrapper 的 stderr 生命周期统计仍存在，不能称为完全无日志版本。保留独立编译器库、process_vm_readv、sRGB 准入和私有 Direct 配置。原始库与链接用平台库留在忽略目录。

- runtime-v1 的四队列150轮和sRGB窗口12帧通过。但最初“导入后导出”探针未在分配时包含 VkExportMemoryAllocateInfo，违反 VUID 01882；其10轮结果只保留为历史引用存活观察，不作为标准导出通过证据。
- runtime-v2 修正为原始完整 pNext 链包含 AHB export 位，且先检查 external image properties 的 EXPORTABLE / exportFromImportedHandleTypes。修正后的10轮 GPU网格/CPU读回、双导出引用及释放原对象后查询全过。
- `--ahb-allocate` 生成 runtime-v3，增加由 donor 新建真实平台 AHB 的路径。allocate 成功的初始引用转交 donor owner，prepare 的独立保活引用随 view 保留；最终 donor release 回收，与导入 acquire 路径分别计数。格式暂限单层、非保护、非mipmap RGBA8，全部实际metadata/FD边界校验保留。
- 新建导出探针使用 dedicated image + export AHB 位 + allocationSize=0，bind 后导出两份应用引用。GPU clear→staging buffer→CPU读回，不对未声明CPU用途的AHB进行CPU lock。64×64共16轮覆盖全部RGBA零/一组合，257×129和2880×1920各3轮全过；每轮两份引用在原image/memory释放后仍可查询，最后view/owner/lease归零。这不是全格式、跨进程或完整CTS结果。
- runtime-v3 原有导入/导出网格16轮、UNORM和sRGB窗口各12帧、四队列创建销毁150轮均通过。boot ID、SF/system_server PID和默认Mesa哈希在私有测试前后保持。

runtime-v3 wrapper SHA256 `69b710019872e1d622e797a35f8d4f636e9a0439028d0e6815ac0c70fa389b7f`，AHB adapter `050cadeb3958c4cd5ca148285186e3a23fa6ad5e59e35ca0faf56f246fc4bd91`。runtime-v2 wrapper `2251aea32ca0148095aaa20449aa9244ad415c14c65ec9350a60890e32367aff`。

私有 runner 选择 `SQ2_VK_RUNTIME=2|3`，在独立 namespace 绑定候选 Direct 配置。AHB探针用 `SQ2_VULKAN_AHB_REEXPORT=1` 检查导入再导出，或 `SQ2_VULKAN_AHB_NEW_EXPORT=1` 检查新建导出；`SQ2_VULKAN_AHB_CYCLES` 控制轮次。两种模式分别执行。runtime-v3 系统临时试验用 `SQ2_VK_RUNTIME=3 ... --start-manual`，回退同时撤销本轮 Direct 配置挂载；不能将其视为安装器。

规范依据：[AHB导出有效用法](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryGetAndroidHardwareBufferInfoANDROID.html)、[内存分配](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryAllocateInfo.html)。尚未完成：更多格式/外部句柄、Chrome错误、准确系统feature XML、持久化和重启。实际 Vulkan 版本为1.1.295，不能沿用Mesa的1.3能力声明或宣称CTS认证。

### runtime-v3 普通应用与缓存分配缺口

普通APK通过标准系统loader呈现2880×1920，完成120帧。Chrome GPU进程实际映射本轮HAL、GSL及私有QGL，测试网页可显示；本轮有限日志未再观察到先前的graphics pipeline链接错误，但出现多次 `cnvt_alloc_gsl_flags` 拒绝缓存请求，GSL原始flags为 `0x0c0c2600`。不能据网页可见或这次未出现链接日志宣布Chrome全部通过。临时系统试验已手动回退，两个Mesa哈希恢复，Direct目录原状态恢复为空，boot ID/SF PID保持；回退按设计重启zygote。

新增 `SQ2_VULKAN_MEMORY_TYPES=1` 探针：枚举真实memory type，对buffer兼容类型做GPU填充/复制读回，HOST_VISIBLE类型另测CPU写→GPU读、GPU写→CPU读，非一致性类型按规范flush/invalidate。对该buffer不兼容的类型只做独立合法分配并明确 `gpu_coverage=0`，不非法绑定。

| type | properties | 4KB分配 | GPU读写 |
| --- | --- | --- | --- |
| 0 | 0x1 DEVICE_LOCAL | 通过 | 通过 |
| 1 | 0x1 DEVICE_LOCAL | 通过 | 此buffer不兼容，未测 |
| 2 | 0xb DEVICE_LOCAL / HOST_VISIBLE / HOST_CACHED | **-3 INITIALIZATION_FAILED** | 未测 |
| 3 | 0x7 DEVICE_LOCAL / HOST_VISIBLE / HOST_COHERENT | 通过 | 此buffer不兼容，未测 |
| 4 | 0xf 上述三者加HOST_CACHED | 通过 | 通过 |
| 5 | 0x11 DEVICE_LOCAL / LAZILY_ALLOCATED | 通过 | 此buffer不兼容，未测 |

矩阵以失败退出1，默认Mesa/boot/框架保持，不能把5/6称为全部通过。缓存错误和type2分配失败的关联仍需调用参数跟踪确认。

固定GSL `f16bc42e…` 离线审计：`ioctl_drm_sharedmem_alloc` 入口VA `0x20f898`，在 `0x20f8fc` 提取mask `0x0c000000`；原分支支持0/0x08000000→MSM_BO_WC、0x04000000→MSM_BO_UNCACHED，0x0c000000走 `0x20fbc4` 的拒绝日志。此处是MSM DRM分配，外层错误字符串的 `kgsl_sharedmem_alloc` 不代表实际走KGSL。`ioctl_drm_sharedmem_cacheoperation_bulk` 在 `0x210298` 直接返回-6，单项 `0x2102b8` 仅特定flags返回0，未见实际缓存维护。**下一步必须核对真实Vulkan分配到GSL的flags和当前内核的缓存机制；不可只改拒绝分支或把cached标记当作已支持，也不应直接改memoryTypeIndex绕过。**

### runtime-v5 缓存分配适配（2026-10-29）

有界GSL跟踪在真实type2分配上取得 `flags=0x0c0c2600`、GSL返回-5、Vulkan返回-3，与Chrome原缓存错误一致。跟踪只在私有离屏进程启用，按固定路径、入口指令和唯一现有GSL映射解析原函数。最初通过system namespace额外dlopen产生未初始化的第二份GSL并导致诊断进程SIGSEGV，已改正；该失败不作为应用或内核崩溃证据，追踪库不进入运行候选。

`msm-cache-modes.c` 实测当前内核MSM 1.10.0接受WC、CACHED、CACHED_COHERENT三种4KB分配及释放。这只是分配能力验证，不单独证明GPU缓存一致性。固定GSL只在先前拒绝的12字节分支中改为MSM_BO_CACHED_COHERENT（0x80000），保留其它缓存模式、实际ioctl错误与释放路径。实际缓存选择指令经512组QEMU检查覆盖全部四种cache mask。

runtime-v4只修正原拒绝分支。runtime-v5另解决type4虽公布HOST_CACHED但GSL默认零cache mask仍映射WC的问题：在创建device时读取真实memory properties，为vkAllocateMemory建立线程局部缓存作用域；私有转发只将该作用域中的默认cache mask改为0x0c000000。显式其它模式、AHB导入/导出、公开type/index、请求大小和返回值保持；退出恢复原作用域。原donor只重命名一个等长分配函数导入，配套adapter解析同一vendor namespace的GSL。

此实现依赖**真实硬件一致性缓存**提供CPU/GPU可见性；没有实现旧GSL缺失的软件flush路径，也没有把cache API错误强改为成功。type2公开属性仍保守地不声明COHERENT；其实际分配使用更强的一致性后端。不得把该方式推广至未经验证的其它内核/硬件。

runtime-v5实测：

- 六类内存合法分配全部通过；buffer兼容的type0/4各150轮4KiB、另各16轮16MiB，每轮改变数据并经fence同步，type4覆盖CPU写→GPU读和GPU写→CPU读。type2/3只有CPU映射、flush/invalidate调用和自身数据检查，明确无GPU绑定覆盖。大缓冲区探针独立分配匹配尺寸的HOST_COHERENT读回缓冲区，逐字核对全部数据。
- AHB导入再导出16轮、新建导出16轮、sRGB窗口12帧通过，保留真实metadata、引用和释放校验。
- 标准系统加载器下普通APK完成120帧；Chrome实际GPU进程映射本轮库，网页正常，本次有限日志未发现原缓存分配或管线错误。此项不清除历史全部Chrome兼容性待办。
- Wild Life资源安装完成后，runtime-v5取得 **5813分 / 34.81 FPS**，7.1秒和35.3秒场景采样正常，用户明确确认“这一轮画面正常”。应用实际映射本轮Vulkan/GSL/私有编译器，Mesa EGL哈希保持；这是新的独立结果，不沿用诊断版本5的5876分。单次成绩不能归因为缓存补丁带来性能变化，也非全程逐帧验证。
- 结束后的 `AttanWorkloadActivity.parseWorkloadResult` 工作线程记录数组越界（Index 0 / length 0），同一PID随后进入成绩页并显示上述结果；另有缺少raw monitoring data / COMPONENT_TEST-level result日志。原因尚未确定，不能直接归因为GPU、传感器或缓存补丁；保留为结果解析兼容性待查项，不能宣称整轮日志零异常。
- 已读取该轮应用保存的结果包：raw_result.json 的 statusCode 为 SUCCESS，Result.xml记录5813及34.81，与截图一致。包中有Monitoring.csv，但日志提示缺少其要求的raw monitoring data，二者不能直接等同。该核对支持测试确实完成，不消除解析线程异常。
- 完成后手动回退，busy挂载按既有流程lazy卸载；两个Mesa哈希恢复，Direct目录恢复为空，boot ID/SF PID保持。zygote/system_server按计划刷新，boot_completed=1，screen_only=Y。随后大缓冲区私有测试也保持Mesa/boot/框架。未写启动镜像。

runtime-v5固定哈希：GSL `b302ae0d5a6041be816ebe07c0c8c2686baf231b54182ee319731512db35523d`；AHB adapter `3f758905767cb150aea77dee68c300b55c39701394edbf9a899ad770511ba3f2`；wrapper `4cfe5cba0496e9b007344a53725aa2595cdfa0d777f880f9d468a065b36c78a0`；donor `c6253f16ae790a5f845c563e2705e36a005aecf327ed312f7de4ddcbd0e60dce`。其它六项及Direct配置延续v3。构建入口 `prepare-quest2-cached-memory.py --scoped`；完整原始证据留在忽略目录。

## runtime-v7：更多颜色格式（2026-10-29）

在固定 runtime-v5 上，`build-quest2-vulkan-formats.py --bgra-export` 生成九库 runtime-v7；不带参数的 runtime-v6 是中间诊断候选，其 BGRA 新建导出失败，不应部署。两版本保留真实 mapper5 分量、fourcc、线性布局、步长、FD 大小和引用校验，新增 RGBX8/BGRA8/FP16，按实际每像素 4/8 字节换算，不把 FP16 当成 RGBA8。无颜色开关的源码重建仍精确复现 v5 adapter/wrapper 哈希；最新源码也精确复现 v7。

v77 的 BGRA 导出分配描述实际给出 format=0，v6 因而拒绝。v7 从同设备、已记录的 dedicated image 取得原始格式及尺寸，以线程局部作用域传给 adapter；仅固定 donor 调用点、格式为0且尺寸匹配的 BGRA 导出转换为 Android format5。保留原 VkImage 格式，恢复嵌套调用前的作用域，不更改导入。逐字节读回验证了红蓝顺序，不能简单把 BGRA 宣布成 RGBA。

私有 namespace 中的 v7 实测（所有通过项均有最终摘要、退出0及默认 Mesa/boot/框架不变）：

| 路径 | 格式与尺寸 | 结果 |
| --- | --- | --- |
| 新建 AHB 导出 | RGBA8 64²16轮；BGRA8 64²16轮、257×129及2880×1920各3轮 | GPU读回零错误，释放Vk对象后两个导出引用仍有效 |
| 新建 AHB 导出 | FP16 64²、257×129、2880×1920各20轮 | 逐16位检查；除0/1外还覆盖0.125、-0.5、2.0、0.75 |
| 平台 AHB 导入、再导出 | RGBA8/BGRA8/RGBX8各64²16轮；FP16 64²20轮 | GPU staging和CPU AHB锁定读回均零错误；释放Vk对象及原引用后两个导出引用有效 |
| 平台 AHB 导入、再导出 | BGRA8 257×129及2880×1920各3轮；RGBX8 2880×1920三轮；FP16 257×129二十轮及2880×1920三轮 | 两侧读回通过；RGBX仅检查RGB，不检查未定义的X/alpha |
| 原有回归 | RGBA导入再导出16轮、UNORM/sRGB窗口各12帧、四队列150轮 | 通过 |

全屏 FP16 **导入**最初请求20轮，第13轮完成、14轮进行中触发既有25秒限时，退出143；不能记为20轮通过。保留超时证据，独立缩短为3轮后完整通过。全屏20轮**新建导出**是另一条已完成路径，不混用结果。FP16负值/超1检查在非对齐20轮导入中也通过。

v7固定哈希：adapter `7af3af6965eb9b85fffc4ad54995b2d3bafb6efe7e38197b4b2b1f45a4feefd8`；wrapper `2c6c1d0ed676c13ebb8aae47c9020a9b66fcd25faca8752064082cf56e587fa1`。其余库和Direct配置与v5逐字一致。原始日志/二进制仍在忽略目录。

私有复现选择 `SQ2_VK_RUNTIME=7`；新建导出使用 `SQ2_VULKAN_AHB_NEW_EXPORT=1 SQ2_VULKAN_EXPORT_FORMAT=rgba8|bgra8|fp16`，平台导入另加 `SQ2_VULKAN_COLOR_IMPORT=1`，该模式还接受 `rgbx8`。用 `SQ2_VULKAN_AHB_WIDTH/HEIGHT/CYCLES` 指定尺寸/轮次，runner入口为 `--ahb`。FP1620轮最后四轮覆盖非0/1值；全屏导入宜3轮以保持既有超时边界。

新增格式的真实交换链仍未验证，只有原RGBA UNORM/sRGB窗口回归；YUV、depth、BLOB、RGB565/10bit、其它外部句柄和能力查询与实际支持的一致性仍待补足。不是CTS通过证明，不能宣称所有主流请求已兼容。

临时系统runner新增v7固定哈希准入，同时临时覆盖旧Mesa1.3版本XML为1.1，并重启zygote让PackageManager重读；回退恢复原XML及Mesa后同样刷新框架。不会修改启动镜像，不公布未经验证的dEQP日期。版本编码4198400来自[AOSP 1.1声明](https://android.googlesource.com/platform/frameworks/native/+/7b6a56ef2a/data/etc/android.hardware.vulkan.version-1_1.xml)，准确声明要求见[AOSP Vulkan实现说明](https://source.android.com/docs/core/graphics/implement-vulkan)。完整系统兼容性仍须另行验证。

v7临时系统接入实测：PackageManager已报告4198400，普通APK识别Adreno 680 / 1.1.295，在2880×1920完成120帧，截图正常；Chrome实际GPU进程的ANGLE日志明确报告同一Vulkan版本，maps匹配本轮九库，示例网页显示正常。本次有限采集未见原缓存分配失败和shader链接错误；仍有未知features2结构、未实现性能计数器和内核Landlock等日志，不能宣称日志零异常或全部网页路径通过。采集还包含init子进程与Trusty/GSC服务异常，boot ID与SurfaceFlinger PID保持，不能把进程名init误当PID1崩溃或board重启。该轮未重跑Wild Life，不沿用v5成绩证明v7通过同一负载。已手动回退Mesa及原版本XML，Direct目录恢复为空、锁释放；框架刷新期间SSH短暂连接超时后恢复，screen_only=Y，未写启动镜像。

## 构建与私有复现

### runtime-v8 启动集成候选（2026-10-29）

v8仅在v7上关闭成功调用统计和成功BGRA导出提示，不捕获着色器、不添加常驻日志。错误与不变量检查保留。wrapper `4dee0a10a128833d1b18909254c440dcda218ab529bd4b6dc110f637467cfae9`，adapter `c1c8f883d9de61392c9d1784028622f01692e37cc215e1114abdc3727516e7fd`，其余库与v7相同。私有回归四队列150轮、sRGB窗口12帧、BGRA新建导出64²16轮、FP16新建导出257×129二十轮通过，默认Mesa/boot/框架保持。

独立启动包SHA256为 `5ba59bd91c14208442a15792dedff236b0f1789c4d8fe10d00ee50f5e0a4953c`，基线为当前已验证的fa64305d…镜像。最后条目审计只允许新增候选目录/助手及修改init钩子；原所有vendor文件、音频、IPTSd、CPU/GPU温控和凭据未变，密码SSH addon仍最后。gzip检查与三个离线助手集成测试通过。候选通过标准HAL路径供系统应用加载，版本声明1.1，Mesa EGL/GLES仍保留；无需逐应用注入。`sq2.vulkan=mesa`提供同镜像跳过路径，独立原正常/救援镜像另保留。

已核对实验设备BC711 512GB、磁盘大小、ESP/数据UUID、ESP标记和system_a根挂载，原Image/DTB/镜像哈希匹配。独立候选写入ESP新文件，gzip/hash验证及sync后原子改名；GRUB配置已备份。一次性标志只有save_env成功才选择候选；菜单通过grub-script-check。

真实重启后boot ID改变，公钥SSH恢复；启动助手READY，系统HAL与Direct配置哈希匹配，PackageManager报告4198400，Mesa EGL哈希保持。IPTSd `e4610813…`、麦克风HAL `774cd7e7…`、GPU661MHz/CPU3.1488GHz、screen_only=Y及两个30分钟屏幕设置保持，用户确认桌面/触摸正常。计算16轮、optimal图像与光栅8帧读回通过；普通APK报告120帧，全屏三角形截图正常。

重启后的Wild Life取得 **5859分 / 35.09 FPS**，40秒采样正常，用户明确确认“画面正常”；实际maps为本轮Vulkan/GSL/私有编译器。结果包raw_result.json为SUCCESS，Result.xml分数/FPS与页面一致。仍有完成后的parseWorkloadResult数组越界工作线程异常，同一PID进入成绩页；与v5相同待查项，不宣称零异常。相同已测试镜像已设为默认，菜单读回复核通过；原fa64305d…镜像保留救援和图形Mesa回退入口，同镜像也有Mesa opt-out入口。没有再执行第二次默认菜单重启，也未实机启动opt-out；完整标准一致性和更多格式仍开放，非CTS认证。私有库、带凭据镜像与部署记录留在out，不提交Git。

### 本次 system ANR 的独立诊断

01:24:14事件明确为PID2257/system_server的 `No response to onStopJob for @SyncManager@com.google.android.gms.reminders/com.google:android`，实际超时8000ms。此前同步Job44已重试144次，报numIoExceptions=1；不能单凭此断言具体网络/认证故障。抓栈时main线程Runnable，调用链为DeviceConfigListener → AppCompatOverridesService.applyAllOverrides → PlatformCompat.removeAllOverridesOnReleaseBuilds → CompatConfig.removeAllPackageOverrides → PackageManager.getApplicationInfo；SyncManager线程处于正常poll等待。该证据支持框架主线程处理配置更新期间未及时回应同步任务，但单次栈不证明它占据了整个8秒窗口。

设备保存的前一天22:03 ANR也为Google日历同步onStopJob超时8000ms。当前boot ID、system_server和SurfaceFlinger PID均保持；这次不是整板重启，也没有从该栈取得Vulkan等待证据。没有通过关闭同步、清数据或隐藏ANR绕过问题。下一步如需修复，应补充主线程时间线/多次栈和AppCompat配置更新来源，再区分长计算、事件堆积或调度延迟。原始日志留在out，不发布账号内容。

在已有固定 combined-v5 输入及 NDK/交叉工具链的 WSL 环境中依次执行：

```sh
python3 scripts/prepare-quest2-vulkan-only.py
python3 scripts/prepare-quest2-vulkan-counter-cleanup.py
bash scripts/build-quest2-vulkan-device.sh
```

两次 prepare 均拒绝已有输出，固定输入哈希；第二次还必须通过实际 ARM64 donor 循环的680组 QEMU 所有权检查。九库清单、生成的 device/namespace 探针及其清单同步至设备私有 `sq2-closed` 对应目录，两个运行脚本也放在该私有目录。凭据/网络信息由操作者提供，不进 Git。

从 retained initramfs root 执行：

```sh
SQ2_VK_COUNTER_CLEANUP=1 SQ2_VK_DEVICE_QUEUES=4 SQ2_VK_DEVICE_CYCLES=150   sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-vulkan-only.sh --device
SQ2_VK_COUNTER_CLEANUP=1   sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-vulkan-only.sh --ahb
SQ2_VK_COUNTER_CLEANUP=1   sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-vulkan-only.sh --window
```

GPU 测试必须串行，共用旧图形测试锁。新 helper 在独立 private mount namespace 中保留原 EGL 目录 FD，把原有 Mesa 文件逐字复制至临时目录后才加入 Vulkan 依赖，拒绝覆盖同名文件，核对 Mesa EGL 哈希且不包含 donor GLES。通过原系统 libvulkan 加载私有 HAL。退出核对原 EGL/Vulkan、boot ID、SF/system_server PID；不改变全局挂载。

设备 create 探针支持1–4个实际公布的队列及1–150次创建/销毁，不提交命令。四队列150轮、AHB完整读回和12帧交换链已通过。细节见 [QUEST2-VK-CREATE-AUDIT.md](QUEST2-VK-CREATE-AUDIT.md)。

## 首次两分钟系统试验

`test-quest2-vulkan-only.sh --start` 做临时全局 Vulkan 切换。复制原 Mesa EGL/GLES 并核对后，加入九个 Vulkan 依赖；仅 Vulkan HAL 接入闭源。独立 retained-root watchdog 在120秒后撤回挂载并重启 zygote，以清除应用候选映射。SurfaceFlinger不切换/重启，独立进程分时截图，启动镜像不修改。watchdog不能处理整板/GPU死锁。

本轮已完成自动回退：busy挂载使用lazy unmount，两个原Mesa哈希恢复，boot ID不变，SF PID不变；zygote/system_server因计划内回退重启。不能称为board crash。默认仍是 Mesa Vulkan。

真实应用结果：

- Chrome GPU进程实际映射候选 Vulkan 及两个独立命名的配套 QGL/GLNEXT。未再出现原 `vkCreateDevice` 的重复释放，但出现大量 `vkCreateGraphicsPipelines = -13`。截图可见正常网页和状态栏，不能据此宣称 Vulkan 渲染通过或排除回退路径。
- 普通 `org.surface.sq2.vulkanprobe` 通过系统加载器识别 Adreno 680，`vkCreateSwapchainKHR = -1000072003`，应用内容黑屏，系统状态栏正常。原生 ImageReader 私有窗口通过不覆盖真实应用窗口；接下来应记录 native-buffer 准入失败的具体阶段与实际元数据/应用命名空间。
- 旧图标/状态栏异常属于闭源 GLES 的独立问题，继续使用 Mesa 可避免把它混入这轮 Vulkan 排查。仍需后续实际应用回归，不能据本轮短测宣布所有 UI 问题已解决。

原始截图、maps、logcat 保存在忽略的 `out/quest2-gpu/kmsg/vulkan-only-*`。不要提交用户网页、地址或原始日志。当前不应重复同一全局候选；先用有界诊断定位 pipeline 和 native image import 两处失败，再重试。

## 普通 APK 外部句柄错误修复

新增 `SQ2_VK_APP_DIAGNOSTICS` 编译开关将有界失败阶段写入 logcat（最多96条），未开启时不添加这些日志。实机普通 APK 明确得到 `read_private stage=mem-open errno=13`，之后 BuildID 验证失败、`CreateImage-native-rejected=-1000072003`。root 私有探针成功不能排除这一应用权限边界。

`SQ2_VK_PROCESS_READ` 候选保留地址去tag、溢出/大小上限及 maps 可读范围检查，用 `process_vm_readv(getpid(),...)` 做有界内核拷贝，遇到错误/短读仍拒绝；未改为未经验证的 memcpy，也未放宽 native 元数据/唯一owner校验。该改动目前通过显式候选编译开关启用，未改变基础构建或设备启动镜像。

构建诊断候选：

```sh
bash scripts/build-quest2-vulkan-app-diag.sh                 # v1: 原proc-mem +诊断
bash scripts/build-quest2-vulkan-app-diag.sh --process-read  # v2: self process read +诊断
bash scripts/build-quest2-vulkan-app-diag.sh --pipeline      # v3: v2 +有界管线/着色器捕获
```

构建拒绝已有目录，复制固定 counter-v1 的其它八库，仅重编 wrapper。v2 wrapper SHA256 为 `f167c19ee43f62afad20b686508e449b6605dd9e008c8202474c22b309648c7d`。`SQ2_VK_APP_DIAGNOSTICS=1|2|3` 选择对应全局120秒试验；私有runner支持1/2。正式持久化之前须生成无诊断候选并验证，不能把 v3 捕获功能放入正常启动。

实测 v2：私有窗口12帧 present/fence/读回及最终引用归零；普通 `org.surface.sq2.vulkanprobe` 由系统 Vulkan loader 加载候选，报告 `READY surface=2880x1920 images=4` 和 `SQ2_VULKAN_WSI_OK frames=120`。截图为正常彩色三角形，状态栏正常。真实试验随后恢复 Mesa，boot保持。这个结果修复先前记录的普通APK交换链失败，不代表所有外部图像场景已完成。

## Chrome 结果的准确边界

用户确认 Chrome 正常打开，截图也显示完整网页。v3采集确认 Chrome ANGLE 请求同一族4队列且创建成功；前8次被捕获的图形管线创建均返回成功。随后部分调用仍报 `Failed to link shaders` / `vkCreateGraphicsPipelines=-13`，还有 `AllocateMemory=-3`。**这应记录为“Chrome可用，部分Vulkan路径有待排查”，不能表述为Chrome整体不可用，亦未证明所有错误均触发GLES回退。**

`quest2-vk-pipeline-trace.h` 只在显式诊断构建接入，保留调用参数/返回值，最多捕获每进程前48个shader（各不超过256KiB）和前8次图形管线元数据；日志含句柄关联编号，不输出地址。SPIR-V写入操作者预先创建的私有实验目录 `/data/local/tmp/sq2-vk-capture`，单文件0600、O_EXCL/O_NOFOLLOW；正常构建无此功能。需要给实验进程创建文件的权限，本次专用目录为1777。捕获文件/日志仅保存在out，不能发布。失败发生较晚，前8次均成功，当前样本不能直接用于失败shader的完整重放；下一轮须按失败条件有界捕获并覆盖模块生命周期，而非简单增加无限日志。

## 本轮 Wild Life：失败，按用户要求提交现状

使用 v2（保留计数器修复与self process read）的120秒系统Vulkan试验，Mesa EGL/GLES保持。已进入Wild Life页并点击运行。采样截图明确显示“基准测试运行失败”，用户同时确认测试失败并要求提交当前结果。**没有取得有效分数或完整场景，不能记作完成跑分或性能结论。**

3DMark进程maps中取得候选 Vulkan和配套GLNEXT加载记录；最后采集的logcat窗口已晚于最初失败，尚不足以归因到某条具体驱动错误。本轮没有启用v819专用Direct选择器，也未创建v77的Direct配置；不要把旧v819 Direct成绩挪用到这次v77系统后端。原始错误页/部分截图、maps和logcat留在out，因计划内zygote重启导致一次SCP中断，缺失文件不作已采集证明。

停止后执行回退并核对原Mesa EGL/Vulkan哈希及同一boot ID；未写启动镜像。按用户要求不继续重跑，仅提交修复、诊断工具与诚实的失败记录。系统替换目标仍未完成。
