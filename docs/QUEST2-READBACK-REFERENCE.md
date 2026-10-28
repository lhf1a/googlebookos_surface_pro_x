# Quest 2 v77 GLES 读回路径参考

2026-10-28。本文记录原配库的离线反汇编证据，以及供私有探针使用的只读诊断接口。不是已完成的读回修复，不改变正常 Mesa 启动，也不修改原库代码或内存。

## 固定输入与地址

原配输入由 [quest2-gles-v77-inputs.json](../configs/quest2-gles-v77-inputs.json) 固定：

| 库 | 原文件 SHA-256 |
| --- | --- |
| `egl/libGLESv2_adreno.so` | `a0d4d099a67d4a0db54ee6b5cd69e15e1787484128667120c7a90d6ae333b316` |
| `libadreno_utils.so` | `8bca58979c908b1da4b7caf064c65a5b72eb8ae6989a316e63611b2c1597eb57` |

GLES 的 GNU Build ID 是 `af16b8ea19e72ea9d26d9bb477003db3`。本文所有地址均为该 ARM64 ELF 的 **VA**，运行时地址为已验证的 load bias 加 VA，不能直接当作文件偏移。`objdump` 对已剥离内部函数显示的最近导出符号名通常没有语义意义。

`prepare-quest2-gles-linked.py` 用 `patchelf` 修改 RPATH，副本的整文件哈希会变化，GNU note 也可能移动；原版及当前 linked 副本的 `.text` VA 和上述 Build ID 保持相同。运行时诊断仍要求启动脚本先检查该候选的 `SHA256SUMS`；Build ID 和少量指令匹配只是结构版本限制，不能代替整文件完整性检查。

## 问题范围

主线程的对照结果把问题缩小到读回路径：640×360 内部 RGBA 纹理直接读到客户端内存失败；同一内部纹理再被 GPU 采样到线性 RGBA AHB 后，`glReadPixels` 和 AHB CPU 读取均通过。640×360 PBO 读回通过，但 264×136 PBO 对照失败，故 PBO 本身还不是通用解法。详细矩阵由 [QUEST2-GPU-RESEARCH.md](QUEST2-GPU-RESEARCH.md) 维护。

这些结果不支持直接宣称内部纹理内容、UBWC、GMEM 或 A680 身份是根因。小尺寸与非对齐高度失败需要进一步区分 CPU 转换、布局参数和策略选择。

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
- 确认后取 `real=*(p+8)`、`settings=*(real+168)`，只读取两个 `uint32_t` 阈值；不写任何地址、不调用内部函数、不输出 heap 地址。结束时恢复调用前 `errno`。

Android NDK 构建不需要额外链接库：`pthread_getspecific`、`pread` 和 stdio 都由 Bionic libc 提供；头文件自身不需要 `-ldl` 或 `-lpthread`。非 Android 主机做编译检查可用 `-pthread`。默认不应把该诊断接入正常启动或持续日志。

头文件已用 NDK r28c 的 `aarch64-linux-android35-clang` 和主机 C 编译器通过 `-O2 -Wall -Wextra -Werror` 检查；主机没有 donor 映射时按预期输出 `unavailable reason=unique_donor_mapping`。修正 top-byte tag 后，主线程在 Surface 上对默认、私有配置和 PBO 探针完成上述只读字段检查。该验证不涉及写入内部 settings，也不是读回修复。

## 压缩与芯片识别的边界

原配 GLES 在 `0x26b778–0x26b784` 明确比较 `0x06080001` 并进入 `0x26c280` 分支。`libadreno_utils` 的 `isUBWCSupportedByGpu`（VA `0x101a8`）参数是像素格式，不是 chip ID；它内部在 `0x103b0/0x103bc` 明确处理 A680.1。因此 GSL 的 “Unknown extended properties” 不能推导为这些原库完全不识别 A680。

原库包含 `GL_EXT_texture_storage_compression` 字符串。官方扩展的 `glTexStorageAttribs2DEXT` 接受 `{GL_SURFACE_COMPRESSION_EXT=0x96C0, GL_SURFACE_COMPRESSION_FIXED_RATE_NONE_EXT=0x96C1, GL_NONE}`，但 NONE 只禁止固定码率压缩，而且也是默认值，不能据此宣称所有无损 UBWC 均已关闭；该扩展还要求 GLES 3.0。[Khronos 扩展规范](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_storage_compression.txt)

下一步应对比相同最终阈值下直接客户端与 PBO 的实际资源/传输描述，检查宽高、pitch、布局标志及临时拷贝分支是否不同。目前没有有效字段均为 0 的实测，也未确定具体 detile 地址公式；不能把本轮零配置实验称为已完成的“强制 GPU 读回”测试。
