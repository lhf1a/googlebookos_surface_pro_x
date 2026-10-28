# Quest 2 v77 Vulkan：AHB 与 native handle 生命周期审计

本页记录 2026-10-28 的离线审计证据与适配边界；后续实现及实机结果以 [QUEST2-V77-VULKAN.md](QUEST2-V77-VULKAN.md) 最新章节为准。离线调用图不能代替设备验证，也不代表已经完成WSI或系统替换。正常 Mesa、启动镜像保持。

## 固定输入与方法

- 原件：`out/quest2-gpu/q2-v77/lib64/hw/vulkan.adreno.so`。
- SHA256：`0f316d0331ef5b4ec60d8d3f21a845f47583b14ccaabe54f316e619b9897bd8d`。
- GNU Build ID：`179c7a3381683d680046ce544b76ef30`。
- 本页地址均为这个原件的 ELF VA，不是 ASLR 后地址，也不能套用 v819。
- `aarch64-linux-gnu-objdump -d -C` 输出在忽略目录 `out/quest2-gpu/vulkan-analysis/v77-ahb-lifetime.disasm`；函数边界同时核对 `.gnu_debugdata`。不提交 donor 二进制或原始反汇编。

覆盖范围是所有静态可见的 AHB API PLT 调用点，以及其关联的 Properties、memory 导入/导出/最终释放和 Android native-buffer 图像路径。虚函数目标按已恢复的对象表和具体调用点核对；不把这些路径的覆盖宣称为整库所有指针别名、全部扩展或所有错误注入均已验证。

## AHB API 的全部可见直接调用

| API / PLT VA | 调用 VA | 用途及返回值寿命 |
| --- | --- | --- |
| `getNativeHandle` / `0x2d43e0` | `0xdc170` | `vkGetAndroidHardwareBufferPropertiesANDROID`；handle 放入 x21，本次调用直接读字段 |
| 同上 | `0x14b438` | 绑定 AHB memory 后建立图像布局；handle 放入 x20 |
| 同上 | `0x14b524` | 同一绑定函数的厂商 surface-metadata 成功分支；再次取得 handle |
| 同上 | `0x1ce1cc` | 导入前 surface-metadata 验证 helper；handle 放入 x21 |
| 同上 | `0x1f6f04` | AHB memory 导入，返回值作为 `0x1cd3b0` 的 handle 参数 |
| 同上 | `0x1f71a4` | 导入后再次取 handle，`0x1f71a8` **保存到 memory+432** |
| 同上 | `0x1f585c` | 最终析构时重新读取 handle+72，作为自建 CPU 映射的 munmap 长度 |
| `acquire` / `0x2d4400` | `0x1f6e9c` | 导入持有：原 AHB 已在 `0x1f6e98` 存入 memory+312 |
| 同上 | `0xdefd0` | `vkGetMemoryAndroidHardwareBufferANDROID` 导出：新增给应用的一次引用 |
| `release` / `0x2d4ec0` | `0x1f5874` | memory 最终析构，释放 memory+312 中原 AHB |
| `allocate` / `0x2d4ee0` | `0x1f76c8` | driver 分配可导出的 AHB；成功后把初始引用保存 memory+312，无额外 acquire |
| `describe` / `0x2d43f0` | `0xdc194`、`0x14b458`、`0x1ce1dc`、`0x1ce1f8` | Properties、绑定、导入验证。最后一处是原库的空 AHB 分支 |

不能把两个 acquire 调用点等同处理：`0x1f6e9c` 的引用由 donor 析构 release 配对，`0xdefd0` 的引用交给应用，通常由应用调用平台 `AHardwareBuffer_release`。若只拦 donor 的 release，却把导出 acquire 也加到兼容视图计数，会泄漏视图。

### 私有重命名入口的 caller 分类

若仅将该固定 donor 的 AHB 导入符号改名到私有 bridge，下面是 `BL` 返回地址（LR 的模块内 VA），**不是 call 指令地址**。必须先确认调用模块和版本，再分类；未知来源不能套用这个表。

| getNativeHandle 返回 VA | 原 AHB owner 是否已经取得 | 视图要求 |
| --- | --- | --- |
| `0xdc174` | 本函数未 acquire，调用者拥有输入 | driver 不保存此指针；当前设计通过外层 prepare/finish 临时租约借用同一真实 view，不另建 TLS 假句柄 |
| `0x1ce1d0` | 是。静态调用来自 `0x1f6edc`，在导入 acquire 或成功 allocate 保存 +312 之后 | 返回该 owner 的持久 view；helper 本身只短期使用 |
| `0x1f6f08` | 是，同一已持有的 +312 | 返回持久 view，用于实际 GSL 导入/CPU 映射 |
| `0x1f71a8` | 是 | **必须持久**；紧接着保存到 memory+432 |
| `0x14b43c` | 是。`0x14b414–0x14b418` 从被绑定的 memory+312 取得 | 返回已有 owner 的持久 view；不能单独以绑定函数返回结束其寿命 |
| `0x14b528` | 是，同一 binding/AHB | 返回已有持久 view；这个返回值在当前分支没有后续有效读取，但不得据此为该调用制造另一套不一致 view |
| `0x1f5860` | 是，仍在最终 release 之前 | 必须仍能返回此前持久 view，供 munmap 长度检查 |

其余私有 API 的返回地址：导入 acquire `0x1f6ea0`；导出 acquire `0xdefd4`；最终 release `0x1f5878`；allocate `0x1f76cc`。两个持久 owner 来源分别是导入 acquire 和 allocate 成功的初始引用，不能只实现前者。Properties 临时租约不参加 driver owner 计数。最初考虑的无资源 TLS view 已由统一 prepare/finish 方案替代，减少属性查询与真正导入的差异。

## v77 原件确认的 QTI handle 布局

不能直接把缓存 `upstream/libadrenotools/tools/qtimapper-shim/ext/gr_priv_handle.h` 的旧版 108 字节结构视为 v77 ABI。原 Quest 2 `libgralloccore.so` 构造处 `0xa840` 分配 **124 字节**，validate `0x9380` 在 `0x939c/0x93a4/0x93b0` 分别检查 version=12、numInts=26、numFds=2；`0x93bc–0x93c4` 检查 magic=`0x676d736d`。原 `gralloc.kona.so` 的 `0xb030` 与 mapper3 的对应 validator 有相同检查。

| 字节偏移 | 字段 / 原版约束 |
| --- | --- |
| 0 / 4 / 8 | native header：version=12、numFds=2、numInts=26 |
| 12 / 16 | pixel fd / metadata fd |
| 20 / 24 | magic=`0x676d736d` / private flags |
| 28 / 32 | physical aligned width / allocated height |
| 36 / 40 | requested unaligned width / height |
| 44 / 48 / 52 | format / buffer type / layer count |
| 56 / 64 | 64 位 allocation id / usage |
| 72 / 76 / 80 | 32 位 size / pixel offset / metadata offset |
| 84 / 92 / 100 | 4 字节对齐的 64 位 CPU base / metadata base / gpuaddr |
| 108..119 | 三个尾部 32 位槽；构造时清零，**本轮未确认名称或后续语义** |
| 120 | 构造时复制 format（`0xa8b4`）；不是可随意省略的尾部 padding |

前 108 字节既由旧公开头解释，又与原 Vulkan 的字段读取和原 gralloc 构造 `0xa850–0xa8bc` 交叉核对。尾部 `0xa898/0xa89c/0xa8a0` 的重叠向量清零覆盖 76..119，`0xa8b4` 单独存 format 至 +120。对于当前受限 RGBA view，可完整保留 124 字节大小、26 ints、清零未知尾部并复制 +120 format；这仍不是任意 QTI 元数据消费者的全兼容声明。metadata fd 为 −1 的 view 仅用于已审计的 donor 私有访问，不得发往 IPC、旧 gralloc 或 native_handle_clone。

flags 的意义也必须与使用点区分：公开头中的 bit3=`USES_ION`、bit25=`TILE_RENDERED`、bit27=`UBWC_ALIGNED`、bit30=`UBWC_ALIGNED_PI`。原 Vulkan Properties 在 `0xdc260/0xdc28c` 检查 bit25/27，布局 helper 在 `0x1cd630` 提取 bit27。当前线性 view 必须清这些布局压缩位，且 protection 必须由真实 metadata 判定；**没有证据证明 Vulkan 此路径要求 flags=8**。GLES op24 对 bit3 的要求不能直接外推到 Vulkan；如保留 8 作为私有 fd-backed 兼容值，应如实标为实现选择，不宣称实际分配器是 ION。

补充原件 SHA256：

- `libgralloccore.so`：`86ac7bee9e96d06eeaf8edc263c7b9acac41c067e076186a14e6f80706e48e05`。
- `hw/gralloc.kona.so`：`2177fac5f1f5f938600eb3dff32c2cc258c2db454f4aae8e3ddd36293ee5ab1b`。
- `hw/android.hardware.graphics.mapper@3.0-impl-qti-display.so`：`a2982831beced3d12052dede4d59ed5689a084e007b9e1647ece7a8226367197`。

## Properties 查询没有取得长期引用

`vkGetAndroidHardwareBufferPropertiesANDROID` 从 `0xdc120` 开始，长度 2280 字节。它不 acquire/release AHB，不保存长期 handle；调用者必须保证输入 AHB 有效。

关键直接读取为：

- `0xdc19c`：handle+72 的 32 位 allocation size。
- `0xdc22c`：handle+44 的 QTI format，传入格式转换函数 `0x1cd090`。
- `0xdc250`：handle+24 的 private flags。
- `AHardwareBuffer_describe` 的结果用于 usage、BLOB 特例；常规 RGBA 格式仍来自私有 handle。

这些字段查询没有先经过 mapper3 的 getFormat/getSize。原库对空 handle 有分支，但可能填出零属性并返回成功；因此不能用“hook 返回空指针”替代清楚的 unsupported 错误。外层 Vulkan 入口应先验证兼容范围，拒绝不能构造真实视图的输入。

查询应使用临时事务租约：取得/验证视图、调用原 Properties、核对真实输出、退出时释放查询租约。仅查询不能永久持有每个 AHB，也不能仅改输出值而让后续导入继续读取原 minigbm 布局。

## vkAllocateMemory 导入与真正释放

导出入口 `vkAllocateMemory` 为 `0xbed88`，它在 pNext 遍历中于 `0xbee70–0xbee88` 识别 `VkImportAndroidHardwareBufferInfoANDROID` 的 sType `0x3b9cc1eb`，取结构 +16 的原 AHB。随后构造内部 allocation 描述，`0xbf2ec` 调用 `0x1f68a8`。

| 内部 memory 字段 | 本轮确认的含义 |
| --- | --- |
| +12 | 内部对象引用计数；最终释放前有原子减计数 |
| +264 bit18 | 此导入路径中由 driver 自己 mmap 的 CPU 映射标志 |
| +288 | 指向 GSL/memory 映射描述；其 +0 是 CPU 地址，+8 是 GPU 地址，+16 是长度字段 |
| +304 | 独立 fd 导入分支的 owned fd；初始值 −1，不能误认为 AHB raw fd 的所有权 |
| +312 | 原 `AHardwareBuffer*`，用于 acquire/release、describe、导出 |
| +320..+375 | native-buffer 分支的 56 字节结构副本，见后文 |
| +336 | native-buffer 分支副本中的 `native_handle_t*` |
| +432 | AHB 导入分支保存的 raw handle 指针；其他 memory 类型可有其他含义 |

这些是按分支确认的私有字段，不能作为跨版本通用结构定义。

AHB 导入顺序：

1. `0x1f6e94–0x1f6e9c` 保存原 AHB 至 +312 并 acquire。
2. `0x1f6edc` 调用 `0x1ce198`，可能经旧 gralloc surface-metadata 接口验证。
3. `0x1f6f04` 取得 handle；`0x1f7178` 调用 `0x1cd3b0` 完成 fd/地址导入。
4. helper 自建 mmap 成功时，通过 out 参数置 1；`0x1f7184–0x1f7198` 据此设置 memory+264 bit18。
5. `0x1f71a4–0x1f71a8` 再次取得并保存 raw handle 至 +432，即使之后还有其他初始化检查，视图此时也不能是栈对象。
6. 失败退出 `0x1f78c8–0x1f78e8` 调用 `0x130c70` 释放已建对象，随后清空返回 memory 指针。失败处理必须容许 donor 已经执行了 AHB release，不能由外层再次盲目释放同一租约。

### acquire 不能承担可失败的资源创建

`AHardwareBuffer_acquire` 返回 void，而且 donor **先存 +312、再调用 acquire**。若私有 acquire 创建 view 失败却没有真正增加平台 AHB 引用，后续 donor 错误析构仍会 release，可能错误减少应用原有引用。即使始终调用平台 acquire 再让 getNativeHandle 返回 NULL，虽然 `0x1cd3b0` 会拒绝空 handle，之前的 `0x1ce198` 仍可能把空 handle 交给旧 perform；不能把这作为正常错误协议。

应在公开 `vkAllocateMemory` 包装入口解析真实 AHB，完成 metadata、fd 边界、格式/层数/protection 检查，并预留 view、dup fd、需要的 mmap 与独立 AHB 保活引用。进入 donor 后的私有 acquire 只提交已有事务的 owner 计数并调用真实 acquire，不再执行可能失败的分配。外层 prepare 失败返回对应的 Vulkan unsupported/OOM 错误；正常错误回滚仅释放尚未提交的临时租约。成功 owner 必须一直留到 donor 真正的 release，未知 caller 或缺失事务属于适配器契约错误，不能回退使用原 minigbm raw 布局。

`vkFreeMemory`（`0xdbf00`）在 `0xdbf64` 调用 `0x130c70`。后者在 `0x130c8c–0x130ca4` 对 memory+12 原子减计数，**只有原计数为 1 才继续最终析构**。`0x131070–0x131090` 经对象 vtable+16 分派；该 memory 的 vtable 位于 `0x2e17f0`，+16 对应 `0x1f53d8`。

最终 AHB 清理在 `0x1f5844–0x1f5878`：

1. 读取 memory+312；为空则跳过。
2. 若 bit18 且映射地址非空，重新 getNativeHandle，按 handle+72 调用 munmap。
3. `0x1f5874` release 原 AHB。
4. 清空 memory+312，继续释放 GSL/内部对象。

因此 **外层 `vkFreeMemory` 返回不是可靠的视图最后使用点**。视图应跟随真正的内部最后引用/析构，不能仅按公开 memory handle 从表中删除。释放拦截器在调用平台 release 前必须完成依赖原 AHB 的必要查询；同时必须保留原函数调用语义。

`vkGetMemoryAndroidHardwareBufferANDROID`（`0xdefb0`）返回的是 memory+312 中的原 AHB，先在 `0xdefd0` acquire。不得把 QTI handle 视图强转成 AHB 返回给平台或应用。

driver 自行分配 AHB 的分支在 `0x1f76c8` 调 `AHardwareBuffer_allocate`，成功在 `0x1f76e0` 保存其初始引用，转入同一导入过程；最终仍走 +312 的 release。这一路没有导入 acquire 调用，因此只追踪 `0x1f6e9c` 会遗漏它。当前线性 RGBA 初期范围可以明确拒绝尚未支持的 BLOB/导出分配路径，但不能声称支持全系统后继续遗漏。

## CPU 地址、fd 与 munmap 所有权

`0x1cd3b0` 的参数为 `(device_context, raw_handle, extra_offset, secure_flag, memory_desc, out_did_mmap)`。已确认的操作：

- `0x1cd3d4` 读 handle+72 size、+76 offset；`0x1cd3e0` 读未对齐的 64 位 +84 CPU base；`0x1cd3e8` 读 +12 pixel fd。
- `0x1cd408` 计算 size−extra_offset；`0x1cd3f4` 计算 offset+extra_offset；`0x1cd41c` 通过 device dispatch+200 传入这些值与 CPU base。
- 若已有真实 CPU base、GSL 输出 CPU 地址仍为空，`0x1cd450–0x1cd454` 以 base+offset+extra_offset 填充。
- 若成功导入但仍无可用 CPU 地址且非 secure，`0x1cd4a0–0x1cd4c0` 以 fd/长度/offset 调 `mmap64`，仅成功后设置 out_did_mmap。
- 这条 helper **不把新 mmap 地址写回 handle+84**，而是写到 memory_desc；不能依靠原 handle 被改写来管理映射。

兼容层必须选定一种明确所有权：提供真实、由视图租约拥有的稳定 mmap，或保持 base=0 让 donor 走已审计的自建 mmap 路径。不能同时把同一映射交给两方 munmap；不能把 minigbm width/height 当作 +84 指针。offset、extra_offset、size 的加减都需做溢出和 fd 边界校验。首次范围宜保持单层、非保护、线性 RGBA、已验证的单 pixel fd/零偏移，后续再扩展。

**本轮选定 base=0**：适配器只提供已验证的真实 metadata、私有 view 和自有 dup fd；donor 按上述路径创建、记录并释放 CPU mmap。文中关于自有 mmap 的说明是所有权审计结果，不代表当前候选另外创建了该映射。

AHB 和 native-buffer 分支都借用 handle+12 的 fd；没有看到它们把该 fd 写入 memory+304 的 owned-fd 槽。析构在 `0x1f5b04–0x1f5b18` 只关闭非 −1 的 +304。适配层若 dup fd，dup 的关闭权应归自己的视图对象，不应通过修改 donor +304 猜测转交。

### 最终 release 之后的剩余析构

AHB release 返回后，`0x1f5878` 清 +312，`0x1f587c` 跳回 `0x1f5574`；已审计后继不再读取 raw handle 或 memory+432。它仍通过 `0x1f5770–0x1f5780` 的 dispatch+176 释放 GSL descriptor，之后释放内部描述对象。该 dispatch 的 GSL wrapper 为 `0x12e210`，`0x12e240` 调 `gsl_memory_free_pure`；基本对象析构 `0x1f6890` 仅重设 vtable，不读 handle。

原 `libgsl.so`（SHA256 `8971ade1009b02ca49a19cde9a78e324b2f7b868253f614c337105bbb669cfb6`）的 DRM 路径补充证据：

- `gsl_memory_free_pure` `0xca810` 转入 `ioctl_drm_sharedmem_free` `0x20e8b0`，后者只读取 descriptor+32 的内部记录，递减 GSL 导入引用。
- 最终 `0x20b7c8` 根据 GSL 自有记录处理自己的 mapping/GEM；它没有读取 AHB view，也没有解引用 descriptor+0 的外部像素地址。
- `ioctl_drm_sharedmem_map_ext_fd` `0x20f260` **未使用传入的 x1 CPU base**，在 `0x20f2b4` 即覆盖 w1；它另建清零的 40 字节内部记录，+24 的自有 mmap 槽不等于 view+84。Vulkan 上层在 `0x1cd450` 才补入自己的 CPU base。
- 原 Vulkan 自建 mmap 分支在 `0x1f586c` 就 munmap，再 release AHB，再走 GSL free，说明原有顺序允许外部 CPU 映射先失效。

因此，在本轮限定的 DRM、线性、offset=0 路径且 view 的独立映射没有转交 GSL 所有权时，最后 owner 的 release wrapper 可以回收 view、自有 mmap 与 dup fd；无需把它们错误绑定到公开 vkFreeMemory 返回。仍应保留实际引用计数和 map/fd 归零验证；此结论不覆盖 KGSL、额外 profiling 回调或未来 WSI 别名扩展。

## WSI / VkNativeBufferANDROID 不是 AHB API 路径

Android loader 的 native-buffer 图像路径使用 sType `0x3b9af110`：

- `vkCreateImage` → `0xeb9e0`；`0xebc00–0xebc0c` 匹配该 sType，`0xebb28–0xebb48` 保存原 pNext 节点并记录 native 类型。`0x147d68–0x147d78` 调用 `0x148ec8`。
- `vkBindImageMemory2` 在 `0xc0048–0xc0054` 匹配同一 sType，`0xc00f0–0xc00f4` 传入 node；`0x14c190–0x14c1d8` 将图像转为 native 类型并调用同一 `0x148ec8`。
- `0x148f08` 从 node+16 取 raw handle；`0x148f1c` 取 node+28 format，implementation-defined 格式 0x22 才再读 handle+44。`0x148f48` 读 node+24 stride；`0x148f70` 把 raw handle 交给 `0x1cd510` 构造布局。
- `0x149054–0x14905c` → `0x14a6f0`，将包含 node 的描述交给内部 allocation。`0x14a800` 保存描述指针到 allocation+104；`0x14a89c` 进入 `0x1f68a8`。
- `0x1f6df8–0x1f6e18` 从该 node **浅拷贝 56 字节**到 memory+320。node+16 的 raw handle 因而保存到 memory+336。只复制结构，不复制 native handle 本体，也没有 AHB acquire。
- `0x1f6f24–0x1f6f30` 用 memory+336 的 raw handle 调 `0x1cd3b0`。
- native 分支析构 `0x1f5824–0x1f5840` / `0x1f5880–0x1f5890`：必要时根据 +336 handle 的 +72 munmap，清空 +320..+375；**没有 AHB release，也没有 native_handle 释放**。

所以只包装 `AHardwareBuffer_getNativeHandle` 不足以接管 Vulkan 窗口。需要在 create/bind 的 Android native-buffer 入口制作 node 的私有副本并替换其中 handle，随后把对应视图租约绑定到成功创建/更新的 image 与其内部 native memory 最后使用期。调用结束后可以释放临时 pNext 节点，但被浅拷贝的 handle 视图不能释放。应同时审计 image 销毁、重绑定和失败回滚；本轮没有把这一部分实现或最后引用触发点宣称为已完成。

### 普通 native 图像的持有终点

固定版本中可以确认以下所有权链；它支持下一轮按单层、单平面、首次绑定的 native 图像进行有限实现，不构成所有扩展的通用生命周期证明。

1. 图像构造 `0x147718` 在 `0x147748–0x147750` 分配 1080 字节，初始 +12 引用数为 1。native 类型 3 在 `0x147d54–0x147d78` 进入 `0x148ec8`。
2. `0x14a89c` 为每个内部 plane 调用 `0x1f68a8` 创建 memory；成功后 `0x14a8b8` 保存到 image+360+8×plane。这是新建的内部 memory，不是公开 `vkAllocateMemory` 返回的另一个对象。node 内的 view 指针保存在各 memory+336，多个 plane 必须共享同一视图的持有而不是各自关闭同一个 fd。
3. `vkDestroyImage`（`0xdb128`）在 `0xdb18c` 调 `0x130c70`，先递减 image+12。真正 image 析构 `0x147028` 的 `0x147164–0x1471a0` 遍历 image+352 的 plane 数，对 image+360 起的每个 memory 调 `0x130c70`（调用点 `0x147194`），再清槽。memory 最终析构会读取 view+72 完成 donor mmap 清理，所以 view 至少要保留到该清理返回。
4. 已审计的正常 image-view 构造 `0x156bc8–0x156be8` 将输入图像指针复制到 view+240；该段没有对图像 +12 加引用。image-view 析构 `0x156798` 释放自己的 +440/+448 辅助对象，没有释放 +240 图像。因此不能把普通 image view 当作会延长 native 图像生命的 owner。
5. 图像构造失败在 `0x148e88–0x148ea0` 经 `0x130c70` 同步清理已建部分，并清空输出；公开 create 仅成功时写 `*image`（`0xd77b0–0xd77b8`）。外层临时 view 在 donor create 返回以前必须存在，失败返回后可撤销尚未转入其他 owner 的临时租约。

普通 native 路径未发现把 image+360 中的 memory 另行交给公开应用持有，也未在已审计的普通 image-view 路径发现 image 引用增量。因此，**在确认销毁前 image 和 native memory 的实际引用都符合该唯一 owner 路径时**，让 view 持有至原 `vkDestroyImage` 返回，覆盖了已审计的最后 raw-handle 读取。不得把“公开 DestroyImage 已调用”替代该引用条件。若出现额外引用，应延续视图到真实最后 native-memory 析构或明确继承租约；不应先关闭 dup fd、释放 view 再假设 donor 不会访问。

### 共享与派生路径的限定

- `0x148230–0x14825c` 可把构造描述 +128 的既有 memory 保存到 image+360，并对 memory+12 原子加 1。但 `0x14819c–0x1481a4` 把 native 类型 3 分到另一分支，故这不是普通 native create 自动共享 memory 的证据。
- `0x14cc20` 构造辅助图像时也可传入既有 memory（`0x14ccfc–0x14cd28`）；已找到的直接调用位于 `vkCmdCopyBufferToImage2` 的 `0xc39fc` 和 `vkCmdCopyImageToBuffer2` 的 `0xc49e0`。对应辅助对象在 `0xc3ab4` / `0xc4aa0` 通过 `0x130c70` 释放。不能仅因输入对象也使用 +360 字段，就把 buffer 的辅助图像路径误认为两个 native WSI 图像共享 memory。
- `0x14c10c` 的内部 tag=2 分支保存另一来源对象至 image+936，取来源图像的 memory；若来源为 native 类型，它从 memory+336（旧版本分支为 +320）取 handle 再导入。这里确有传播 raw view 的能力。**目前找到的直接 `0x14c0b8` 调用只设置 tag0/1**；公开 `vkBindImageMemory2` 对 sType `0x3b9bb46e` 的分支只是跳过，并未设置 tag2。因此没有证据证明正常 Android WSI 会到达该分支，也不能把它当作不存在。未来开放相关来源对象/别名扩展时必须为目标对象继承租约。
- 本轮没有证明所有间接虚调用、profiling、扩展或异常重绑定都不保存额外引用。有限 WSI 候选应验证实际 image/memory 引用和 create/bind/destroy 的 view/fd 计数归零；完整系统支持不能仅以单次呈现成功结束该审计。

### bind 失败不能按整个 VkResult 回滚全部 view

`vkBindImageMemory2`（`0xbffa8`）逐项调用 `0x14c0b8`（`0xc001c` / `0xc0154`）；`0xc0020` 遇首个非零结果直接返回，未回滚前面成功绑定的图像。失败项内部的 `0x14a6f0` 也可能已有 plane 成功：`0x14a8b8` 逐项保存 memory，下一项在 `0x14a8a0` 失败后仅返回，并未释放先前 plane。`0x14c1d8–0x14c1dc` 将失败继续返回；图像仍是原来已存在的对象。

因此公开 bind 的 prepare/finish 必须以图像/实际已提交内部 memory 为单位记账。整个 batch 失败时，已绑定图像和失败项中已形成的内部 owner 仍要保留 view，直到对应销毁清理结束。不能统一撤销整批 view，也不能因为失败而假定该图像尚未保存 raw handle。首次实现可限定一张单 plane native 图像，仍应保留 donor 失败后的销毁路径；尚未审计的重复绑定不应通过覆盖表项丢弃旧租约。

## Android HAL 打开与入口查询补充

固定 donor 的 HMI 位于 `0x2e41b0`，methods 位于 `0x2e42a8`，open 为 `0xf24f0`。open 在 `0xf250c–0xf2518` 调 `calloc(1,144)`，不是静态 HAL device；`0xf2548–0xf2554` 清零 +16..143，随后只保存 +120 的 EnumerateInstanceExtensionProperties、+128 的 CreateInstance 和 +136 的 GetInstanceProcAddr，`0xf26a4` 写入输出。**common.close（+112）保持 NULL**。包装层不能将非空 close 当作本 donor 的必要条件或无条件调用；若选择不释放未知 donor 对象，宜保存复用这个无 close 的 HAL device，避免每轮 open 留下 144 字节。不要仅释放自身 wrapper 后就把 donor 分配误称为静态常量。

原 GIPA 为 `0xbde18`，GDPA 为 `0xde1c0`，动态符号分别为 `_ZN11qglinternal21vkGetInstanceProcAddrEP12VkInstance_TPKc` 与 `_ZN11qglinternal19vkGetDeviceProcAddrEP10VkDevice_TPKc`。GIPA 的 450 项注册表中，`vkGetDeviceProcAddr` 的条目确实位于 `0x2d7828`：name=`0x4b4d0`、extension=-1、device-entry=1、global-entry=0。函数字段 `0x2d7830` 的 APS2 relocation 是 `R_AARCH64_ABS64` → dynsym192 → `0xde1c0`。所以不能从包装层的初始化失败推断 Android HAL 不提供 GDPA。查询时若内部配置对象不可用，`0xbdff0` 仅放行 global-entry 非零的函数；若配置可用，extension=-1 会在 `0xbdfd4` 直接进入 `0xbe078` 返回 GDPA。实际失败阶段应分别记录 open、CreateInstance 与各必需入口是否取得。

## mapper、旧 gralloc 与 clone/free 边界

平台对象初始化同时做两件事：`0x129c80/0x129c90` 取得 mapper3/QTI mapper，随后 `getMapperExtensions` 的回调保存 extensions；`0x129f10–0x129f60` 另行通过 `hw_get_module("gralloc")` 打开旧 gralloc device，并取得 perform 函数，分别保存在平台对象 +112/+120。

固定 donor 可把 `hw_get_module` 私有重命名为 `sq2_getmodule`，只对 `"gralloc"` 设置 `*module=NULL` 并返回 `-ENOENT`，其他名称透传。原对象在 `0x129be8` calloc144，+104/+112/+120 初始清零；`0x129f20/0x129f24` 检查 module 指针，空时跳到 `0x129f68`，不会调用 open。只要 mapper extensions 已成功（w22=0），`0x129f6c` 仍跳到成功的 `0x129f74`；若 mapper 和旧 gralloc 都不可用，则走既有失败清理。这一设计的前提是 Vulkan 专用 mapper 服务确实正常，不能把缺少所有 mapper 的失败吞掉。

Properties 的 `0xdc268`、layout 的 `0x1cd5f4/0x1cd63c` 和导入 metadata 的 `0x1ce254` 都先检查 +112。关闭旧 gralloc 后走直接字段/无厂商 metadata 的既有回退，阻止 QTI view 被传入真实旧 gralloc；不需要给 minigbm 伪造 QTI metadata fd。此时 mapper3/mapper5 反向解析仍必须保留。

本轮跟踪的 Properties/layout/import helper 直接读取 QTI handle，或把**同一 raw handle 指针**传给上述旧 perform。例如 `0x14b4ac`、`0x1ce260`、`0x1cd608`、`0x1cd64c`、`0x1cd838`，没有自动把 view 转回 original。不能因为取得了 mapper3 service 就认为后续所有 metadata 都通过 mapper3。

审计时的基线 `probe/graphics/quest2-qti-mapper.cpp` 没有 view 解包：

- `Mapper::importBuffer` 直接将 `handle.getNativeHandle()` 传入 mapper5。
- getter 的 `info()` / YUV helper 直接把收到的指针当原 minigbm handle 查询。
- lock 的 mapper5 import 和 free/getTransportSize 也依赖正确的原/已导入 handle。

后续已加入 `SQ2_V77_AHB_VIEW` 专用编译分支，处理这些入口；普通GLES/mapper4路径不变。Vulkan view的RGB/YUV地址查询暂拒绝，避免创建额外映射；borrowed view不能直接freeBuffer。具体实现见源码及上述最新实测页，不将本节的原始缺口误读为仍未修改。

因此一旦向 donor 返回 QTI view，**每个可能流向 mapper/旧 gralloc 的边界都必须反查 view→original/imported-original**。raw QTI view 不能直接传入真实 mapper5，也不能只在 `importBuffer` 一处解包后忽略 getter、lock、unlock、free。mapper5 返回的 imported handle 还可能是另一指针，必须记录别名与独立的 import/free 所有权。

该固定 Vulkan ELF 的动态符号表中没有 `native_handle_create`、`native_handle_clone`、`native_handle_close`、`native_handle_delete` 导入；上述 AHB/native-buffer 路径也没有这些调用。唯一可见 `dup` 调用在 `0xdf010` 的外部 fd 导出路径，不是 AHB handle clone。不能由此排除所有依赖库或未审计扩展里存在内联/间接 clone；新视图设计不应依赖“整个进程永远不会复制 handle”的假设。

## 可扩展的私有视图设计边界

建议把职责拆成以下实体，不改变原 AHB 或原 minigbm handle：

1. **视图记录**：固定验证的 QTI 布局数据；原 AHB（若存在）、原 handle、mapper5 imported alias、实际 fd/metadata、可选自有 mapping、代数与线程安全引用。用原对象身份加分配标识校验，不能只按整数 fd 缓存。
2. **AHB 导入租约**：driver 的导入 acquire/分配初始引用创建持有，最终析构 release 结束持有；Properties 仅临时查询租约。导出给应用的 acquire 原样透传，不当作新的 driver 内部租约。
3. **WSI 租约**：由 native-buffer create/bind 成功路径持有，确保原 buffer 与 view 至少存活到 image/internal memory 最后使用。没有 AHB 时可以由合法 mapper5 import 保持 fd-backed 原数据的有效寿命；不能伪造 AHB 对象。
4. **反向查找**：所有真实 mapper5/平台入口只收 original 或各自合法 imported handle。donor 的私有字段访问收 view。跨边界时在注册表中明确转换，未知指针失败，避免把任意 handle 的头部特征当作安全识别。
5. **失败事务**：预验证不支持的 format/modifier/protection/layers；在 donor 已 acquire 后失败的路径由实际 release 撤销租约，外层只撤销尚未提交的本地事务。fd/mmap/mapper import 各有唯一清理者。

可通过独立链接器 namespace 的 donor 专用符号依赖和 Vulkan dispatch 包装来实现；所有 `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr` 与导出入口需一致，不能让部分应用绕过包装。当前审计没有要求修改 GPU identity/capability 或默认驱动。

建议验证顺序为：属性与真实元数据逐项一致 → import/bind 与双向 GPU/CPU 图案读回 → 同 AHB 多 memory、早释放应用引用、导出后释放 memory、错误导入/绑定、重复创建/释放 → raw native-buffer 生命周期与实际窗口呈现。记录实际 donor acquire/release、view/map/import 数量和 fd 数量归零，再讨论系统持久化；属性非零或单次绘制成功不能代替这些检查。
