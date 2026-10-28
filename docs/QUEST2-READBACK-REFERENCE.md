# Quest 2 v77 GLES 读回路径参考

2026-10-28。本文记录原配库的离线反汇编证据、只读字段诊断，以及显式启用的进程内跟踪和独立副本策略实验。当前最佳私有候选叠加 CPU 4通道/bank15、PBO fallback和零面积验证修正：原414项与两个额外大步长各54项合计522项矩阵、上传/采样两组各10项及两种YUV各3帧全部通过。其范围是下述精确探针与有限格式，不能称为完整兼容。正常启动仍为 Mesa，原始 donor 文件保持不变。

## 固定输入与地址

原配输入由 [quest2-gles-v77-inputs.json](../configs/quest2-gles-v77-inputs.json) 固定：

| 库 | 原文件 SHA-256 |
| --- | --- |
| `egl/libGLESv2_adreno.so` | `a0d4d099a67d4a0db54ee6b5cd69e15e1787484128667120c7a90d6ae333b316` |
| `libadreno_utils.so` | `8bca58979c908b1da4b7caf064c65a5b72eb8ae6989a316e63611b2c1597eb57` |

GLES 的 GNU Build ID 是 `af16b8ea19e72ea9d26d9bb477003db3`。本文所有地址均为该 ARM64 ELF 的 **VA**，运行时地址为已验证的 load bias 加 VA，不能直接当作文件偏移。`objdump` 对已剥离内部函数显示的最近导出符号名通常没有语义意义。

`prepare-quest2-gles-linked.py` 用 `patchelf` 修改 RPATH，副本的整文件哈希会变化，GNU note 也可能移动；原版及当前 linked 副本的 `.text` VA 和上述 Build ID 保持相同。运行时诊断仍要求启动脚本先检查该候选的 `SHA256SUMS`；Build ID 和少量指令匹配只是结构版本限制，不能代替整文件完整性检查。

## 问题范围

早期对照把 GPU 渲染/采样与读回分开：640×360 内部 RGBA 纹理直接读到客户端内存失败；同一内部纹理再被 GPU 采样到线性 RGBA AHB 后，`glReadPixels` 和 AHB CPU 读取均通过。640×360 PBO 读回通过，但 264×136 PBO 对照失败，故 PBO 本身不是通用解法。后续精确图案和原始 tiled 缓冲区确认 CPU 转换器选错4/8通道及 bank 地址公式；反向 CPU 上传也复现了布局错误，不能把问题范围限定为 `glReadPixels` 一个 API。详细矩阵由 [QUEST2-GPU-RESEARCH.md](QUEST2-GPU-RESEARCH.md) 维护。

这些结果不支持直接宣称 UBWC、GMEM 或 A680 身份是根因。CPU 布局修正、PBO 目标偏移和零面积参数验证是三个独立问题，以下分别记录其证据和限制。

## ReadPixels 调用链

| VA | 静态证据 |
| --- | --- |
| `0xfde88` | 导出的 `glReadPixels`，取得当前分派对象后调用其 `+752` 槽。 |
| `0x1d1db8` | 无检查 thunk：`ldr x0,[x0,#8]; b 0x17f698`。 |
| `0x1ed098` | 带参数验证的 thunk；`0x1ed0b4` 从分派对象 `+8` 取得内部 context，尾跳在 `0x1ed144`。 |
| `0x2271d0` | 带 `GlReadPixels` trace 的包装；`0x227254` 从分派对象 `+8` 取 context，`0x227274` 调用 `0x17f698`。 |
| `0x17f698` | 公共读实现；`0x17f8b4` 调用 `0x1b67f0` 构造传输描述，`0x17f8e0` 调用 `0x1bb4e0`。 |
| `0x1bb4e0` | 资源传输主体，包含字节数/比例阈值、临时资源分配及 GPU 拷贝与 CPU 传输的选择。 |
| `0x11add0` | CPU 传输参数构造；调用点为 `0x1bc1b8`。内部通过 `0x133290` 映射源资源，再进入 `0x296390` → `0x2904b8` 软件转换/拷贝分派。 |

不能仅看到某结构也写 `+752` 就认定它是 GL 分派表。特别是 `InitEsx` 的 `0x108a30` 属于另一接口表，它对应的 `0x2714f0` 不是本处的 ReadPixels thunk。

CPU 转换描述中的表面格式在 `+224`，另一个布局判别字段在 `+232`，尚未完整恢复其枚举。`0x11b244` 把此次构造的传输字段 `+164/+168` 设为 `{1,1}`，`+172` 设为 0；`0x296390` 的多工作线程分支要求 `+168 >= 2`。目前不能把损坏边界归因于多线程切片。

## 两个精确的数值配置键

人类可读名称尚未恢复，以下使用原库实际比较的哈希键。

| 数值键 | 解析与赋值 | 使用位置 |
| --- | --- | --- |
| `0x0388d1af` | `0x1430f0–0x143108` 比较；`0x1501ec` 调用 `strtoul`；`0x1501f8` 写 settings `+9596`（`0x257c`）。 | `0x1bb884–0x1bb88c` 比较传输字节数，小于阈值时清除临时拷贝选择；`0x1bbbe4` 另有按源分配字节数比较的分支。 |
| `0x3497955d` | `0x14ab7c–0x14ab94` 比较；`0x150674` 调用 `strtoul`；`0x150680` 写 settings `+9600`（`0x2580`）。 | `0x1bb858–0x1bb874` 比较 `传输字节数 × 100 / 源分配字节数`。 |

把两键设为数值 `0` 是一个有静态依据的私有实验候选：允许较小读回进入临时资源/GPU 拷贝分支。它并不保证跨过所有其他条件，也没有被证明是有效修复。主线程用 `strace` 确认进程以 `O_RDONLY` 打开 `/data/vendor/gpu/esx_config.txt`，完整读取含 magic 和两项 0 的 41 字节内容，共观察到四次读取；但 context 创建后的字段仍为 `{0xffffffff, 0}`。这证明配置文件确实被读取，不能证明两项 0 在策略校正后保留。

同一只读诊断下，默认无配置、私有两键为 0、640×360 PBO 三种情况的最终字段均为 `{0xffffffff, 0}`。前两种直接客户端读回各有 16032 个错误通道；PBO 三帧全部通过，最大误差 1。阈值相同而结果不同，说明还存在影响路径选择或后续行为的因素；不能据此把 CPU 复制、detile 或某一具体函数认定为已证实根因。

### 初始化与其他赋值点

默认初始化函数 `0x1610b8` 从对象 `+56` 取得 settings。`0x1610f4` 从 VA `0xc318` 加载 64 位常量 `0x8000000000000000`；`0x16119c` 将其写到 settings `+9592`，因此相邻字段 `+9592` 为 0，字节阈值 `+9596` 为 `0x80000000`。`0x161118` 设置 `w9=75`，`0x1611a0` 将 75 写到比例阈值 `+9600`。这些是初始化值，不是现场最终值。

配置解析器在 `0x1501f8`、`0x150680` 分别写两个阈值；相邻 `+9592` 字段另由键 `0xff3bca02` 在 `0x14eab0` 写入，不应与字节阈值混淆。对上述精确偏移和寄存器偏移常量的离线检查还找到了以下 finalizer 覆盖，未找到另一个可确认为这两个 settings 字段的直接赋值。该检查不能排除别名访问或批量复制；别处出现相同数字偏移并不足以证明目标是同一结构。

### 后初始化覆盖

配置解析与策略校正有明确先后顺序：

1. `0x1c2068` 调用配置解析器 `0x141c88`；另一个配置可在 `0x1c266c` 解析。
2. 随后 `0x1c2090` 调用 finalizer `0x1c2a98`。另一条更新路径也是先在 `0x1c3618` 解析，再在 `0x1c3624` finalizer。
3. finalizer 在 `0x1c2f0c–0x1c2f24` 检查 `*(*(object+24)+1180) == 1`。该字段的准确名称还未知。
4. 条件不满足时，`0x1c2f9c–0x1c2fa4` **在解析后把字节阈值写为 `0xffffffff`，比例阈值写为 0**：

   ```asm
   1c2f9c: 2f00e5e0  movi d0, #0xffffffff
   1c2fa0: 5284af84  mov  w4, #0x257c
   1c2fa4: fc246900  str  d0, [x8, x4]
   ```

`d0` 是一个 64 位寄存器，此处立即数产生 **`0x00000000ffffffff`**，不是 `0xffffffffffffffff`；小端 8 字节写入 `+9596` 后，两个 32 位字段就是 `{0xffffffff, 0}`。离线通过 `qemu-aarch64` 执行原始指令字 `0x2f00e5e0` 并用 `fmov` 取回 64 位结果，已确认该值。本文早期版本把它写成“两个阈值都为 `0xffffffff`”是立即数解释错误，现已更正；store 偏移本身无误。静态覆盖值与现场读取一致，但尚未通过运行时分支跟踪确认每次都执行此处。

因此，必须把“配置文件读取”“键被解析”“最终字段值”“实际拷贝路径”和“像素通过”分开记录。这里不建议伪造 chip ID 或能力位来跳过校正。

## 只读 settings 诊断接口

[quest2-readback-settings.h](../probe/graphics/quest2-readback-settings.h) 提供：

```c
static void quest2_log_readback_settings(void);
```

在私有探针中包含此头文件，在 EGL context 已 current 的同一线程、`glReadPixels` 之前按需调用。它没有 GL API 调用，也不改内存；所有目标地址的内容都经 `/proc/self/mem` 的 `pread` 取得。失败只打印 `QUEST2_READBACK_SETTINGS status=unavailable`，不退出、不改变测试判定。主线程已在 Surface 上验证 global 分派路径成功，目标 thunk 为 `0x1ed098`；TLS 分支仍只有静态证据。

诊断步骤与限制：

- 从 `/proc/self/maps` 找到 basename 精确为 `libGLESv2_adreno.so` 的唯一 offset 0 可读映射；多个同名实例或 deleted 映射均拒绝。
- 核验 ARM64/little-endian/ELF64/ET_DYN、offset 0 且 VA 0 的 LOAD、从程序头定位的 GNU Build ID；不固定 note 在文件中的位置。
- 核验公开 ReadPixels 的全局/TLS读取和分派指令，以及 settings 读取指令。每次读取都重新检查可读范围，用 `pread` 处理并发卸载造成的失败；不直接解引用目标指针。
- 原 wrapper 使用 `G=base+0x405ad0`，取 `p=*(G+616)`。若为 `UINT64_MAX`，用 `G+44` 中的原 `pthread_key_t` 调用 `pthread_getspecific`；返回指针只转换成整数。
- Bionic heap 指针可能带 ARM64 top-byte tag。初版未移除 tag，现场报 `unavailable reason=dispatch_slot_read`；主线程修正为仅去掉指针最高 8 位，保留 `UINT64_MAX` sentinel，并在后续每次读取前继续验证 maps 可读范围。修正后成功读取上述 global 链；去 tag 不能代替地址校验，也不允许扩大到任意位掩码。
- 读取 `p+752` 槽，只有其目标是上述三个已审计 thunk 且相关指令匹配才继续。未知 thunk 只打印相对 VA，不猜测结构。
- 确认后取 `real=*(p+8)`、`settings=*(real+168)`，读取两个 `uint32_t` 阈值及 `real+136` 的只读flags字段；不写任何地址、不调用内部函数、不输出 heap 地址。结束时恢复调用前 `errno`。

Android NDK 构建不需要额外链接库：`pthread_getspecific`、`pread` 和 stdio 都由 Bionic libc 提供；头文件自身不需要 `-ldl` 或 `-lpthread`。非 Android 主机做编译检查可用 `-pthread`。默认不应把该诊断接入正常启动或持续日志。

头文件已用 NDK r28c 的 `aarch64-linux-android35-clang` 和主机 C 编译器通过 `-O2 -Wall -Wextra -Werror` 检查；主机没有 donor 映射时按预期输出 `unavailable reason=unique_donor_mapping`。修正 top-byte tag 后，主线程在 Surface 上对默认、私有配置和 PBO 探针完成上述只读字段检查。该验证不涉及写入内部 settings，也不是读回修复。

## 压缩与芯片识别的边界

原配 GLES 在 `0x26b778–0x26b784` 明确比较 `0x06080001` 并进入 `0x26c280` 分支。`libadreno_utils` 的 `isUBWCSupportedByGpu`（VA `0x101a8`）参数是像素格式，不是 chip ID；它内部在 `0x103b0/0x103bc` 明确处理 A680.1。因此 GSL 的 “Unknown extended properties” 不能推导为这些原库完全不识别 A680。

原库包含 `GL_EXT_texture_storage_compression` 字符串。官方扩展的 `glTexStorageAttribs2DEXT` 接受 `{GL_SURFACE_COMPRESSION_EXT=0x96C0, GL_SURFACE_COMPRESSION_FIXED_RATE_NONE_EXT=0x96C1, GL_NONE}`，但 NONE 只禁止固定码率压缩，而且也是默认值，不能据此宣称所有无损 UBWC 均已关闭；该扩展还要求 GLES 3.0。[Khronos 扩展规范](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_storage_compression.txt)

以下后续实验已确认最终阈值为 0/0，仍未修复像素；不能把消除两个阈值限制称为成功强制 GPU 读回。

## 有效零阈值对照：未修复

`prepare-quest2-readback-policy.py` 只接受 linked GLES SHA256 `99d4c18d35e69e8589f2550b242d5f6722031404852b9831e328df8d1778551f`，核对 executable LOAD 中上述三条指令，把 `movi d0,#0xffffffff` 改为 `movi d0,#0`，其他指令不变。输出到新的 `out/quest2-gpu/gles-linked-readback-policy/`，拒绝覆盖。只有两个文件字节变化，输出 GLES SHA256 为 `ef467371fb2f5d46ffc6519e6d448517a1dc5e25ae228d6c44f7da2b089c2c52`；身份、能力和布局字段未修改。

`SQ2_GLES_READBACK_POLICY=1` 仅在私有系统探针选择此副本。实际读取到 byte/percent 两阈值均为 0，但 640×360 YV12→内部 RGBA→client 仍有 16032 个错误通道，264×136 NV12 的 PBO 仍有 7180 个错误通道。退出后原 Mesa 哈希、boot ID 和框架 PID 均保持。此候选不是修复，不加入启动镜像。

## 精确 RGBA 与 PACK 矩阵

`quest2-readpixels.c` 另用逐坐标变化的 8 位 RGBA 图案检查所有像素、PACK 间隔和前后哨兵，不沿用大色块网格。10 个尺寸、2 个图案阶段、10 种 PACK/subrect 配置、client/PBO 两条路径共 400 项，再加 14 项错误和状态检查。包含 alignment 1/2/4/8、row length、skip rows/pixels、非对齐 PBO offset、子矩形/末像素、零/负尺寸、mapped PBO 和越界检查。

实际矩阵结果：

| 项目 | Mesa 基线 | 同版闭源原策略 |
| --- | --- | --- |
| 总检查 | 414 | 414 |
| 失败 | 0 | 221 |
| 字节不匹配总计 | 0 | 42996110 |
| GL 错误码不匹配 | 0 | 1（零高度 PBO 返回 INVALID_OPERATION） |

闭源 1×1、3×5、13×7 均通过；较大尺寸的 client/PBO 均有失败，**包括旧大色块读回通过的 640×384**。旧测试仍是有限图案的实测，不是精确纹理正确性的证明。两种构建分别强制 Mesa/闭源 renderer 和原始/私有 mount namespace；不可因加载错误驱动而得到伪通过。

构建 `bash scripts/build-quest2-readpixels.sh`，同步两个二进制及 `READPIXELS_SHA256SUMS` 到私有 `readpixels/` 目录。闭源通过 runner `--readpixels`；Mesa 基线在 Android 原命名空间直接运行 `quest2-readpixels-mesa`。GPU 检查串行、限时，原始日志留在 `out/quest2-gpu/readpixels/`。

新增独立 `--relay-to-ahb` 模式：原逐坐标图案先写内部RGBA8纹理，再用整数坐标`texelFetch`写入真实modifier=0的线性AHB，CPU按实际lockPlanes步长逐字节检查，完全不调用glReadPixels/PBO。640×360、264×136、640×384各两个phase共6组，Mesa和闭源均0错误；闭源每次引用/地址映射释放归零。该精确对照支持这三个尺寸的纹理渲染/采样内容正确，将问题进一步收窄到读回转换。runner用`SQ2_READPIXELS_RELAY=1 --readpixels`选择，不混入414项矩阵。

## 私有 CPU 传输跟踪

`SQ2_YUV_TRACE_COPY=1` 在独立探针的 EGL context current 且提交工作前，显式安装 `quest2-readback-trace.h` 的进程局部 hook。它核验完整候选清单、Build ID、目标函数全部字节的版本指纹和准确 prologue；只在本进程 MAP_PRIVATE donor text 上临时 RW→RX，退出即丢弃。没有改磁盘文件、其他进程、descriptor 或设备能力。不能用于正在运行的应用/SF；安装期间不允许并发 donor 调用。

hook 保留 ARM64 参数/向量/状态寄存器、原栈参数和返回地址，尾跳执行原始函数；先记录 `0x296390` 的 copy context、transfer 及两个 image 描述的各 256 字节。context/transfer/image descriptor日志仍用地址范围检查和 `pread`；另行显式启用的DRM源buffer快照使用下述受限 `write`/kernel copy_from_user，二者读取机制不同。原始指针日志只留在忽略的 out。NDK 严格构建和 QEMU 跳转/参数/返回 ABI 检查通过；实机原策略 client/PBO 已触发并正常退出。

| 实机路径 | 调用者 VA | 源字段 +200/+204/+208/+212 | 源/目的 +224 格式 | 源→目的 +232 布局 |
| --- | --- | --- | --- | --- |
| client 640×360 | `0x11bab4` | 640/360/368/640 | 28/28 | 2→0 |
| client 640×384 | `0x11bab4` | 640/384/384/640 | 28/28 | 2→0 |
| PBO 640×360，三次 | `0x119fb4` | 921600/1/1/921600 | 62/62 | 0→0 |

PBO 捕获可能来自 CPU 初始化 buffer，不能直接当作 PBO ReadPixels 的 detile 调用。源 padded height 与逻辑高度的差异是观察值，尚不是错误根因。

布局2→0分派在 `0x2921c8–0x292210` 进入 copy-context vtable `+832`。实机该槽为 `0x2be398`（相邻 +824 为 `0x2bca38`），正在核对具体地址公式。只读 GL real-context `+136` 为 `0x8500`，其中先前怀疑的 bit0 为0。静态 `0x3ccc88` 主要按格式筛选，格式28无额外条件即返回1；但该 gate 的真实输入仍需区分于后续转换描述。没有修改这些能力/创建标志来绕过检查。

## 原始缓冲区与 CPU 平铺公式对照

RGBA layout2的原配scalar地址函数为VA`0x2bbd38`，NEON批处理为`0x2bf718`。`inspect-quest2-tiled-readback.py`转写其RGBA地址运算，读取copy-context的+312/+316/+320。实机分别为8/16/0；+312=8选择额外的地址bit10异或，另一分支是4通道公式。这里只描述CPU地址运算，不把这些数字直接等同于Mesa或硬件寄存器中的同名字段。

离线QEMU把原始库只读映射后调用原函数，4/8模式、bank参数13..16、额外位0/1、多种pitch和两种内部chip参数共39832例，脚本地址与原函数完全一致。另有1718272个地址的唯一性/范围自检；这些是公式验证，不代替设备图像验证。

`SQ2_READPIXELS_CAPTURE=1`选择单尺寸client测试并安装一次记录hook。额外显式开关`SQ2_READBACK_SAVE_SOURCE=1`只允许format28、layout2、单样本、可读映射覆盖、`length=pitch*padded_height`且不超过16MiB的源快照。源地址为image+16，pitch为+24，length为+32。`/proc/self/mem`不能读此DRM映射，后续改由`write(fd, source, length)`执行受限kernel copy_from_user；完整短写/错误处理，固定私有目录O_EXCL/NOFOLLOW创建，重复文件拒绝覆盖。它不修改像素或descriptor，原始数据只留out。

三个phase0原始缓冲区的离线结果：

| 尺寸 | pitch / padded height | 4通道公式 | 8通道公式 |
| --- | --- | --- | --- |
| 640×360 | 2560 / 368 | 全230400像素正确 | 115200像素错误、460770错误字节 |
| 264×136 | 1280 / 144 | 全35904像素正确 | 17920像素错误、71663错误字节 |
| 640×384 | 2560 / 384 | 全245760像素正确 | 失败 |

640×360首错在(x=64,y=0)，8通道公式读到(x=80,y=8)，与实际client读回首错和值一致。一般置换为：若`((x>>6)^(y>>4))&1`则读取`(x^16,y^8)`，否则坐标不变。该证据确认这些实际源缓冲区采用4通道布局，而CPU转换选择了8通道；padded height368本身不是这批错误的原因。

## 初版 CPU 4通道/bank16 候选：保留作对照

`prepare-quest2-cpu-layout.py`固定全部8个linked donor和4个适配器/探针哈希，仅改独立CPU转换器初始化的一条指令：VA`0x2a6100`从`ubfx w6,w8,#10,#4`改为`mov w6,#4`，W6只写入转换器+312。转换器是单独分配的464字节对象，观察到的vtable两个槽对应CPU复制；source capability word+80、GPU编程/身份、bank字段和阈值均不改。输出`gles-linked-cpu-layout/`，GLES SHA256 `14dca59473e08dc2e797a7eb9d77ee623b28fe1c2592da654ac1bd3e821b51d5`，原件和清单保留在reference子目录。

私有runner用`SQ2_GLES_CPU_LAYOUT=4`启用，不能与零阈值候选混用。实机只读字段确认为4/16/0；414项矩阵从221项失败降到7项，全部client路径通过。剩余6项像素失败是64×64、640×360、640×384的连续PBO offset3、两个phase：buffer要求从259写入，却实际从256开始，六组错误计数都与“整个正确RGBA流前移3字节”逐项吻合。第7项为原有零高度PBO错误码。不能把414项宣称全过。

YV12 640×360 client、NV12 264×136 PBO均三帧通过；三个尺寸精确relay仍6组全过，引用回零，原Mesa/boot/框架PID保持。**扩大到1024×128和2048×128、触发额外bank地址运算后，client再次出现像素错误**；这两组各54项中31项失败，说明只改4通道不够。下述 bank15 候选已替代它作为后续实验基础；脚本无参数仍保留原 bank16 输出，不能把默认脚本参数误解为当前最佳候选。

## bank 地址公式与 CPU 4通道/bank15 候选

两个新增 phase0 源快照分别为1024×128、pitch4096、524288字节，以及2048×128、pitch8192、1048576字节；两者 padded height 都为128。完整原始字节与逐坐标 RGBA oracle 比对时，mode4、bank13..16、extra0/1 共八种组合中，**只有4/15/0在两个尺寸均为零错误**。4/16/0分别有81920和98304个错误像素；首错均为 `(256,16)`，实际RGBA `[48,208,176,112]`、预期 `[48,208,176,176]`，与实机 client 首错一致。全部候选访问仍位于快照范围，排除了这两次比较中的截断快照问题。

地址公式由原配 scalar 函数逐指令转写并经前述 QEMU 对照。令 `xn`、`yn` 是像素坐标的第n位，`^` 是异或；RGBA8 的4通道公式先计算 `O = (y & ~15) * pitch + 4 * P`，其中 P 的各位为：

| P位 | 来源 |
| --- | --- |
| 0、1、2、3、4、5 | `x0, y0, x1, y1, x2, x3` |
| 6 | `x4 ^ y3` |
| 7 | `x4 ^ x5 ^ y2 ^ y3` |
| 8 | `x4 ^ y2`；原8通道分支再异或 `x6 ^ y4` |
| 9 | `x5` |
| 10及以上 | `x6`及以上 |

随后令 `S = pitch << 4`、`b = bank_bit`。在本次观察到的 `extra=0`、内部chip参数 `0x5002a8` 下，若 `S` 可被 `2^b` 整除，则 `O ^= y4 << (b-1)`；若可被 `2^(b+1)` 整除，则 `O ^= y5 << b`。这说明小尺寸常用 pitch2560/1280 的成功并未覆盖 pitch4096/8192 的 bank XOR。完整 extra 分支见 [inspect-quest2-tiled-readback.py](../scripts/inspect-quest2-tiled-readback.py)；这些字段名仅描述闭源 CPU 运算，不应直接替换为 Mesa/硬件寄存器同名值。

独立候选由 `python3 scripts/prepare-quest2-cpu-layout.py --bank-bit 15` 生成到 `out/quest2-gpu/gles-linked-cpu-layout-bank15/`，输入仍固定为原 linked GLES `99d4c18d35e69e8589f2550b242d5f6722031404852b9831e328df8d1778551f`，输出 SHA256：

`71eba2e1d10daaf842f8667654af711c63582dc9be3c42df14d6d4191faac668`

只改变两条 CPU 转换器赋值来源，共6个文件字节；此固定 ELF 的这两个VA恰与文件偏移相同，脚本仍按 executable LOAD 校验映射：

| VA | 原指令与小端字节 | 候选指令与小端字节 | 作用 |
| --- | --- | --- | --- |
| `0x2a6100` | `ubfx w6,w8,#10,#4` / `06350a53` | `mov w6,#4` / `86008052` | 在 `0x2a6108` 写 CPU converter+312。 |
| `0x2a61b0` | `mov w10,#16` / `0a028052` | `mov w10,#15` / `ea018052` | 在 `0x2a61b4` 写 CPU converter+316。 |

后一个位置是**最终 selector16 分支**，不是 `0x2a6104` 的初始默认值：实机 `converter+72=1`、`(+76 & 127)=16`，通过 VA `0x22b0c` 的表 `00 05 0a 1c` 跳到 `0x2a61b0`。改初始默认值会被该分支覆盖。13/14/15分支保持原样；selector16 写入后，W10在两个后继分别由 `0x2a6178` 的 `mov #1` 或 `0x2a61c0` 的 `mov wzr` 重定义，改MOV不改变NZCV。独立审计确认初始化函数后续无同字段/重叠写入；接收parent的后续构造函数只读取其字段并写新child，allocator回调也未传递该bank字段。源能力字+72/+76/+80、extra+320、GPU身份/寄存器编程及读回阈值均保持不变。

私有 runner 使用 `SQ2_GLES_CPU_LAYOUT=4-bank15`。两个大步长矩阵各有40项像素/哨兵检查及14项错误/状态检查，实机结果如下：

| 尺寸 | 初版4/16/0失败数 | 4/15/0失败数 | 剩余失败 |
| --- | --- | --- | --- |
| 1024×128 | 31/54 | 3/54 | 两个phase的连续PBO offset3，以及零高度PBO错误码。 |
| 2048×128 | 31/54 | 3/54 | 两个phase的连续PBO offset3，以及零高度PBO错误码。 |

这两组的全部client检查已通过，含子矩形、PACK row length/skip和非对齐客户端起点；其余PBO像素路径也通过。剩余 offset3 仍是要求从259写却从256开始，不应归为bank错误。

### 反向上传的独立对照

`--upload-relay` 从客户端精确RGBA图案调用 `glTexSubImage2D` 写内部纹理，再由整数坐标 `texelFetch` 采样到真实 modifier0 的线性AHB，最后按CPU lockPlanes的实际步长比较所有字节。此路径**不调用 `glReadPixels` 或PBO**，避免用同一个错误CPU解平铺器自证正确。尺寸为640×360、264×136、640×384、1024×128和2048×128，各两个phase：

| 驱动 | 通过/总计 | 错误字节 |
| --- | --- | --- |
| Mesa | 10/10 | 0 |
| 原闭源 | 0/10 | 3800993 |
| CPU4/bank15 | 10/10 | 0 |

因此这两个局部赋值修复不仅改善已测GPU→CPU读回，也修复了这五个RGBA8尺寸的CPU→GPU上传；仍不能外推为所有像素格式、压缩、多平面、mipmap或并发路径均正确。探针通过 `SQ2_READPIXELS_UPLOAD=1 --readpixels` 选择独立模式。

离线完整矩阵保存在忽略的 `out/quest2-gpu/readpixels/bank-formula-matrix.json`；实机原始日志为 `cpu-layout4-bank15-{upload,1024,2048}.txt`，上传对照为 `mesa-upload-relay.txt`、`closed-upload-relay.txt`。这些原始数据不加入Git。

## PBO非对齐起点：已验证的保守fallback候选

[prepare-quest2-pbo-copy.py](../scripts/prepare-quest2-pbo-copy.py) 在上述bank15副本上仅增加一条分支修改：VA `0x1bb758` 的 `cbz w0,0x1bb7d8`（`00040034`）改为 `b 0x1bb7d8`（`20000014`）。原client路径本来就去这个后继，修改时尚未取得PBO临时资源或额外引用；原fallback映射PBO，在 `0x1bc144` 加入完整byte offset，完成传输后解映射。它没有改变CPU布局、能力或原来的 `UINT32_MAX/0` 阈值。

候选目录为 `out/quest2-gpu/gles-linked-cpu-layout-pbo/`，GLES SHA256为 `677287b130ac3ac0dfd2c0a8bc1ac73b642466c64f67d39f662a2bb09d823ded`；runner显式使用 `SQ2_GLES_CPU_LAYOUT=4-bank15-pbo`。实机原414项矩阵结果为 **400项像素/哨兵检查全部通过、0错误字节，整体413/414通过**；唯一失败仍是零高度PBO应无错误却返回 `GL_INVALID_OPERATION`，日志为 `cpu-layout4-bank15-pbo-matrix.txt`。

这是一项正确性fallback：关闭原direct-to-PBO优化，已测传输改走原CPU复制路径，可能增加CPU占用并同步阻塞。真正执行GPU地址截断的最终指令尚未隔离，不能把该分支修改称为GPU pack根因修复或性能提升。414项通过范围也不代替新增大步长、其他格式、真实应用/SF和整系统回归；不写入正常启动镜像。

## 零面积PBO验证：当前组合候选与完整回归

原测试的 `width=1,height=0`、RGBA/UNSIGNED_BYTE、512字节未映射PBO、offset256是合法空传输，预期应为 `GL_NO_ERROR` 且buffer不变。OpenGL ES 3.0.6 §4.3.2.1按宽高定义像素集合，零高度时集合为空；§4.3.2.4的PBO越界条件取决于实际所需的存储访问。该用例不需要任何像素写入，不能凭一个不存在的末行产生越界错误。尺寸为零也不豁免格式、映射状态、FBO等其他验证。[Khronos规范](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf#page=206)、[官方glReadPixels错误条件](https://github.com/KhronosGroup/OpenGL-Refpages/blob/main/es3.0/glReadPixels.xml)

原验证器 `0x1ed580–0x1ed598` 的末行公式为 `offset + uint32((height-1)*stride) + uint32(width*bytes_per_pixel)`。在这个 stride4 的用例中，计算得到 `0x100000100 > 512`，在核心函数处理零面积之前误报 `GL_INVALID_OPERATION`。这是静态指令推导，与实机错误码吻合；没有把测试预期改成接受错误。

[prepare-quest2-zero-area.py](../scripts/prepare-quest2-zero-area.py) 以已核验的PBO fallback副本为输入，将入口 VA `0x1ed580` 改为跳至 `0x3ea2a0` 的 **20字节stub**：

```asm
cbz w12, empty_trampoline  // width=0；短跳到stub+16
cbz w9,  empty_trampoline  // height=0；同一短跳目标
sub w3, w9, #1            // 非空：重放被替换的原指令
b   0x1ed584              // 回到原末行计算
empty_trampoline:
b   0x1ed61c              // 空区域：跳过末行范围计算，保留后续FBO验证
```

20字节而非16字节，是因为CBZ不能直接跨越所选stub与原验证函数之间的距离；两个CBZ先短跳到stub内的无条件B。这个补丁保留之前的负尺寸、format/type、mapped PBO和offset元素对齐检查，之后也继续原FBO/sample检查，未在API入口直接返回。非空维持原算术；CBZ、SUB及B不改变NZCV，也不引入新的寄存器或栈要求。

脚本固定输入/输出哈希和入口/后继指令，核验程序头、节、符号（含mini debug data）、RELA/APS2/RELR目标和尾部零填充；只把已审计RX LOAD的filesz/memsz在现有末页内延长20字节，节表不变，不能把任意零填充当成可用代码位置。实际分支字节在QEMU按原相对距离执行，176组尺寸/标志组合的所有GPR、NZCV及空/非空路由全部通过，并通过独立代码审计。这是stub语义检查，不代替完整驱动或API实测。

当前候选目录为 `out/quest2-gpu/gles-linked-zero-area/`，runner选择 `SQ2_GLES_CPU_LAYOUT=4-bank15-zero-area`；GLES SHA256为：

`c47968472a01e3d2a69dad87d3e000fe7914a0f4e0198c6c70e389ba43219476`

在Surface的串行私有探针回归已完成：

| 检查 | 结果 | 本地日志 |
| --- | --- | --- |
| 原10尺寸PACK/client/PBO及错误矩阵 | 414/414；0 GL错误码不匹配、0坏字节 | `readpixels/zero-area-matrix.txt` |
| 1024×128大步长矩阵 | 54/54；0 GL错误码不匹配、0坏字节 | `readpixels/zero-area-1024.txt` |
| 2048×128大步长矩阵 | 54/54；0 GL错误码不匹配、0坏字节 | `readpixels/zero-area-2048.txt` |
| CPU texSubImage→GPU采样→AHB CPU | 五尺寸、两phase，10/10；0坏字节 | `readpixels/zero-area-upload.txt` |
| GPU绘制内部纹理→GPU采样→AHB CPU | 同五尺寸、两phase，10/10；0坏字节 | `readpixels/zero-area-relay.txt` |
| YV12 640×360，client读回 | 3/3帧；0错误通道，最大色彩转换误差1 | `gles-linked-zero-area/yv12-640.txt` |
| NV12 264×136，PBO读回 | 3/3帧；0错误通道，最大色彩转换误差1 | `gles-linked-zero-area/nv12-264-pbo.txt` |

表中日志都位于忽略的 `out/quest2-gpu/`。RGBA精确oracle没有容差；YUV的最大误差1是另一项色彩转换判定，不能混用。YUV跟踪实际读取到CPU converter为4/15/0、阈值仍为 `0xffffffff/0`。全部runner结束后正常Mesa库哈希、boot ID及框架PID保持不变，未写入启动镜像。

还需覆盖非默认PACK row length/skips下的0×0/0×1/1×0，以及零面积同时遇到错误format/type、无效FBO、mapped PBO、真正多字节格式的offset对齐等组合；当前414项只覆盖其已有的基础错误用例。其他像素格式、压缩、多平面、mipmap、并发、真实应用/SurfaceFlinger及性能回归仍未由这些结果证明。PBO复制仍是可能同步阻塞的CPU fallback，完整闭源替换和正常启动默认值保持未变。
