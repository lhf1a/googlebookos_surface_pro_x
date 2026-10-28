# 构建与部署

EGLImage上传新对照：直接上传后finish+DMA同步读取原fd仍全零；PBO上传64²连续150轮通过但小尺寸仍全零，尚非通用修复。诊断参数与边界见 [QUEST2-HWUI.md](QUEST2-HWUI.md) 末节；默认Mesa保持。

最新隔离复现：HWUI仅硬件Bitmap失败；原生EGLImage纹理glTexSubImage2D上传后采样全零，Mesa对照正确，GPU绘制同类源及普通纹理上传正确。另Vulkan单队列通过、两个合法队列即在原Chrome位置Scudo abort，无需提交。两条具体阻断未修复，正常Mesa/boot保持；见 [QUEST2-HWUI.md](QUEST2-HWUI.md) 与 [QUEST2-VK-CREATE-AUDIT.md](QUEST2-VK-CREATE-AUDIT.md)。

最新闭源候选 combined-v5 已补严格 BGRA/RGBX，私有九格式组合/FP16/双API回归通过；真实120秒桌面仍有图标黑块、状态栏横条和应用黑屏，Chrome vkCreateDevice 还有Scudo释放错误。两分钟回退及分时截图已实测，Mesa已恢复，不可提升默认。配套编译器已在实际SystemUI maps确认；后续定位producer/compositor及Chrome创建参数，见 [QUEST2-COLOR.md](QUEST2-COLOR.md)。

脚本来自迭代实验室，依赖缓存和分阶段产物；全新克隆不能直接生成完整可启动 SSD。不要删除哈希或磁盘身份保护以绕过缺失输入。

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
