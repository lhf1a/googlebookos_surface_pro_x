# Quest 2 闭源 GPU 驱动研究

2026-10-28，分支 `codex/quest2-proprietary-gpu`。已取得两个发布者标注为 Quest 2 提取的 Vulkan 包。v819.1 经可选 profile 规避和进程内 QTI→mapper5 适配后，原生 MSM DRM 计算、图像、三角形、AHardwareBuffer 读回及独立普通 APK 的 2880×1920 窗口呈现通过。不是完整系统驱动替换；没有修改 Surface 启动镜像、固件或正常桌面 Mesa。下文首轮 AHB 失败为历史对照，最新结果见文末。

## 已取得的证据

已取得 Meta 官方 `oculus-quest2-kernel-master` 的固定提交 `e8c2245c4ca511a8208568e713b14500f252c0ec` 中 Makefile、GPU core 表、Kconfig、KGSL UAPI；哈希见 `configs/quest2-gpu-research.json`，下载缓存留在忽略的 upstream/quest2-gpu。

该树版本为 4.19.325，KGSL 的 GPU 表**包含 A680**。这提供了比伪装成 A650 更合理的研究起点，但不证明发行固件的闭源用户态库包含相同设备支持。

“A650 与 A680 只有频率差别”的假设不符合已读源码：A650 GMEM 为 1152 KiB，A680 为 2048 KiB，SQE/GMU 固件名及初始化表也不同。当前本地 Mesa 26.2.4 将 A650 配置为 gen3、3 CCU、96×16 tile alignment，A680 为 gen2、4 CCU、64×32。已有 661 MHz OPP 调整不改变这些差异。不能将硬件 ID 改成 A650 来绕过设备识别。

一手来源：[Meta GPU core 表](https://github.com/facebookincubator/oculus-linux-kernel/blob/e8c2245c4ca511a8208568e713b14500f252c0ec/drivers/gpu/msm/adreno-gpulist.h)、[KGSL UAPI](https://github.com/facebookincubator/oculus-linux-kernel/blob/e8c2245c4ca511a8208568e713b14500f252c0ec/include/uapi/linux/msm_kgsl.h)、[Kconfig](https://github.com/facebookincubator/oculus-linux-kernel/blob/e8c2245c4ca511a8208568e713b14500f252c0ec/drivers/gpu/msm/Kconfig)。

## 首选推进顺序

1. 取得可追溯版本的 Quest 2 vendor 镜像或提取包，记录系统 fingerprint、包 SHA256、ELF build ID（如有）。完整 EGL/GLES/Vulkan 库和依赖优先；只有 AdrenoTools Vulkan 包时明确其不覆盖 GLES/桌面合成。
2. 静态分析 ELF 依赖、未定义符号、KGSL/ION/dma_heap 引用及设备识别路径。查找 A680 字符串仅作线索，不作为兼容结论。缺少某库时先分析依赖，不批量覆盖系统 vendor。
3. 新取得的 v819.1 包存在 MSM DRM 后端线索，先核实并尝试其原生路径，保留目前内核显示和电源驱动。原生路径不可用时，再评估独立用户态 KGSL→MSM 适配原型的最小工作量。需要真实处理 GPU VA、缓存一致性、上下文、提交、时间戳及 sync_file；不能只转发 ioctl 编号。若 donor 的直接 syscall 或专用内存路径阻断用户态拦截，再决定是否走内核兼容层。
4. 备选为移植 KGSL 的 A680 路径至独立实验内核。4.19 厂商树到 6.3.8 不是复制 .ko：Kconfig 依赖 QFPROM、MSM_SUBSYSTEM_RESTART、QCOM 专用 devfreq governor 等，仍需逐项查清 IOMMU、SCM、GMU、时钟、供电和总线投票。MSM 与 KGSL 不可同时接管同一 GPU；显示 KMS 的保留需另行设计。
5. 第一实机目标为隔离进程中枚举真实 GPU，随后做离屏计算/图像读回。通过后再做 DMA-BUF/AHardwareBuffer 导入、fence、modifier/压缩和普通应用。最后才接桌面，保留独立救援与一次性启动回退。

## 已识别的接口工作

| KGSL 接口族 | 当前 MSM 对应能力与待查差异 |
| --- | --- |
| DEVICE_GETPROPERTY | GET_PARAM；真实 chip ID、GMEM、VA 布局不可伪造 |
| DRAWCTXT_CREATE/DESTROY | submitqueue 等；context flags、优先级和隔离语义待核对 |
| GPUMEM_ALLOC_ID/MAP_USER_MEM | GEM/DMA-BUF；对象 ID、mmap offset、CPU/GPU VA 生命周期不同 |
| GPU_COMMAND | GEM_SUBMIT；命令对象、依赖与 fence 需转换 |
| TIMESTAMP_EVENT/等待 | KGSL context 时间戳与 MSM fence/sync_file 不是相同句柄 |
| GPUMEM_SYNC_CACHE | 缓存同步及一致性，不能简单返回成功 |

此表是研究拆分，不是已实现转换或对具体 donor 调用轨迹的结论。

## 本地审计工具

```sh
python3 scripts/audit-adreno-donor.py downloads/quest2-extracted/vendor \
  --output out/quest2-gpu/donor-audit.json
```

脚本在 WSL 使用 readelf，递归读取 ELF，不执行或加载 donor。输出哈希、位数/架构、SONAME、NEEDED、未定义动态符号和有限设备路径提示。依赖清单中“未找到”不等于无法加载：Android 系统可能提供，需要另做 namespace/符号版本核查。

已用本地 AArch64 Dropbear ELF 和自行编译的 AArch64 共享库验证 ELF 读取、NEEDED、SONAME、符号版本及设备路径提取。固件/闭源二进制及原始报告保持 downloads/upstream/out/private 忽略路径，不随研究代码发布。

## 已取得 donor：2026-10-28

已下载 [v819.1](https://github.com/K11MCH1/AdrenoToolsDrivers/releases/tag/v819.1) 和 [v690](https://github.com/K11MCH1/AdrenoToolsDrivers/releases/tag/v690)，发布者均标注提取自 Quest 2。两者都是经 AdrenoTools 重打包/依赖改名的 Vulkan 包，不能称为已验证与官方 vendor 镜像逐字一致，也没有完整 EGL/GLES 入口库。归档 SHA256、每个 ELF SHA256/build ID/依赖已写入研究 JSON。发布 API 没有给出资产 digest，本地 SHA256 用于固定本次下载，不构成厂商签名认证。

两个包各有六个 AArch64 ELF。v819.1 的 notgsl.so 包含 Adreno 680/690、PM4CC_GPU_VERSION_A680_POIPU、/dev/kgsl-3d0 及 /dev/dri/renderD128 字符串，并出现 DRM_IOCTL_MSM_GET_PARAM、DRM_IOCTL_MSM_GEM_SUBMIT、DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE、MSM_BO_CACHED_COHERENT、gsl_drm_* 等线索。因此此前将所有 donor 一概认定为 KGSL-only 不准确：本版本应首先调查原生 MSM DRM 路径。字符串存在不证明此路径可达、支持当前内核或 A680.1，需交叉引用和隔离加载/调用跟踪。

v690 的本次扫描找到 KGSL 和 A680/690 名称，未找到上述 DRM 线索，暂作版本对照。注意 v690 是驱动版本号，不是 Adreno 690 GPU 型号；其包描述还限制部分设备并提示固件配对要求，不能仅凭名称选择。

v819 Vulkan 库依赖 vendor.qti.hardware.display.mapper 2.0/3.0/4.0、mapperextensions 1.1 及 Android HIDL mapper 库。这些库没有包含在该压缩包，需要核对目标 Android 的实际可用文件与链接命名空间。即使补齐加载依赖，也不意味着现有 minigbm buffer handle 与 QTI 私有布局兼容。优先目标仍是独立进程离屏枚举与读回，不能直接更换系统 Vulkan HAL。

下载位置：downloads/quest2-gpu/v819.zip、v690.zip；静态审计输出：out/quest2-gpu/v819-audit.json、v690-audit.json。后续隔离执行结果如下，当前系统部署未改变。

## 首轮实机结果

当前内核 6.3.8-sq2-android1，现有 MSM DRM、真实设备 ID `0x06080001`、原有固件和动态调频。实验文件只写入 Android `/data/local/tmp/sq2-closed`；没有修改系统 HAL、属性、启动项、驱动模块或频率。正常桌面继续使用已验证的 Mesa。

| 候选 | 实测结果 | 结论边界 |
| --- | --- | --- |
| v690 原文件 | `vkCreateInstance` 返回 −3；GSL 打开 `/dev/kgsl-3d0` 失败 | 本机没有 KGSL，不能直接使用此包 |
| v819.1 原文件 | HAL 加载成功，进入 MSM DRM；在当前系统 `libadreno_app_profiles.so` 的 `ApplyApplicationProfile+240` 崩溃，退出 139 | 可选 profile 回调与 donor 不匹配的证据；未证明私有结构的具体差异 |
| v819.1 跳过可选 profile 回调 | 枚举 Adreno 680、Vulkan 1.1.295；计算、optimal 图像清除/复制读回、顶点/片段着色器三角形读回通过 | 原生 MSM 路径确实可执行，不能由此宣称完整 Vulkan/应用兼容 |
| 同上，Android AHardwareBuffer | Android 分配成功；驱动属性查询返回 success，但 format=0、externalFormat=0、allocationSize=0、memoryTypeBits=0xa；探针以错误退出 | 未导入、未提交错误缓冲区；阻塞共享缓冲区与应用呈现 |
| 当前系统 Turnip 对照 | 同一探针与 system-first 库搜索顺序下，所有步骤通过；AHB format=37、size=16384，CPU 读回 4096 像素正确 | 当前 minigbm/mapper5 分配链路可用，失败集中于 donor 接口 |

v819 日志确认打开 `/dev/dri/card0`。同时仍报告部分 `getinfo_ext` 对 chip ID 未知、`getfeatures`/性能计数器未实现，不能把有限测试通过理解为这些功能已修复。未伪装为 A650，也未移植 KGSL。

### 可复现的最小改动

`scripts/patch-quest2-optional-profile.py` 校验原始 v819 Vulkan SHA256，仅将唯一的 dlsym 查询字符串 `ApplyApplicationProfile` 等长替换为不存在的 `Skip_ApplicationProfile`。已检查该 donor 调用点先判断查询结果是否为空；不改指令、GPU ID、固件或系统 profile 库。输出是独立实验副本，SHA256 `45e88aa1f3181e6b5e768869eee6119d589831ec9ac5a7b0825b6752d48990aa`。它跳过应用 profile，不是修复 ABI，也不适合不经审计地应用于其他版本。

```sh
python3 scripts/patch-quest2-optional-profile.py \
  downloads/quest2-gpu/v819/vulkan.adreno.so out/quest2-gpu/vulkan-no-profile.so
bash scripts/build-sq2-vulkan-probe.sh
```

将六个 v819 库、独立补丁副本与新探针复制至上述实验目录。使用自己的 SSH 身份；从保留的 initramfs root 执行：

```sh
timeout 30 nsenter -t 1 -m -r -- /system/bin/env \
  LD_LIBRARY_PATH=/data/local/tmp/sq2-closed/v819:/system/lib64:/vendor/lib64 \
  /data/local/tmp/sq2-closed/probe \
  /data/local/tmp/sq2-closed/v819/vulkan-no-profile.so
```

**顺序有意义**：早期 vendor-first 搜索会在 `AHardwareBuffer_allocate` 报 `gralloc-mapper is missing` 并 abort；system-first 后分配成功，剩余失败发生于 donor 属性查询。不要将前者误判为后者，也不要覆盖系统的 nativewindow/UI 库来规避。

探针修正两处 Android HAL 使用问题：通过 HAL 的 `CreateInstance` 创建实例，不依赖 vendor GIPA 返回全局创建函数；清理时检查 `close` 回调非空。曾在 HAL close 处发生的空指针崩溃是探针自身问题，已修复，不能记为 GPU hang。

默认完整测试保留 AHB 失败。显式 `--offscreen-only` 仅跳过 AHB，结束标记为 `SQ2_VULKAN_OFFSCREEN_OK (AHB/WSI not tested)`。新增三角形测试为 8 帧 64×64 图像，顶点/片段着色器生成绿色三角形，背景红/蓝交替；CPU 检查内部和外部像素并避开边界。不是只验证 clear，也不是全分辨率游戏压力测试。

### 重复负载

在 initramfs root 运行复制后的 `scripts/test-quest2-offscreen.sh`：保留现有调频、温控和 screen-only guard；三次独立进程，各运行 10 秒、4096 个字、每字 4096 次整数递推，逐轮校验，随后检查图像/三角形读回与正常退出。GPU 温度达到 80°C 时停止。

三轮实际分别校验 11287、10733、11827 轮，均通过；最高采样 51.4°C。boot ID、system_server 与 SurfaceFlinger PID 全程不变。没有替换当前驱动做性能 A/B，轮数不应当作闭源比 Turnip 快的证据；也不代表长期稳定性、CTS 或 WSI 通过。原始输出保存在忽略的 out/quest2-gpu。

## 首轮 AHB 故障定位（历史）

进程局部的 `quest2-gralloc-trace.c` 已确认 donor 的 `hw_get_module("gralloc")` 返回成功，模块名称 Graphics Memory Allocator Module、API version=1；不是简单缺少 libhardware/gralloc 文件。`build-quest2-trace.sh` 构建此只转发原调用的观察库，在上述单独 probe 命令中增加 `LD_PRELOAD=/data/local/tmp/sq2-closed/libgralloc-trace.so` 即可，不给系统服务设置 preload。AHB 查询反汇编显示尺寸/格式查询依赖额外 mapper 对象与类型分支；这进一步支持调查 QTI 扩展接入，但尚未实现桥接。

此故障随后由下述进程内桥接解决。完整 EGL/GLES donor 仍未取得，这两个包仅覆盖 Vulkan。

## QTI mapper 桥接与普通应用验证

`quest2-qti-mapper.cpp` 使用公开 HIDL 4.0/扩展 1.1 接口定义，提供进程局部对象，将 donor 的尺寸、格式、FD 等查询转为真实 minigbm mapper5 元数据。每次查询先 import 独立 handle，RAII free；返回 FD 借用原始 handle，不返回已释放的复制 FD。64×64 RGBA 的 StandardMetadata allocationSize 为 27756（含附加元数据），像素 DMA-BUF 的 fstat 长度为 16384；QTI getSize 必须与 getFd 对应，不能直接照抄总分配量。

只接受单层、单平面、无保护、modifier=0 的 RGBA8888/AB24。宽度、stride、offset、plane size 来自元数据，像素分配长度来自 FD。未知操作显式 UNSUPPORTED；`getSurfaceMetadata_V1` 在当前路径可以不支持。不是完整 QTI mapper，不支持 YUV、多平面、UBWC、受保护视频或任意颜色格式。系统已有 Mesa 多平面修复保持原样。

桥接库使用平台 `std::__1` ABI，链接本系统 HIDL 库，以 `DF_1_GLOBAL` 和 donor 副本的 DT_NEEDED 加载。早期 LD_PRELOAD 仅用于独立探针；最终 APK 无需全局 preload 或任何系统服务改动。原库 profile 字符串补丁保持，新增独立 SONAME `vulkan-bridge.so`。

实测结果：

- 64×64、13×7、257×129、1920×1280、2880×1920 的 AHB 导入→GPU 写入→CPU 读回通过，包括非对齐宽度；各轮同时完成计算、图像和 8 帧三角形读回。
- 通过固定版本 libadrenotools 加载隔离的系统 Vulkan loader 后，同一完整探针通过。
- 普通 NativeActivity 包 `org.surface.sq2.quest2probe`，通过私有目录中的 donor 创建 2880×1920、4 图像 FIFO 交换链，首轮 120 帧通过，截图为清晰彩色三角形。探针核对 driverVersion=2150838273，拒绝 Turnip 回退；实际进程 maps 含 donor 和桥接库。
- 后续两次冷启动和一次 HOME→返回的窗口销毁/重建，各完整提交 1800 帧，总计 5400 帧；第二次冷启动截图正常。该轮最高 GPU 采样 44°C，boot ID、system_server、SurfaceFlinger PID 均保持不变。固定简单三角形场景，不是游戏压力测试或性能基准。

AdrenoTools 只启用 CUSTOM_DRIVER；不启用 turbo、KGSL 用户映射或文件重定向。应用是单独测试包，不注入其他应用；系统桌面继续使用 Mesa。测试 APK 包含闭源二进制，只保存在忽略的 out/，不发布到仓库。

### 构建与复现

沿用 BUILD.md 的 WSL、NDK r28c、Mesa mapper5 头文件和已提取的本机 vendor 库缓存。额外需要 cmake、ninja、patchelf、glslang-tools、zip、JDK 17。首次准备：

```sh
python3 scripts/fetch-quest2-mapper-sources.py
bash scripts/generate-quest2-mapper-headers.sh
bash scripts/build-quest2-mapper.sh
python3 scripts/prepare-quest2-runtime.py
git clone https://github.com/bylaws/libadrenotools upstream/libadrenotools
git -C upstream/libadrenotools checkout 8fae8ce254dfc1344527e05301e43f37dea2df80
git -C upstream/libadrenotools submodule update --init --recursive
bash scripts/build-quest2-adrenotools.sh
python3 scripts/fetch-sq2-test-sdk.py
bash scripts/build-sq2-vulkan-probe.sh
bash scripts/build-quest2-vulkan-app.sh
```

已有 clone 不重复 clone。构建脚本校验 AdrenoTools 和 linkernsbypass 提交；接口来自 AOSP android-12.0.0_r1 和固定 QTI 公共 HAL 提交。下载缓存记录输入 SHA256；生成头文件、第三方源码与本机 vendor 二进制不进入 Git。`prepare-quest2-runtime.py` 逐个验证原 donor 哈希并输出新的 runtime/hashes.json。复制完整 runtime 到隔离目录 v819，hooks 到同级 hooks，新 readback 为 probe。

```sh
timeout 30 nsenter -t 1 -m -r -- /system/bin/env \
  SQ2_ADRENOTOOLS_DIR=/data/local/tmp/sq2-closed/hooks \
  LD_LIBRARY_PATH=/data/local/tmp/sq2-closed/hooks:/data/local/tmp/sq2-closed/v819:/system/lib64:/vendor/lib64 \
  /data/local/tmp/sq2-closed/probe /data/local/tmp/sq2-closed/v819/vulkan-bridge.so
```

`SQ2_VULKAN_AHB_WIDTH/HEIGHT` 可覆盖默认 64，范围 1–4096。不传 `--offscreen-only` 才包含 AHB。将本地 APK 复制到 Android local tmp 后 `pm install -r`；屏幕保持解锁，运行 `scripts/test-quest2-wsi.sh`（保留的 initramfs root）。脚本只操作此测试包，核对 screen-only guard、GPU 温度、boot ID 和框架 PID，冷启动及窗口销毁/重建后分别等待 1800 帧；退出停止测试包并清理临时采集。日志留在忽略目录，不修改长期日志策略。

## Wild Life 完整场景测试：未通过渲染正确性

2026-10-28，用户要求实测 3DMark Wild Life。未修改 APK、应用数据、内核、固件或启动镜像。临时 RAM Vulkan HAL 分派器仅精确匹配 `com.futuremark.dmandroid.application`，该进程加载 v819.1，其余转发哈希不变的 Mesa。目标加载失败明确报错，不自动回退。

实际进程 maps 包含 `vulkan-bridge.so`、`notgsl.so`、`libqti-mapper-bridge.so`，日志明确报告 Driver Version 0819.0.1 及独立 vendor 路径。应用 UI 的 GLES 仍使用 Mesa，其日志不能据此误判 Vulkan 回退。driver 的 getinfo_ext/getfeatures/perfcounter 警告仍存在，尚未定位与花屏的因果关系。

| 项目 | 结果 |
| --- | --- |
| 测试 | 3DMark 2.7.5093，Wild Life 标准，2560×1440 内部渲染 |
| GPU | 现有 661 MHz 上限、原温控，不强制固定频率 |
| 结果页 | 6357 分 / 38.07 FPS，完成整轮 |
| 场景 | 约 2.3、41.9、55.5 秒采样均为黑块、粉白横带，严重错误 |
| 温度 | GPU-top 2 秒间隔采样峰值 57.2°C |
| 稳定性 | boot ID、system_server、SurfaceFlinger PID 不变；检查的 dmesg 未见 GPU fault/hang |
| 判定 | **渲染失败，成绩不能用作有效性能对比或兼容性通过证明** |

现象类似未使用 sysmem 绕过的 Turnip，但相似截图不能证明根因相同。当前仍应使用已验证的 Mesa/3DMark sysmem 配置，不能据 6357 比 5264 高就声称闭源更快。

测试结束停止 3DMark 并解除临时 HAL 绑定。普通 umount 报 busy，随后仅对该文件绑定执行 lazy detach；默认 Vulkan HAL 恢复为 `0b36eeba…`，系统加载器重新报告 Turnip，计算、图像、三角形及 AHB 读回全部通过。临时库仍在 RAM vendor overlay 和忽略的测试目录，不被正常入口选择，无镜像更改。

复现工具：`build-quest2-vulkan-selector.sh` 构建 HMI 分派器；`prepare-quest2-wildlife.py` 校验已有 runtime 哈希并为独立副本设置 `$ORIGIN` RUNPATH。复制 runtime（含 SHA256SUMS）及 selector 到 `/data/local/tmp/sq2-closed/wildlife`，selector 目标名为 `vulkan.sq2-selector.so`。从保留的 initramfs root 运行 `run-quest2-wildlife.sh`，打开 Wild Life 后点击运行；脚本 300 秒自动恢复，80°C 中止，也可创建 `/tmp/sq2-quest2-wildlife.lock/stop` 提前恢复。新测试前必须确认默认 HAL 哈希、仅关屏 guard 和实际 donor 映射，不能只看应用能否出分。原始截图/日志在忽略的 out/quest2-wild-life。

## 后续范围

当前证明此版本闭源 Vulkan 在 SQ2 MSM 后端可运行，并通过有限共享缓冲区与简单窗口呈现测试；Wild Life 复杂场景已确认严重花屏。尚无 CTS、有效性能对比或长期稳定性结论。系统部署需要定位复杂渲染故障并增加格式/同步/生命周期覆盖，完整 EGL/GLES 还需要另找 donor；不因此切换桌面、伪装 GPU ID 或启动 KGSL 内核移植。
