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

### 2026-10-28 Wild Life 再次实测

沿用同一 v819.1 runtime 和临时进程分派器，实际 maps 再次确认闭源 Vulkan、notgsl 与 mapper 桥接已加载。本轮完成 **6320 分 / 37.85 FPS**；14.1 秒和 42.9 秒场景截图均出现严重黑白横带及粉色噪点，因此仍为渲染失败，分数不作有效性能比较。GPU-top 两秒采样峰值 57.2°C；boot ID、system_server、SurfaceFlinger PID 保持不变。未修改镜像或频率配置。

主动结束临时测试后返回 MESA_RESTORED，默认 HAL 完整哈希恢复为 `0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c`；重新冷启动的 3DMark maps 确认使用该 Mesa 文件，screen-only guard 仍为 Y。本轮截图保存在忽略的 `out/quest2-wild-life/retest/`。本次未重复独立读回或测试其他场景。

## 完整 EGL/GLES donor 与实机进展

Quest 2 v77 build 51584880050400150 来自第三方固件归档候选，来源、HTTP range、ZIP/payload 布局见 `configs/quest2-full-donor-candidate.json`。整包约 1.17 GB；整包慢速下载已终止，改为 vendor 精确范围 84,162,032 字节。131 个 REPLACE_XZ 操作均通过各自哈希，重建 vendor 分区 SHA256 为 `44f90742c03c04037e378860ad493098a369d2bc752aa0d56d14f63ae91b6722`，与固定清单一致。未验证整 ZIP 哈希或厂商签名，不称为已认证 OEM 来源，不能刷入 Surface。

通过 debugfs 只读提取的 lib64 保存在 `out/quest2-gpu/q2-v77/lib64`。`inspect-quest2-payload.py PREFIX --payload-offset 3897` 可重现 payload 清单，字段依据 AOSP update_engine/update_metadata.proto；解析器不是签名验证器。`configs/quest2-gles-v77-inputs.json` 固定 EGL、GLES1/2、Android 子驱动、GSL、adreno_utils 和两套 LLVM 编译器的原始哈希。`prepare-quest2-gles.py` 仅复制这些 GPU 库到私有目录，不携带 donor libc、libui 或整个 vendor。

### 离屏 GLES 已通过，AHB 路径出现 GPU hang

2026-10-28，新增 `build-quest2-gles-probe.sh`，直接链接私有 EGL/GLES；真实 pbuffer + FBO，拒绝 Mesa/软件渲染器。初次 EGL_BAD_ALLOC 定位到两个接口缺口：Android EGL 子驱动使用 QTI mapper3（不是 Vulkan 的 mapper4）；原始 GSL 启动强制 ion_open，而本机仅有 DMA-heap。增加进程局部 mapper3→mapper5 对象，沿用实际 metadata 的 import/free 和线性 RGBA 限制。另一个私有适配器用真实 `/dev/dma_heap/system` 控制 FD 实现 ion_open/close；**ion_alloc_fd 仍明确返回 ENOTSUP**，没有假装完成 ION 分配/安全堆/缓存语义。

此时 EGL 初始化成功。最初着色器阶段崩溃出现在加载系统 libllvm-qgl.so 后；加入相同 donor 的 libllvm-qgl.so 后，该隔离探针通过全部三帧、12,288 像素比较。实际报告 Qualcomm EGL 1.5 / Adreno (TM) 680 / OpenGL ES 3.2 V@0819.0，GIT 274d25476a，日期 04/19/25。该成功仅覆盖普通纹理 FBO，不是 AHB 或桌面通过。

`--ahb` 对照：元数据查询、EGLImage 导入和 framebuffer 检查未报错，但 glReadPixels 留下初始化的 0x5a 数据，CPU 映射首像素为零。设置旧 QTI bit3（PRIV_FLAGS_USES_ION）没有改善，已经撤回。后续加入上下文核对，当前 context 与预期相同；strace 6.15 显示实际 PRIME_FD_TO_HANDLE 和 GEM_INFO 成功，GEM_SUBMIT 后 MSM_WAIT_FENCE 返回 ETIMEDOUT，随后探针停在等待中。工具只跟踪私有进程，未改变内核或系统属性。

之后内核出现 GPU hangcheck/recovery；最后一次可读取的 offending task 为原 Mesa 3DMark UI 的 RenderThread，而非直接标注探针，**不能仅靠该行归咎这个 UI 进程**。框架 PID 当时仍相同，随后 Activity 服务报 Broken pipe，SSH reset/超时。用户确认黑屏/卡住并正在手动重启；没有证据把手动恢复说成板子自主重启。原正常镜像仍为 Mesa，未持久部署任何闭源库。设备恢复前停止 GPU 提交；AHB 原始跟踪在忽略的 `out/quest2-gpu/gles-v77/ahb.strace`。

构建顺序：fetch-quest2-mapper-sources → generate-quest2-mapper-headers → prepare-quest2-gles → build-quest2-gles-probe。新增固定公开 mapper3 定义，mapper4 独立构建仍通过，不覆盖已部署运行库。私有目录及哈希清单准备后，从 retained initramfs root 执行 `run-quest2-gles.sh` 默认只运行离屏；`--ahb` 是已知可能导致 GPU hang 的失败复现，不能作为例行 smoke 或并发测试。加载器 trace preload 仅用于失败诊断，不进入正常测试路径。此脚本新增后尚未整体实机运行，等价的单条 probe 命令已执行如上。

## 后续范围

手动重启后已重新连接：boot ID 改变，boot_completed=1，新 system_server/SurfaceFlinger PID 可读；默认 Vulkan 完整哈希恢复/保持 `0b36eeba…`，screen-only=Y。pstore 为空，本次启动的筛选日志未见 hangcheck/fault。重启后没有再执行闭源 GPU 负载。离线对比原 libgsl 与 v819.1 notgsl 的全局导出名称集合一致，但这不证明内部结构 ABI 一致；后者此前已通过 Vulkan AHB，是下一步对照候选，尚未混用到 GLES。

原 mapper4 桥接在加入条件编译的 mapper3 代码后重建，SHA256 仍为 `3c3e04a3e1a6e1ab19c60f3b6814d1da2f68ffd278fba7b4da7fc3240a07d54f`，逐字未变。新脚本通过 shell/Python 语法检查；mapper3 和原 mapper4 均构建通过。诊断 strace 来源为官方 https://strace.io/files/6.15/strace-6.15.tar.xz ，本地归档 SHA256 `8552dfab08abc22a0f2048c98fd9541fd4d71b6882507952780dab7c7c512f51`；源码和静态产物只在忽略缓存中。

完整系统替换仍未完成：先解决共享图像的导入/地址/同步及 GPU hang，再覆盖多平面与窗口生命周期，然后接系统合成器和重启持久化。Vulkan 的 Wild Life 严重花屏也是独立未解决项，不能因 GLES 离屏通过便替换默认桌面。

## 最新：原配 GLES 的 AHB 五尺寸读回通过

### 当前进展：EGL 1.5 与 Android 系统加载器接入

`quest2-egl-wrapper.c` 现在同时代理 EGL 1.5 `eglCreateImage` 与 KHR 入口，包括 `eglGetProcAddress` 返回的函数。core 直接调用原配 donor 的 core 实现，完整保留 `EGLAttrib`，不截断为 EGLint。包装层与 donor 的错误状态按先后顺序返回并清除；同一个 native buffer 不能同时使用包装层和探针手工代理。

私有 preload 的 core/KHR × 直接符号/getProcAddress 四组各完成 150 轮：每轮两个 image 共享同一 AHB，主 image 提前销毁而纹理仍存活，三帧双重逐像素读回，随后引用 2→1→0。每组均有 150 次错误导入回收、150 次共享引用检查及 150 次全局清理零残留。错误测试使用有效 context 导入 native buffer，donor 返回 EGL_BAD_CONTEXT；另外检查本地 NULL 参数错误与 donor 错误的先后覆盖和一次性读取。最初用未知属性测试时，donor 接受了它，不能把上述结果写成全部错误语义/CTS 已通过。

`prepare-quest2-gles-linked.py` 先校验固定原配库及当前构建清单，再在新的 `out/quest2-gpu/gles-linked` 生成独立 ELF 依赖组合：包装库依次依赖 mapper3 桥接、ION 控制 FD 适配和原 EGL；探针改为依赖包装库，私有库设置 `$ORIGIN`。这个路径不再需要 LD_PRELOAD。它也通过 150 轮共享 image/错误释放/早销毁测试，450 帧。原配 GLES/GSL/编译器仍是 v77 同版本，ION 分配本身仍明确不支持。

随后直接验证 Android 自带加载器，未更改全局桌面：

- `quest2-egl-namespace.c` 先创建独立 mount namespace，把 `/` 设为递归 private，再仅在子进程的 `/vendor/lib64/egl` 挂 tmpfs。任一步失败即退出，不向 PID 1 传播挂载。
- `run-quest2-gles-system.sh` 复制已验证候选并为当前 freedreno 后缀建立私有入口，清除 LD_PRELOAD 与 LD_LIBRARY_PATH；不改属性或启动镜像。`gles-readback-system` 正常链接系统 libEGL/libGLESv2，确认实际 API 入口来自 `/system/lib64/`。
- EGL 报告 `1.5 Android META-EGL`；GL 报告 Qualcomm、Adreno 680、V0819.0。进程 maps 确认 vendor namespace 内的 EGL 包装库、原配 EGL/GLES/GSL/编译器以及桥接实际加载，不能只凭渲染器字符串推断成功。
- KHR 单轮、core 同一进程 150 轮（450 帧）通过。core 150 轮每轮提前销毁 image，绘制后由最后的 native-buffer 回调释放映射，共 150 条 remaining=0；这条系统路径没有调用探针的全局映射清理钩子。
- 经系统加载器，13×7、257×129、1920×1280、2880×1920 另各三帧双重逐像素检查通过，普通非 AHB pbuffer/FBO 三帧也通过。连同 64×64，覆盖此前五尺寸；不是覆盖所有格式。

该次 ELF-linked 包装库 SHA256 为 `8326cb237ffe54dad2513d0db5edb7db1384671f4a17d5206a4b805900a9247d`，系统探针为 `1907a76b620647b972e325ca3b122a5c700ac792b56ace928bd3d02d7ece9cba`。测试结束 boot ID 与两个框架 PID 保持，screen-only=Y，默认 Mesa EGL 为 `f16302e3…`、Vulkan 为 `0b36eeba…`，均未变化；GPU/GMU fault/hang/timeout 日志筛选无输出。原始输出仅在忽略的 out/quest2-gpu/gles-linked 和 gles-v77。

复现：构建 `build-quest2-gles-probe.sh` 后，在 WSL 执行 `python3 scripts/prepare-quest2-gles-linked.py`（拒绝覆盖已有候选）。复制 gles-linked、gles-v77 下的系统探针/namespace helper/SYSTEM_PROBE_SHA256SUMS 和运行脚本至脚本指定的 `/data/local/tmp/sq2-closed`，确认两个可执行文件为 0755。在 retained initramfs root 执行：

```sh
SQ2_GLES_CORE_IMAGE=1 SQ2_GLES_DESTROY_IMAGE_EARLY=1 SQ2_GLES_REPEATS=150 \
  sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-gles-system.sh --ahb
```

**尚未完成完整系统替换。** 这里只证明系统 EGL 分派与 vendor linker namespace 下的线性 RGBA 离屏导入、绘制和释放。ANativeWindow dequeue/queue/交换链生命周期、多平面视频、SurfaceFlinger 桌面、普通应用覆盖、持久化镜像及重启仍待完成；不能据此把候选提升为正常入口。下文旧的 preload-only、core 未实现与 AHB 失败描述是历史检查点。

### 后续：独立 EGL KHR 导入包装库

`quest2-egl-wrapper.c` 将 native-buffer 代理接入导出的 eglCreateImageKHR 和包装的 eglGetProcAddress。其他查询转发原配驱动；包装层拒绝的输入通过线程本地 eglGetError 报错，不将不支持的 native handle 静默传给原始强转回退。当前是私有 preload，不是完整可供系统加载的 EGL HAL：EGL 1.5 core 导入、窗口创建/交换路径、全部错误语义和多线程均未完整覆盖。

构建脚本生成 libquest2-egl-wrapper.so 并纳入 PROBE_SHA256SUMS，runner 的 `--egl-wrapper` 显式选择该实验。实机未启用探针的 SQ2_GLES_NATIVE_PROXY；仅包装库创建/持有代理。先通过 eglGetProcAddress 获取 KHR 入口，150 次早销毁图像后的绘制/双重读回/拆除全部通过；再用 SQ2_GLES_DIRECT_CREATE=1 经 dlsym(RTLD_DEFAULT) 找导出入口，同样150次通过。实际入口路径指向私有包装库，映射在最终 native-buffer dec 后释放，拆除时零残留；两组退出0，框架PID保持。输出仅在忽略的 wrapper-proc-150.txt、wrapper-direct-150.txt。

复现：`SQ2_GLES_DESTROY_IMAGE_EARLY=1 SQ2_GLES_REPEATS=150 sh /tmp/run-quest2-gles.sh --egl-wrapper --ahb`。不要再同时打开探针的手工代理，否则会重复包装。此步骤将生命周期逻辑从探针移到独立模块，但仍未替换系统驱动；下一步需要系统 EGL 加载方式、core 入口与 ANativeWindow 路径统一接入。

### 后续：native-buffer 引用驱动的映射释放

`quest2-native-buffer-lifetime.h` 新增私有 ANativeWindowBuffer 代理，复制而不改写原对象；仅接受本目标实测的 ARM64 magic/168 字节 ABI。代理持有原对象引用，将自身原子 inc/dec 回调交给 donor，最终 dec 后先释放该缓冲区的桥接映射引用，再释放原对象。原对象存活期间 FD 有效，映射以实际 dev/ino/size 识别。新增 mapper3 retain/release 接口，只有最后一个代理引用释放时才 munmap；没有修改 mapper4 路径。

实测 `SQ2_GLES_NATIVE_PROXY=1 SQ2_GLES_DESTROY_IMAGE_EARLY=1 SQ2_GLES_REPEATS=150`：先销毁 EGLImage，纹理仍绘制三帧并通过 GL/AHB 双重读回，之后删除纹理触发最终 native-buffer dec 和映射回收。再加 `SQ2_GLES_SHARED_IMAGE=1` 创建同一个 AHB 的两个独立 EGLImage/代理，150 轮均断言映射引用先为 2，删除原图像和纹理后仍为 1，销毁另一图像后为 0。三种断言/记录（refs=2、refs=1->0、完整 EGL 拆除时 mappings=0）各实际出现 150 次。最后进程退出 0、框架 PID 不变，boot ID 保持且默认 Mesa 哈希和 screen-only 正常。原始输出在忽略的 native-proxy-early-150.txt、native-shared-150.txt。

这次回收发生在驱动最终 native-buffer 引用回调，不依赖 EGL 全局拆除钩子兜底。但是代理目前仅通过私有读回探针显式使用，尚未接到通用 EGLCreateImage/eglGetProcAddress/窗口交换路径。**同一缓冲区不能混合未经代理和经过代理的活动使用来推断安全释放**；系统集成必须覆盖全部导入与窗口路径。共享 contexts、并发、多平面、同一个 context 长期 churn、失败导入的错误注入仍待验证；未把这一实验作为已完成的系统桌面替换。

### 后续：同进程 150 次完整 EGL 生命周期

新增仅 mapper3 导出的 `sq2_qti_mapper3_reset_addresses`，在调用方已经销毁全部纹理、EGLImage、surface/context、解除 current 并成功 eglTerminate 后，解除该桥接缓存的真实 CPU mmap 并清零统计。此钩子不能在单个 EGLImage 销毁时调用，不能与其他存活图像/上下文并发使用，也未自动安装到系统 EGL。

GLES 探针新增 `SQ2_GLES_REPEATS=1..200`。每轮独立创建/销毁完整 EGL 状态，严格检查 context 销毁和 terminate 成功后调用释放钩子；原 Mesa 编译路径不查找此私有函数。实机 `SQ2_GLES_REPEATS=150 sh /tmp/run-quest2-gles.sh --ahb` 完成同一进程 150 次循环、450 帧双重像素读回，150 条释放记录均为一个 16384 字节映射，退出 0，框架 PID 保持。输出在忽略的 `out/quest2-gpu/gles-v77/lifecycle-150-checked.txt`。

这证明完整 EGL 拆除边界能回收映射，不等于长期桌面所需的逐图像/纹理引用计数已经实现。系统 EGL 包装层仍需跟踪共享 EGLImage、纹理引用及窗口生命周期，避免 image 已销毁但 texture 仍引用时过早释放。旧五尺寸矩阵的二进制哈希是历史检查点，本次重建产物以本地 PROBE_SHA256SUMS 为准。

2026-10-28，继续以同一 v77 EGL/GLES、原 GSL 和原编译器组合为主线，未混用 v819 GSL 来取得下列结果。正常系统和启动镜像仍为 Mesa。先在私有进程阻止 GPU 提交并读取命令，再依据真实错误逐项修复。

### 已定位并修正的三个接口问题

1. `getRgbDataAddress` 未实现时，eglSubDriverAndroid 的回退直接读取 native_handle+84，假设那里是 QTI CPU 基址；本机 minigbm 实际为宽高，64×64 时是 `0x4000000040`。产生的偏移经 49 位掩码后，将实际 IOVA `0x100047000` 变成命令中的 `0x1ffc100046fc0`，与捕获完全吻合。mapper3 现在对验证过的线性 RGBA 缓冲区返回真实 mmap 地址；两次地址查询一致，偏移回到 0，resolve 地址恢复为真实 IOVA。没有伪造 GPU ID、IOVA、私有 flags 或元数据 blob。
2. 地址修复后，GPU→AHB CPU 的三帧像素检查通过，但 glReadPixels 仍留下 0x5a。确认 donor 调用了未实现的 mapper3 lock/unlock。直接将原 AHB handle 传给 mapper5 lock 又返回 BAD_VALUE；现为每次 CPU lock 创建独立 mapper5 import，保留至匹配 unlock 后再 free。真实 metadata 返回 bpp/stride，acquire fence 复制后转交 mapper5，release fence 在 HIDL 回调期间借用。互斥锁不跨越 fence 等待，活动锁数量有界；非空 fence 和并发锁尚未独立压力验证。
3. 64×64/全屏通过后，13×7 AHB 像素仍错误，普通纹理的同尺寸正确。minigbm 的 height、plane heightInSamples 是逻辑高度，但 QTI getHeight/getCustomDimensions 描述分配高度；msm 的 RGB 分配实际将高度对齐至 64。mapper3 改用经过整除与 INT32 范围检查的 `plane_size / stride`，getUnalignedHeight 和 ANativeWindowBuffer 仍保留逻辑高度。之后非对齐尺寸也通过。

这些变化仅在 SQ2_QTI_MAPPER3 编译分支启用。原 mapper4 重建 SHA256 仍为 `3c3e04a3e1a6e1ab19c60f3b6814d1da2f68ffd278fba7b4da7fc3240a07d54f`，没有改变既有 Vulkan 桥接。最终私有 mapper3 SHA256 为 `3cc5096eb8981cdebb8e3501adb88faa50932e54c8f353c12a142e64f3353e44`，GLES 探针为 `0978f0e328a4326d1ac5083c7aea612ba95d0f7e37edc37c20a163271bdf7f29`。

### 最终实机矩阵

每项三帧，着色器输出四象限及交替蓝通道；逐像素比较 glReadPixels 的期望结果，再检查 AHB CPU lock 得到的图像与之相同。不是只检查导入或单个首像素。

| 尺寸 | 三帧像素总数 | 结果 |
| --- | --- | --- |
| 64×64 | 12288 | 两条读回均通过 |
| 13×7 | 273 | 两条读回均通过 |
| 257×129 | 99459 | 两条读回均通过 |
| 1920×1280 | 7372800 | 两条读回均通过 |
| 2880×1920 | 16588800 | 两条读回均通过 |

合计 15 帧、24073620 个像素。所有进程返回 0，boot ID 和 system_server/SurfaceFlinger PID 保持；最终筛选 GPU fault/hang 日志无输出，screen-only=Y，默认 Mesa Vulkan 完整哈希保持 `0b36eeba…`。原始跟踪与测试输出仅在忽略的 out/quest2-gpu/gles-v77。

复现为 retained initramfs root 执行 `SQ2_GLES_WIDTH=257 SQ2_GLES_HEIGHT=129 sh /tmp/run-quest2-gles.sh --ahb`；先同步当前构建的库/探针/清单。默认尺寸 64，范围 1–4096，未宣称范围内所有尺寸均已测试。`--ahb-cpu-readback` 是独立 CPU 路径诊断，输出明确注明未覆盖 glReadPixels。

### 诊断和系统接入边界

`quest2-drm-trace.c` 为私有 preload，记录 PRIME/GEM_INFO/submit 参数；`SQ2_DRM_TRACE_NO_SUBMIT=1` 拒绝被识别的 MSM 提交并有界读取第一层/间接命令。该模式下 readback 失败是预期结果，不能计为渲染通过。发现 import-only 的资源清理阶段也会尝试 GPU 提交，因此不能把它描述为零 GPU 命令保证。诊断库不进入常规 runner、应用或镜像。

真实地址映射目前缓存至进程结束，限制 128 个/512 MiB，只适合独立实验，**尚未具备系统长期运行所需的 EGLImage 生命周期释放**。仍只支持已核对的单层、单平面、无压缩/保护的 RGBA8888；窗口交换链、多平面视频、其他颜色格式、SurfaceFlinger、重启持久化和完整复杂场景验证尚未完成。不能把本矩阵等同于闭源系统替换完成。

Vulkan 并行进展见 [QUEST2-VULKAN-DIRECT.md](QUEST2-VULKAN-DIRECT.md)：独立 Direct 配置读取与基础读回通过，Wild Life 尚待新一轮图像正确性验证。

## 同一固件配套参考与布局对照（2026-10-28 后续）

用户要求从 Quest 2 固件提取整套 GPU 驱动并直接参考实现。当前主参考统一为上述 v77 vendor，而不是把第三方 Vulkan 重打包与任意 GLES 组合当成原厂整套。`configs/quest2-v77-graphics-reference.json` 新增固定原配 Vulkan、mapper3 实现、gralloc.kona、gralloc core/utils、qdMetaData 及 QTI 接口库哈希；连同既有 EGL/GLES 清单共 18 个文件。它们都来自同一个已校验 vendor 分区。尚未认证 OEM 签名，主机侧抽取文件也不等于这些 HAL 已能在 Surface 加载运行。

可复现提取与审计：

```sh
python3 scripts/rebuild-quest2-v77-vendor.py \
  downloads/quest2-gpu/q2_51584880050400150.zip.part \
  downloads/quest2-gpu/q2-v77-vendor.compressed.part \
  out/quest2-gpu/layout-reference/vendor-rebuilt.img
python3 scripts/inspect-quest2-graphics-reference.py
bash scripts/build-quest2-layout-reference.sh
```

重建脚本只接受新建的工作区 out/*.img，拒绝覆盖；固定 full vendor 身份，逐项检查压缩哈希、解压长度、目标 extent 不重叠且全覆盖和最终分区哈希。此次重新执行 131 个操作通过，产物与原分区 SHA256 相同。lib64 仍用 debugfs 从该参考 ext4 镜像只读提取；不把 OTA、分区或整套 HAL 刷到 Surface。审计脚本先校验全部库哈希，再保存 DT_NEEDED 和指定函数反汇编到忽略的 out/quest2-gpu/firmware-reference。

### 固件实现已经确认的细节

- 原 mapper3 的 getPrivateFlags（VA 0xe2b0）读取 QTI handle +24 的 flags；getCustomDimensions（0xd500）调用原配 gralloc 计算。Surface 的 minigbm handle 不能按 QTI 结构直接强转。
- getSurfaceMetadata_V1（0xea80）调用 getMetaData(handle, 16, destination)。qdMetaData 的该分支在元数据有效时复制 16384 字节；这不是“随便补一个尺寸结构”就能满足的接口。
- adreno_utils 的 adreno_get_metadata_blob_size（0x10eb8）返回 5528。grallocutils 的资源布局路径调用 adreno_init_memory_layout 和 adreno_get_aligned_gpu_buffer_size；**这个 5528 字节布局 blob 与上述 16384 字节 surface metadata 不是同一契约**。未把它伪造成 getSurfaceMetadata_V1 的成功输出。
- 原 GSL 导入路径使用 16 字节 GEM_INFO，请求 0xc0106443；v819.1 GSL 使用 24 字节 0xc0186443。本机内核结构含末尾 len/pad。这是已识别的 ABI 差异，不是已证明的故障根因；两者旧记录中的 PRIME 导入和地址查询均曾返回成功。

### 运行原配布局计算，未执行 EGL 绘制

`quest2-layout-reference.cpp` 直接调用已固定的 v77 adreno_utils（实际库路径已打印），保留同版本 GSL，仅使用原有私有 ION 控制 FD 适配。通过当前系统 AHardwareBuffer/minigbm 分配五种尺寸，调用原配函数计算线性 RGBA8、单层/单采样布局，usage=0x333。没有创建 EGL context、没有调用绘制 API，也未更改系统分配器。

| 尺寸 | donor 对齐尺寸 | donor 所需字节 | minigbm 行跨度（字节） | 像素 DMA-BUF 字节 |
| --- | --- | --- | --- | --- |
| 64×64 | 64×64 | 16384 | 256 | 16384 |
| 13×7 | 64×7 | 4096 | 256 | 16384 |
| 257×129 | 320×129 | 167936 | 1280 | 245760 |
| 1920×1280 | 1920×1280 | 9830400 | 7680 | 9830400 |
| 2880×1920 | 2880×1920 | 22118400 | 11520 | 22118400 |

五项布局初始化均返回 0，offset 均为 0，探针完整退出；boot ID 和两个框架 PID 未变。因此这些尺寸不支持“像素分配长度小于 donor 所需长度”的解释，尤其失败的 64×64 尺寸两者完全相同。但这不验证实际 GPU 地址、缓存一致性、命令中的布局字段、隐式/显式 fence 或多平面兼容，也不是 AHB 渲染通过。

### 另外保留的混合 GSL 对照

`prepare-quest2-gles.py --gsl-dmabuf` 只在独立 gles-v819-dmabuf 目录中用固定 v819.1 notgsl/notdmabufheap 替代 GSL；原 v77 库不改。`run-quest2-gles.sh --gsl-dmabuf` 离屏三帧 12288 像素通过；再加 `--ahb-import-only`，完成 AHB/EGLImage/FBO 创建和释放，返回专用 no-draw-no-readback 标记。这两次 boot ID/框架 PID 保持。

这是诊断对照，不是同版本原厂栈，更没有证明 AHB 绘制或系统合成通过。本轮未在此组合执行 `--ahb` 绘制，也未原样重试会挂死的原 v77 AHB 路径。正常启动保持已验证 Mesa；后续以配套固件的地址/布局/同步实现为依据继续定位。

离线线索：完整 donor `ioctl_drm_device_getinfo_ext`（VA 0x20bc20）存在 chip 分支，对真实 0x06080001 打印 Unknown GSL device info properties，但这不是其已经导致 hang 的证明，不能伪装为 A650。v819 notgsl.so 的 get_panel_settings（VA 0x206a20）调用 parseFile（0x1f4538），依次尝试 adreno_config.txt、yamato_panel.txt，再查询 YAMATO_FORCE_GMEM_SHL。尚未确定配置字段/路径如何控制 Vulkan sysmem/GMEM；不凭字符串猜测或宣称有效。现有 v819 GSL 使用 DMA-BUF 分配接口，完整 v77 GSL 使用旧 ION 接口，二者 ABI/导入实现的比较待继续。
