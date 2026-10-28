# Quest 2 v77 同版 Vulkan 私有候选

状态：**同版 v77 已通过私有系统加载器的基础计算、optimal image 和八帧三角形读回；AHB 属性查询失败，尚未进入 AHB 绘制。** 本文的候选不是已通过 Wild Life 的 v819.1 Direct 包，也不能继承后者的实测结果。正常系统继续使用原 Mesa，未修改启动镜像、内核或固件。

原件来自已校验的同一个 Quest 2 v77 `vendor.img`。分区和库的固定 SHA256 证明内容与本项目下载/提取清单一致；**未完成 OEM OTA 签名认证**。提取来源、完整显示参考和接口边界见 [QUEST2-VENDOR-REFERENCE.md](QUEST2-VENDOR-REFERENCE.md)。

## 最小私有组合

`scripts/prepare-quest2-v77-vulkan.py` 生成七个库：五个同版 donor，加当前项目的 mapper3 和 ION 控制适配库。原件路径为 `out/quest2-gpu/q2-v77/lib64/`，以下哈希均在复制前、生成后重新核对。

| 原件相对路径 | SHA256 |
| --- | --- |
| `hw/vulkan.adreno.so` | `0f316d0331ef5b4ec60d8d3f21a845f47583b14ccaabe54f316e619b9897bd8d` |
| `libgsl.so` | `8971ade1009b02ca49a19cde9a78e324b2f7b868253f614c337105bbb669cfb6` |
| `libadreno_utils.so` | `8bca58979c908b1da4b7caf064c65a5b72eb8ae6989a316e63611b2c1597eb57` |
| `libllvm-glnext.so` | `64054b91e804c2e021e28bc1225b205cf754a633850c577db445132ed4ea29fb` |
| `libllvm-qgl.so` | `e52264451deb74c86e728b3559b99267eb37b1b0a1973f578d584707767ae49a` |

适配器来自 `out/quest2-gpu/gles-v77/PROBE_SHA256SUMS`，按当前构建清单验证，不把旧构建当成永远固定的二进制。此次生成时为：

| 适配器 | 输入 SHA256 |
| --- | --- |
| `libqti-mapper3-bridge.so` | `4066a9c09a9d12d54e18c7ce48147f8b09fd1e71ee8bbf8a800b7a52a1b1d020` |
| `libquest2-ion-dmaheap.so` | `9dab9575f77905a33fa7a7ab240f14a35324758fee569831f54b6e11960ae701` |

这是私有 GPU 库的最小候选，不是完整 Android ELF 依赖闭包。Android 平台的 libc/libc++、HIDL、libnativewindow、QTI/标准 mapper 接口库仍由测试命名空间提供。设备实测已确认该组合能通过系统加载器执行基础负载；这不代表全部可选扩展依赖均已覆盖。

## 与 v819.1 包的差异

| 项目 | 同版 v77 原件 | 已有 v819.1 实验包 |
| --- | --- | --- |
| GPU 库名 | `libgsl.so`、`libadreno_utils.so`、`libllvm-glnext.so` | `notgsl.so`、`notadreno_utils.so`、`notllvm-glnext.so` |
| mapper 直接依赖 | 标准和 QTI mapper2/3；没有 mapper4 | mapper2/3/4 |
| 桥接构建 | `SQ2_QTI_MAPPER3` / `libqti-mapper3-bridge.so` | mapper4 / `libqti-mapper-bridge.so` |
| 内存接口 | 原配 GSL 依赖 `libion.so` | 新版 `notgsl` 与 `notdmabufheap` 组合 |
| 额外平台依赖 | Vulkan 不直接依赖 `libbase.so` | Vulkan 直接依赖 `libbase.so` |
| 配置目录槽文件偏移 | `0x57277` | `0x6e6ef` |
| 已有 Direct 跑分 | 没有 | 独立 v819.1 实测，不能迁移结论 |

不能把 v819 的改名 GSL、DMA-heap 库或 mapper4 桥接直接塞入 v77 同版候选。相同版本字符串附近可能出现 `0819.0`，但它不是二进制相同或可互换的证明；版本识别以原件哈希和 ELF 依赖为准。

v77 Vulkan 的原始 `DT_NEEDED` 为：

```text
libcutils.so                         libz.so
libadreno_utils.so                   libgsl.so
liblog.so                           libllvm-glnext.so
libhardware.so                      libnativewindow.so
vendor.qti.hardware.display.mapper@2.0.so
android.hardware.graphics.mapper@2.0.so
vendor.qti.hardware.display.mapper@3.0.so
android.hardware.graphics.mapper@3.0.so
vendor.qti.hardware.display.mapperextensions@1.1.so
libhidlbase.so                       libutils.so
libc++.so                           libc.so
libm.so                             libdl.so
```

这些 mapper2/3 链接依赖仍保留；mapper3 桥接额外依赖 mapperextensions1.0。当前桥接将实际 minigbm/mapper5 元数据翻译为 donor 接口，并不安装 Quest 的 mapper 服务或替换全局 mapper。现有 GLES 的桥接验证只说明那些 GLES 路径通过，Vulkan 使用的 AHB usage、format、fence、buffer 生命周期还需要单独覆盖。

## 动态库和 profile 的核对

只有 `DT_NEEDED` 不足以决定打包内容：

- `libllvm-glnext.so` 的 `libllvm-qgl.so` 字符串在文件偏移 `0x1fa0b`；VA `0xd1260–0xd1274` 将其传入加载函数，后者在 `0x656d8` 调用 `dlopen`。因此必须带同版 `libllvm-qgl.so`，不能因为它不是 Vulkan 直接 NEEDED 就省略。
- Vulkan 对 `libadreno_app_profiles.so` 的调用在 `0x121090` 做 `dlopen`；`0x1210a4` 查找 `ApplyApplicationProfile`。返回值保存到 x8，在 `0x1210ec` 判空，非空才在 `0x121104` 调用。
- 默认候选只把文件偏移 `0x4ee5c` 的唯一 `ApplyApplicationProfile\0` 等长改成 `Skip_ApplicationProfile\0`，复用原件已有的空回调分支。未改任何指令。`--keep-optional-profile` 可生成另一个保留原查找名的对照候选。
- 这不是“删除全部 profile 代码”：另有 `AppProfDestroyStruct` 查找，`dlopen` 失败和 `dlsym` 失败均有分支；例如 `0x11e914/0x11e918` 和 `0x11e928/0x11e92c`。候选不供应其他版本的 profile 库。
- 另见 `libkernelmanager.so`（`0x2ce288` 调用 dlopen 后 `0x2ce290` 检查失败）和 `libVkLayer_ADRENO_qprofiler.so`（`0x2507e4` 调用 dlopen 后 `0x2507ec` 分支）。它们不是初始 ELF 加载必须满足的 NEEDED；不把存在错误处理等同于所有相关扩展都可正常工作。

完整 v77 vendor 普通文件清单中没有 `libion.so`、`libadreno_app_profiles.so`、`libkernelmanager.so` 或 `libVkLayer_ADRENO_qprofiler.so`。这只证明当前 vendor 分区中没有，不能推断同版 system/odm 完全没有；后续若需要原厂对应路径，应从同版其他分区补齐，不混用另一固件库。

## 允许的候选变化

候选 Vulkan 文件和 SONAME 为 `vulkan-v77-bridge.so`。新增首个 `DT_NEEDED` 为 mapper3 桥接，默认所有七个输出库加 `$ORIGIN` RUNPATH。`--system-loader` 变体统一改为固定 `/vendor/lib64/egl`，只供私有 mount namespace 将七库放入该目录、再将 HAL 绑定至原系统 Vulkan HAL 路径使用。它避免 HAL 绑定到 `hw/` 后 `$ORIGIN` 按绑定目标目录解析的问题；不是全局 vendor 安装器。脚本不接受任意自定义 RUNPATH。

其余 donor 的 SONAME 不变，所以必须使用全新进程或专门的链接器命名空间；不能在已经加载不同版本 `libgsl.so` 的进程中依赖 RUNPATH 把它替换掉。

原配 GSL 动态表只导入三个未版本化 ION 符号：`ion_open`、`ion_close`、`ion_alloc_fd`。脚本核对导入集合和适配器导出后，仅把**复制件**的 `DT_NEEDED libion.so` 改为 `libquest2-ion-dmaheap.so`。这是与先前 GLES 包的 preload/先加载 ION 做法不同的新链接组合，后续基础负载实测见文末。它避免让 GSL 偶然调用平台的旧 ION 控制路径，但不是通用 ION 分配实现：

- `ion_open` 返回 `/dev/dma_heap/system` 的控制 fd。
- `ion_close` 关闭该 fd。
- `ion_alloc_fd` 明确返回 `-ENOTSUP`，不能制造一个貌似成功的分配。

所有 GPU 内存分配/导入仍需核对实际走到原配 GSL 的 MSM/DRM 路径；不能因为依赖加载成功就认为内存语义正确。不能改用 v819 GSL 来绕过一个失败后继续称为“同版栈”。

脚本保存每个原件和候选的依赖/SONAME/RUNPATH、原件/输出哈希和可执行 section 哈希。所有输出库的可执行 section 必须与各自输入完全一致。`patchelf` 会改变 ELF 元数据及布局，因此候选整体 SHA256 改变是预期的，原件仍保留原始哈希。

## 私有 Direct 配置

v77 配置目录指针在 VA `0x11f0e0–0x11f0f8` 初始化，对应文件槽 `0x57277`；文件名 `qgl_config.txt` 在 `0x57289`，随后在 `0x11f11c–0x11f174` 拼接。脚本只将这个已核实目录槽等长重定向为 `/data/sq2vk-v77t/`。其余同名目录字符串保持原样，不能全局字符串替换。

原版读配置仍存在其他路径和属性来源；私有目录重定向只控制该已证明的路径，不是隔离系统全部驱动属性的证明。实机验证需要记录确实打开的配置文件及当前属性。

`--direct` 额外输出：

```ini
0x0=0x08675309
RenderMode=Direct
```

依据是 v77 VA `0x11f614–0x11f62c` 的头值比较/启用，`0x2725c4/0x2725cc` 构造的 RenderMode 键 hash `0x1fd41fd8`，以及文件 `0x313640` 开始的枚举表：Direct hash `0x1e3dd538` → 0，HbVizBinning → 1，Binning → 2，HwVizDirect → 3。它与 v819 语义对应，但不能复用 v819 字节偏移。GSL 的 `adreno_config.txt`/`yamato_panel.txt` 是另一套机制，未在此候选中更改。

无配置和 Direct 两个候选的七库哈希完全一致，只有后者多一个 `qgl_config.txt`。运行无配置对照时必须确认私有目录没有前一次 Direct 配置；不能仅换库目录后保留旧配置。

## 离线重建

在 Linux/WSL 仓库根目录运行，安装 `patchelf` 和 `readelf`，并先完成同版原件提取与当前 GLES 适配器构建。脚本拒绝覆盖已有输出，只允许在本仓库 `out/` 下创建与输入分离的新目录。

```sh
python3 scripts/prepare-quest2-v77-vulkan.py
python3 scripts/prepare-quest2-v77-vulkan.py --direct \
  --output out/quest2-gpu/v77-vulkan-direct-runtime
python3 scripts/prepare-quest2-v77-vulkan.py --system-loader \
  --output out/quest2-gpu/v77-vulkan-system
```

此次实际输出：

- `out/quest2-gpu/v77-vulkan-runtime/`：七库，无配置文件。
- `out/quest2-gpu/v77-vulkan-direct-runtime/`：同样七库，加 Direct 配置。
- 两者 `vulkan-v77-bridge.so` SHA256：`19421cce6a1997150317d2f1cd080f767892aa41c49b3737575e5af8acb0ccab`。
- `out/quest2-gpu/v77-vulkan-system/`：七库固定 `/vendor/lib64/egl` RUNPATH，无 Direct 配置；HAL SHA256 为 `fa62ab04fa11642b7717c49d8be13679ca817ae330ed12583b496d7a1793b916`。
- 每个目录的 `v77-vulkan-manifest.json` 和 `SHA256SUMS` 是本次候选记录。适配器重建或 patchelf 版本变化可能改变输出哈希，应重新核对清单。

准备阶段已完成三种候选的原件哈希、ION 符号集合、依赖重写、SONAME、RUNPATH 和可执行 section 检查。准备脚本本身不执行 ARM64 代码，不操作设备。后续实测与阻断点见下节；任何阶段失败都应保留正常 Mesa 和 screen-only 保护。

## 系统加载实测与 AHB 阻断点

主线程在独立 mount namespace 内，将系统变体的七库置于私有 `/vendor/lib64/egl`，仅在该 namespace 将 HAL 绑定到 `/vendor/lib64/hw/vulkan.freedreno.so`。实际映射匹配 `fa62ab04…` 候选及同版 GSL，系统报告 Adreno 680、chip `0x06080001`、Vulkan 1.1.295、driverVersion `2150838272`。没有启用 Direct 配置。

`out/quest2-gpu/v77-vulkan-system/ahb-first.txt` 记录：

| 检查 | 结果 |
| --- | --- |
| 16 轮 × 256 words 计算读回 | 通过 |
| 64×64 optimal RGBA clear/copy/readback | 通过 |
| 64×64 八帧 vertex/fragment 三角形、交替 clear、内外像素 | 通过 |
| AHB 属性 | 返回 `format=0 external=0 size=0 types=a features=20000`，探针按预期拒绝 |
| AHB create/import/bind/GPU 绘制 | 此次尚未执行 |
| boot ID、框架 PID、默认 Mesa | 前后保持 |

这说明原配 GSL/ION 控制链接组合可初始化并提交基础负载，不能证明 AHB 导入、窗口或整套系统可用。`features=0x20000` 是 midpoint chroma sample 位，不是 RGBA 已支持的证据。

### 为什么 mapper3 getter 没有收到查询

原件导出的 `qglinternal::vkGetAndroidHardwareBufferPropertiesANDROID` 位于 VA `0xdc120`，长度 2280 字节。离线反汇编显示它在 `0xdc170` 调用 `AHardwareBuffer_getNativeHandle`，把返回值保存为 x21；之后**直接按 QTI 私有 handle 布局取字段**，没有通过 mapper3 查询 format/size/flags：

| donor 指令 VA | 对 native handle 的直接访问 | donor 用途 | 当前 minigbm 同偏移字段 |
| --- | --- | --- | --- |
| `0xdc19c` | `uint32_t +72` | allocationSize，随后写入输出属性 +16 | `sizes[2]` |
| `0xdc22c` | `uint32_t +44` | QTI 格式，经 `0x1cd090` 转内部格式 | `strides[3]` |
| `0xdc250` | `uint32_t +24` | 私有 flags | `fds[3]` |

本机 minigbm 源码 `cros_gralloc/cros_gralloc_handle.h` 使用 packed 布局：12 字节 native_handle_t，五个 fd，四个 strides，四个 offsets，四个 sizes。线性单平面的 `strides[3]` 和 `sizes[2]` 不代表真实格式/总长度，通常为零，正好解释此次输出。真实 AHB desc 已被正常调用，但此函数只用其 usage 和 BLOB 特例，常规格式仍读私有字段。格式转换函数对 0 走未识别分支并返回 0，整个属性函数最后仍返回 VK_SUCCESS；因此探针拒绝 format0 是有效失败，不能放宽检查来“通过”。

这条路径不是因为现有 `getFormat/getSize` 返回错误，而是根本没有调用它们。单独修改 mapper3 getter 或加入 mapper4 不会覆盖这个缺口。

### 后续导入和释放也有直接布局依赖

不能只包装 Properties 输出为非零就继续绘制。原件还存在以下相关路径：

- `0x14b438` 获取 AHB handle，`0x14b474` 再直接读 +44 格式；fallback 调用 `0x1cd510` 构造图像布局。
- `0x1cd510` 的 handle 参数保存为 x24；`0x1cd5dc` 直接读 +28/+32 的两个尺寸，`0x1cd618` 读 +24 flags，`0x1cd86c` 读 +84 的 64 位 CPU base，`0x1cdb20` 读 +72 size。部分分支尝试旧 gralloc perform，但存在直接字段 fallback。
- 映射 helper `0x1cd3b0` 在 `0x1cd3d4` 读取 +72 size 与 +76 offset，`0x1cd3e0` 读取 +84 CPU base，`0x1cd3e8` 读取 +12 fd。没有 CPU base 时，可在 `0x1cd4c0` 尝试 mmap。当前 minigbm 的 +84/+88 是 width/height，绝不能把它们组合为一个 CPU 指针。
- `0x1f6f04` 获取 AHB handle 后经 `0x1f7178` 调用上述映射 helper；`0x1f71a4` 再获取并保存 handle。
- 释放路径 `0x1f585c` 获取 handle，`0x1f5864` 读取 +72 作为 munmap 长度，然后在 `0x1f5874` 释放原 AHB。临时栈上的 handle 视图或过早释放视图会破坏这一生命周期。

下一步应先实现私有、版本限定的 AHB handle 视图或对应 Vulkan 包装入口，以真实 mapper5 metadata/fd 为来源；只支持已核对的非保护、线性、单平面 RGBA，保持原 handle 与 AHB 本身不变。需要明确双向映射，避免 mapper3 把兼容视图当作原 minigbm handle 导入；需要为 fd、尺寸、格式、size、offset 和实际 CPU base 分别验证界限，不制造虚假地址。视图必须跟随原 AHB 和 Vulkan memory 引用生命周期，最后释放检查不能省略。

初期可先做“只查询属性且不提交 GPU”的对照，将原版与包装版结果和真实 AHB metadata 并排记录；随后才逐步验证 import/bind、真实双向读回、错误导入与重复释放。上面的访问列表是已确认的关键路径，不宣称已经覆盖全部 Vulkan WSI/多平面/受保护缓冲区实现。
