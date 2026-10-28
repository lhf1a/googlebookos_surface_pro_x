# Quest 2 闭源 GPU 驱动研究

2026-10-28，分支 `codex/quest2-proprietary-gpu`。用户授权开始研究移植。当前阶段为源码和接口审计，未替换 Surface 驱动、固件或启动镜像。沿用最新麦克风/Wild Life/温控/触摸/密码 SSH 基线。

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

下载位置：downloads/quest2-gpu/v819.zip、v690.zip；静态审计输出：out/quest2-gpu/v819-audit.json、v690-audit.json。尚未执行任何 donor 库，当前 Surface 部署未改变。
