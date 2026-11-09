# 原厂 gralloc 共享内存：首个 6.18 实机增量

## 2026-11-08：原厂 allocator Binder 服务已通过

allocator/v4通过原厂HIDL服务跨进程分配与mapper3导入，RGBA8/RGB10A2/FP16三尺寸共27缓冲，
callback后锁定写入/读回逐字节一致。新增mapper兼容层修正已实证的RGB bytesPerStride单位错误，
使用原厂公共metadata核对容量；P010旧metadata缺陷仍未修复。
native-gpu-v24的27轮RGB GPU/fence/exec及6帧NV12 VPU→GPU回归通过。
系统默认allocator/mapper未切换；临时服务/声明已撤下，OMX恢复注册。
细节、失败对照与后续系统HAL入口要求见[HANDOFF最新节](HANDOFF.md)。


## 2026-11-08：原厂 OMX 原生缓冲完整帧循环与分辨率往返通过

当前新增实测是进程私有 OEM OMX→UseBuffer2 原厂 gralloc→Venus→GPU，
持久系统服务仍 AVC-only/RequiresSwRenderer，系统 allocator/mapper/HWC 未替换。
本阶段无重启、无 EFI 写入，boot b56c010f…、SF2858/system_server3322保持；
结束时 Venus sys_error=0、insts=0、vb2_bytes=0，ESP可用1355500KiB、logs103268KiB。

- v17 原厂 AVC native 接口：enable/useBuffer2/getUsage 三扩展实际成功；
  usage=0x20000000（PRIVATE_UNCACHED），真实2fd+26ints句柄UseBuffer/FreeBuffer成功，
  fd所有权仍归调用方。源参考审计input-audit-v5为133个donor文件、23份公开源码；
  sm8150 media 810e6add…的接口参考不等于精确Quest二进制源码配套。
- v20 原厂 HEVC Main/NV12 六帧：实际完整状态转换、UseBuffer、ETB/FTB、初次端口重配置、
  EOS与释放通过；六帧紧凑NV12与独立软件参考逐字节相等，SHA325a21d7…。
- 最终 native-gpu-v23：同一流30帧，320×240×18帧→1920×1080×6帧→320×240×6帧。
  30帧逐帧SHA与参考相等，PTS全匹配；初始加两次尺寸变化共3次端口重配置。
  首个六缓冲池每个buffer实际复用3次。30次GPU原始YUV采样bad=0，
  最大误差0.00048637，每帧等待真实native fence后才归还OMX；EOS/Idle/Loaded/FreeHandle/Deinit全通过。
- NV12实际gralloc usage=0x20400103（包含codec uncached要求），320布局512×512、
  UV262144、393216字节；1080布局2048×1536、UV3145728、4718592字节。
  CPU仅只读映射验证/保存，未把解码像素上传GPU。GPU每2×2检查一个Y及对应UV；
  独立软件参考覆盖全部可见像素。不是物理屏幕呈现或性能验收。
- v22同版桥的普通AllocateBuffer路径（不启用native-layout）六帧逐字节回归通过。
  v23只进一步统一helper声明和安全查找buffer slot，桥与v22一致。
  48项主机测试通过（21客户端、8桥、11接口/I/O、6视频builder、2native GPU builder）；
  新增实际C/ASan/UBSan覆盖队列busy/协商失败/内核改写拒绝、不可访问opaque handle、
  只读同步、过短/越界/损坏帧在GPU采样前拒绝，原路径默认行为保留。

真实问题和修正：
1. v19最后一次UseBuffer内STREAMON失败，因为此路径不替客户端申请输出队列。
   按原厂契约在UseBuffer前SetParameter(OutputPortDefinition)，v20贯通；未伪造STREAMON成功。
2. 仅在真实SOURCE_CHANGE后且CAPTURE已STREAMOFF/REQBUFS(0)，通过原厂临时allocation查询布局，
   向现有capture-layout-v2内核提交真实S_FMT，再允许队列分配；失败保留原errno，禁止busy队列修改。
3. v21切1080时OMX GetConfig尚返回旧320×240 crop，客户端容量检查拒绝了错误小分配。
   最终按该代缓冲对应的真实G_SELECTION尺寸分配/验证，旧池的尾帧仍用旧尺寸。
   没有修改原厂组件的crop缓存、输出header或全局metadata，系统HIDL仍需对应集成。

入口：quest2-omx-decode.c的显式QUEST2_NATIVE_OUTPUT/--native-buffers；
quest2-omx-native-output.h使用原始句柄，不能将header->pBuffer读成像素。
QUEST2_VIDC_NATIVE_LAYOUT=1仅启用进程导出的quest2_omx_capture_layout；缺回调拒绝，
默认桥不协商原生布局。当前helper仅支持单进程单解码器、非安全线性NV12/零crop原点，
不能直接作为多实例服务。没有将OMX原生P010/HDR或系统Surface声明为完成。

全部私有证据：out/qualcomm-full-stack-20261108/omx-native/
import-v17-device.txt、decode-v{19,20,21,22,23}-device.txt、对应bridge/expected/result、
decode-v22-legacy-device.txt、final-health.txt；最终构建native-gpu-v23/BUILD.json。
v19/v21失败均已正常FreeHandle回收，未出现新的sys_error或板级重启。
源码与记录仍未提交/推送；私有库、固件和原始日志不进Git。

下一步：将原生buffer的usage、逐代crop/layout与fence契约接入HIDL/ACodec和系统分配服务；
补显式flush后续播、多实例/长视频及原生P010/metadata。MSM GEM WC依赖须在KGSL前移除。
KGSL、下游msm_vidc、QTI HWC、HDR显示、真实PM超时与292/300 rendered回调缺口仍未完成。


## 2026-11-08：NV12/P010 解码到原厂 gralloc 再到 GPU 实机贯通

当前默认为 EFI/SQ2Nat、sq2-qcom-618-native；initramfs
f48e732a316dead047deb25a16b88af9ee2f9e1335a619fd3bed029844072f43。
SQ2QDbg原运行包仍为唯一回退，8项hash保持，未创建备份或恢复旧入口。
本阶段两次正常重启；最终wait窗口88.6秒恢复，boot b56c010f-5b1a-4730-95f3-df403a27689d。
实际加载core/decoder的SHA与Build-ID与venus-capture-layout-v2一致；encoder成套打包但未加载实测。

- native-gpu-v15：HEVC Main、320×240、NV12；原厂真实VIDEO_DECODER usage0x400103，
  协商stride512/scan512、UV offset262144、393216字节。6帧独立CRC全部匹配，
  原生EGLImage直接采样同一dma-buf，6帧bad=0，最大误差0.00048637。
- native-gpu-v16：HEVC Main10/HDR10输入、320×240、P010_VENUS；
  stride768/scan256、UV offset196608、294912字节，6帧独立CRC全匹配，
  10-bit原始YUV GPU采样bad=0，最大误差0.00048733。严格检查16bit容器低6位为0。
- 两条路径均真实native fence等待后再归还V4L2，6输入全部归还、EOS/LAST、
  STREAMOFF/CLOSE、原厂句柄释放通过。CPU仅只读CRC/采样对照，没有上传解码像素。
  每帧验证全部可见Y/UV行的CRC；GPU在每个2×2块中心取Y及该块UV，不宣称GPU检查每个Y像素。
- v16 RGBA8/RGB10A2/FP16三尺寸共27轮GPU读写/native fence/CPU/SCM_RIGHTS-exec回归通过；
  fd稳定9、引用归1。普通非native-gralloc的NV12/P010六帧解码路径也通过。
- 结束时SF2858/system_server3322未变，Venus sys_error0/insts0/vb2_bytes0。
  一次SSH连接超时后重试恢复，boot未变，不将其当成新的PM根因证据。

实际修复：标准CAPTURE S_FMT协商分配几何，与编码尺寸/crop分开；
使用HFI输出平面对齐约束并核验固件bufreq容量。HFI v4原先未封装该属性，
现共用v4/v6线格式，补齐平面数1/2检查、未用平面清零。DRC事件清除旧布局。
v1在STREAMON拒绝并保留DMA引用，正常重启释放；v2确认固件写出指定布局。
初次GPU探针使用像素边界导致nearest采样选点差异，改为像素中心后通过；
没有放宽误差阈值或跳过CRC。原厂旧P010 FLEX/YCbCr metadata缺陷仍未全局修复。

这是进程私有native buffer验证，不是系统allocator/mapper替换或MediaCodec Surface零拷贝验收。
持久OEM HIDL仍AVC-only/RequiresSwRenderer，系统仍MSM DRM、minigbm/mapper5/drm_hwcomposer。
本次P010试验没有验证HDR metadata进原厂handle、显示tone-map、动态分辨率/长视频、UBWC或secure。
KGSL/下游msm_vidc/HWC、真实PM超时、292/300渲染回调缺口均未完成。

证据均在out/qualcomm-full-stack-20261108：
capture-layout-build-v2.json、capture-layout-pack-v2.txt、capture-layout-v2-postboot.txt、
native-gpu-v15-device.txt、native-gpu-v16-device.txt、native-gpu-v16-rgb-regression-retry.txt、
native-gpu-v{15,16}-legacy-decode-regression.txt。原始日志和私有产物不进Git。
下一步将已验证的layout/usage/fence契约接入原厂OMX native-buffer接口与系统分配服务，
补做DRC/flush/复用生命周期及metadata验证，再推进KGSL/HWC；不能只取消SoftwareRenderer标志。


## 2026-11-08：P010 原生 GPU 精度通过，推进 VPU 共享布局协商

原厂 P010 的旧 FLEX/YCbCr metadata 不一致仍存在，但已经进一步验证物理像素：
native-gpu-v9 标准 P010 128×72、v10 P010_VENUS 320×240，均三帧，
以 GL_EXT_YUV_target 原始 YUV 采样到 RGBA16F，再浮点读回。Y 包含
64/65/66、512/513/514、939/940，UV 也改变低位；零错点，最大误差0.00040907，
低于0.00055阈值，真实 native fence 通过。没有全局改写原厂 metadata，
不能由此声明 Android 系统已接通原生视频或 HDR tone-map。

新增原生视频探针使用真实 VIDEO_DECODER producer usage，保留原厂句柄，
将同一像素 dma-buf 的重复 fd 交给 V4L2；CPU只读验证，GPU直接采样。
v12 实验在入队前拒绝布局不一致：Venus NV12 320×240 可见区域需要
384×256，而 donor 分配/布局为512×512、UV offset262144、容量393216。
此前假设 NV12 天然匹配已被否定。P010_VENUS 也有768对640行距差异。
会话正常关闭，Venus错误/实例/存留字节均0，没有因此发生新重启或系统进程退出。

mapper 查询/验证及 ANativeWindowBuffer usage 现在使用原厂 getUsageFlags，
避免将 gralloc1 CPU_READ_OFTEN=6/WRITE_OFTEN=0x60误当legacy/HIDL值。
v13编译包含此修正和共享EGL context；此前RGB实机证据以v6为准，须单独回归新构建。

内核候选 scripts/patch-sq2-venus-capture-layout.py 将 CAPTURE S_FMT 的扩展
bytesperline/height与编码宽高、可见crop分开，向实际OPB发送HFI平面对齐约束；
检查尺寸/容量、格式与SOURCE_CHANGE阶段，DRC清除旧布局，默认路径保持。
只允许线性NV12/P010，保留固件bufreq大小检查，不伪造G_FMT通过。
配套6.18三模块已构建并打包，验证进展见HANDOFF最新记录。
新源参考审计133个donor文件和21份公开参考通过；并不证明精确OEM源码配套。


2026-11-08。整体目标仍为 [高通整栈移植](QUALCOMM-FULL-STACK-PORT.md)。
本轮完成输入审计与原厂 gralloc1 的独立内存验证，未替换系统 allocator/mapper/HWC。

## 实际结果

- 固定 v77 原件：gralloc.kona、gralloccore、grallocutils、qdMetaData、adreno_utils、GSL。
  仅私有 core/GSL 的 libion 依赖改为新 DSO；实际加载路径由探针输出。
- 从现有 VIDC 桥提取共用 system ION 后端，cached 使用真实 system DMA heap，
  uncached 继续使用已验证 MSM GEM WC。新增仅接受页大小以内二次幂对齐；
  不将 secure/其他 heap/未知 flags 转成普通内存。
- 原厂 descriptor/allocator 生成实际 QTI handle：2 fd + 26 ints。
  RGBA8888、1 layer、CPU 读写与 GPU_TEXTURE usage，64×64、13×7、2880×54各三轮。
  像素 fd 分别为16384、4096、622592字节，metadata fd为32768字节。
- 原厂 LOCK/UNLOCK 下逐字节写入/读回，retain/release 后正确；父进程 fd稳定7。
- 每轮 SCM_RIGHTS 传递真实 fd及原生句柄，fork后exec到全新进程。
  接收方使用 gralloc1 的必需 RETAIN 接口导入，原厂重新建立本进程映射；
  父进程释放后接收方仍逐字节正确。根据实际 RELEASE_IMPLY_DELETE 能力处理所有权。
  donor不提供可选 IMPORT_BUFFER，不能把可选入口缺失当成不支持跨进程。
- 最终 v7 在清理后的正常重启再次全部通过，SF/system_server PID保持，
  Venus sys_error=0、insts=0、vb2_bytes=0。没有运行解码或 GPU 绘制。

早期私有试验遗漏同版 GSL，原厂布局计算得到size0，分配按EINVAL失败。
已通过补齐配套库修正测试环境，未把零长度分配改成成功或猜测替代大小。
后续试验的实际函数归属覆盖GSL、adreno布局和ION，避免再误用系统同名库。

## 重建

在本工作树根执行：

~~~sh
python3 scripts/build-quest2-gralloc-probe.py \
  --vendor-root /mnt/spx-workspace/chromebookOS/out/quest2-gpu/vendor-reference-v77/vendor \
  --cc /mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
  --headers /mnt/spx-workspace/toolchains/sq2-graphics/src/mesa-26.2.4/include/android_stub \
  --output out/FRESH-GRALLOC
~~~

完整目录复制到设备新的私有目录，先在该目录运行 sha256sum -c SHA256SUMS，
设置可执行权限和适合实验环境的文件标签；在Android mount/root namespace运行：

~~~sh
LD_LIBRARY_PATH=PRIVATE_DIR:/vendor/lib64:/system/lib64 \
  PRIVATE_DIR/quest2-gralloc-probe PRIVATE_DIR/gralloc.kona.so
~~~

PRIVATE_DIR必须是绝对路径。探针自行exec跨进程消费者；不要用当前Shell目录代替库目录，
也不要将这些库覆盖vendor/system或注册为默认HAL。运行前后检查boot/SF/系统PID与Venus健康。
测试是assert式严格诊断，失败非零退出；不能把仅载入库或只分配fd记为像素通过。

主机错误路径测试：

~~~sh
python3 -m unittest discover -s tests -p test_quest2_system_ion.py -v
python3 -m unittest discover -s tests -p test_quest2_vidc_bridge.py -v
python3 -m unittest discover -s tests -p test_build_quest2_video_probe.py -v
python3 -m unittest discover -s tests -p test_quest2_full_stack_inputs.py -v
~~~

共用后端测试执行真实C逻辑，mock仅替代ioctl，覆盖拒绝未知heap/security/alignment、
cached失败、WC flags核验、PRIME导出和各失败阶段的fd/GEM清理；ASan/UBSan通过。
VIDC重构后的Android ARM64库已编译，但未替换持久服务；现有解码验收不能转记给新编译库。

## 输入清单与缺口

configs/qualcomm-full-stack-inputs.json 固定133项donor文件（新增原厂mapper所依赖的libhidltransport）及14份公开源码参考。
脚本 inspect-quest2-full-stack.py 核验全部hash并列出每个ELF的DT_NEEDED和未审计依赖。
平台库存在与链接器实际解析、C++ ABI、服务兼容是不同检查，清单不会把它们合并为“匹配”。

公开Meta内核commit 6f22d8e7…属于5158488.10240.150、Linux4.19，
而私有vendor来自51584880050400150；属于同一家族，**精确配套仍未证明**。
AOSP sm8150 display源码仅作接口参考，不是Quest OEM源码。
这些源码已确认A680条目、KGSL/ION/VIDC/SDE接口，不代表能直接在Surface/6.18加载。
固定输入获取URL和hash已进入配置，原件与反汇编留out，不上传私有二进制。

审计命令：

~~~sh
python3 scripts/inspect-quest2-full-stack.py \
  --vendor-root /mnt/spx-workspace/chromebookOS/out/quest2-gpu/vendor-reference-v77/vendor \
  --source-root out/qualcomm-full-stack-20261108 \
  --output out/FRESH-INPUT-AUDIT.json
~~~

下一步：
1. 接入NV12/P010视频布局与真实VPU消费者；RGB10A2/FP16 GPU路径结果见后节。
2. 让uncached内存后端脱离MSM GEM，再切换KGSL；不能先删掉当前分配依赖。
3. 接入目标Android allocator/mapper、原厂视频native-buffer与HWC/显示链路。
4. 保留PM超时、292/300渲染回调缺口、SDR tone-map、UBWC/protected等未验收项。

原始结果：out/qualcomm-full-stack-20261108/gralloc-v7-device.txt、
gralloc-v7/BUILD.json、input-audit-v2.json。不是原厂整栈完成或零拷贝解码证明。

## 原厂mapper与GPU共享内存（后续实测）

native-gpu-v6已在同一boot通过27轮：RGBA8888、RGBA1010102、RGBA_FP16，
每种64×64/13×7/2880×54各三轮。实际原厂HIDL_FETCH_IMapper/QTI cast、importBuffer、
validateBufferSize、getTransportSize和freeBuffer通过；不是旧minigbm metadata桥。
探针直接链接私有高通EGL/GLES，用真实QTI native_handle建立EGLImage，GPU shader采样至
同格式FBO，按原始像素逐字节比较；再GPU写入共享缓冲，导出native fence并等待，
原厂LOCK读取/校验/恢复测试图案，UNLOCK后SCM_RIGHTS/exec导入再次读回。
RGB10验证packed10-bit（含低位）；FP16使用精确0/.25/.5/.75/1，未覆盖所有half位型。
父进程fd始终9、native buffer引用回到1。SF/system_server未重启，Venus保持健康。

~~~sh
python3 scripts/fetch-quest2-mapper-sources.py
bash scripts/generate-quest2-mapper-headers.sh
python3 scripts/build-quest2-native-gpu-probe.py \
  --vendor-root /mnt/spx-workspace/chromebookOS/out/quest2-gpu/vendor-reference-v77/vendor \
  --cc /mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
  --headers /mnt/spx-workspace/toolchains/sq2-graphics/src/mesa-26.2.4/include/android_stub \
  --hidl /root/.cache/sq2-quest2-hidl \
  --platform-libs out/quest2-video-618-20261107/system-omx/platform \
  --runtime out/qualcomm-graphics-618-20261107/runtime-618-layout8-direct-15 \
  --output out/FRESH-NATIVE-GPU
~~~

复制新目录、校验SHA与标签后，在Android namespace中以外部40秒timeout运行：

~~~sh
LD_PRELOAD=PRIVATE_DIR/libquest2-native-mapper.so \
LD_LIBRARY_PATH=PRIVATE_DIR:/vendor/lib64:/system/lib64 \
  PRIVATE_DIR/quest2-gralloc-probe PRIVATE_DIR/gralloc.kona.so
~~~

仅进程私有动态链接，不注册HAL。实验不能隔离共享GPU挂死；不并行运行GPU实验。
构建器保持原件和固定Direct补丁，用统一system ION替换GSL旧拒绝分配stub依赖，
记录编译命令、实际platform/header/source输入SHA与全部输出。私有原厂mapper依赖
libhidltransport.so（10408字节，无公开HIDL实现符号）已加入同vendor输入清单。

21份Gitiles归档通过Git blob/tree内容hash与官方固定commit的tree核对；
保留历史archive SHA，之后按固定tree验证而非放宽任意归档。其余7个工具/HAL仍验SHA256。
生成器产物和头文件留本地cache/out；主机22项相关测试通过。
证据native-gpu-v6-result.json、native-gpu-v6-device.txt、native-gpu-v6/BUILD.json。
尚未验证YUV、VPU共享、显示scanout、UBWC、protected、HDR色彩或tone-map；
系统仍minigbm/mapper5/drm_hwcomposer，GPU内核仍MSM DRM，未完成KGSL整栈迁移。

## YUV/P010当前阻碍

`PRIVATE_DIR/quest2-yuv-layout-probe PRIVATE_DIR/gralloc.kona.so 0x36`使用同一私有LD路径。
标准YUV420_888(0x23)当前usage组合被拒绝；P010(0x36)可分配但LOCK_FLEX位深/采样
信息不符。原厂mapper3 lockYCbCr同样把Cb/Cr指针隔成1字节，而P010应按16-bit分量。
原厂QTI getFormatLayout输出也保留了Y step1等通用字段；不能直接宣称YUV已贯通。
原始失败、测量和停止边界见HANDOFF最新节。尚未做YUV像素写入/GPU采样/VPU消费。
构建器额外输出这个严格诊断客户端；失败退出不能解释成系统allocator已切换。
