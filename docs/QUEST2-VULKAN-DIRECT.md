# Quest 2 v819 Vulkan：独立 Direct 模式对照

2026-10-28。本文记录离线反汇编证据和私有对照库。后续已在 Surface 独立进程通过基础读回并确认配置读取，**尚未证明它能修复 Wild Life 花屏**。默认 Mesa、系统库、内核、频率和启动镜像未改变，完整闭源 EGL/Vulkan 替换仍未完成。

## 输入和输出

| 输入 | SHA256 |
| --- | --- |
| 原始 v819 `vulkan.adreno.so` | `b4b4132c3f6c1092563200686d21933ed83ffab865a1f99edda93218e96e1e27` |
| 已有私有 `runtime/vulkan-bridge.so` | `c3563d942c3bd4d7d9c2d33b99eaf9a5af0f145f74cd19f229a432d6d2870ec2` |
| v819 `notgsl.so` | `9446c108708ed89f72199f992b5b7a1300795b0831adf34c08cb7b61f920daa5` |
| 原配 Quest 2 v77 Vulkan | `0f316d0331ef5b4ec60d8d3f21a845f47583b14ccaabe54f316e619b9897bd8d` |
| 原配 Quest 2 v77 GSL | `8971ade1009b02ca49a19cde9a78e324b2f7b868253f614c337105bbb669cfb6` |

新的 `scripts/prepare-quest2-direct-mode.py` 从已有私有 v819 runtime 生成独立目录：

```sh
python3 scripts/prepare-quest2-direct-mode.py
```

默认输出为 `out/quest2-gpu/vulkan-direct-runtime/`。脚本要求输出是仓库 `out/` 内的新目录，不能覆盖或嵌套输入 runtime；再次执行需指定另一个 `--output`。先验证原始 Vulkan、已有 runtime Vulkan 的固定哈希及 manifest 中全部依赖，再创建输出。

输出 Vulkan SHA256：`3e01f9acaab3dc6eaf4239da317b78b4a4514a91cbf5d129b45b5e0c94897b27`。与输入 runtime 长度完全相同，**仅 10 个字节改变，全部位于已确认目录字符串槽中**。六个依赖库均逐字节保持不变；原有 mapper bridge、SONAME/依赖改动和 optional-profile 规避保留。SHA256SUMS 与 direct-mode-manifest.json 记录全部产物及变更偏移。

唯一改变：文件偏移 `0x6e6ef` 的 `"/data/vendor/gpu/\0"` → `"/data/sq2vk-test/\0"`。两者均为 17 字符加 NUL。紧邻的 `qgl_config.txt` 文件名保持不变。库内另一处同样的目录字符串位于 `0x6c9f4`，没有修改；脚本不使用全局字符串替换。

这使新副本读取 `/data/sq2vk-test/qgl_config.txt`，原驱动仍使用原路径。脚本只生成本地文件，不创建设备目录、不写全局 `/data/vendor/gpu` 配置，也不选择应用驱动。

生成的配置为：

```ini
0x0=0x08675309
RenderMode=Direct
```

## 配置格式的证据

以下地址是上述 v819 原始 ELF 的虚拟地址；本文列出的代码和 rodata 地址同时等于文件偏移。可写 `.data` 中的表地址与文件偏移相差 `0x1000`。不要使用 objdump 显示的最近导出函数名称给这些未命名内部函数命名。

| 位置 | 观察 |
| --- | --- |
| `0x29a154–0x29a174` | 初始化搜索路径表，目录来自 `0x6e6ef`，即 `/data/vendor/gpu/`。本次没有找到 CWD 搜索支持。 |
| `0x29a18c–0x29a198` | 从 `0x6e701` 取 `qgl_config.txt`。 |
| `0x29a2e0` | 调用 `fopen`；成功路径含 `Found a QGL Settings File: %s` 字符串（`0x6c920`）。日志是否输出还受驱动日志状态影响。 |
| `0x29a42c` 及其后 | 每行至多 512 字节；分隔符 `0x65ad8` 为 `=`，空白表 `0x683ed` 为 ASCII 空白。`;` 开头为注释。 |
| `0x29a63c–0x29a684` | `0x` 开头键直接按整数处理；其他键转 ASCII 小写，初值 `0x425534b3`，逐字节执行 `ROL32(hash, 5) XOR byte`。 |
| `0x29a6f0–0x29a71c` | 键 ID 为 0 时解析值；只有 `0x08675309` 才将配置启用标志置 1。缺少此头时，普通配置键不被接受。 |
| `0x403a7c–0x403a9c` | 匹配 `0x1fd41fd8`，该值正是 `RenderMode` 的上述哈希；目标指针为 settings + `0x21dc`。 |
| `0x403cd0–0x403ce4` | 将值交给内部枚举解析器 `0x3b25e8`，枚举表 VA `0x4d2530`（文件 `0x4d1530`），共 7 项。 |

枚举表每项是两个 32 位数 `(name_hash, numeric_value)`。解析器的字符串哈希与键相同；十进制数字按枚举数值验证，`0x` 格式则按枚举名称哈希查表，因此本次使用已确认的名称 `Direct`，避免把十六进制值误当枚举数值。

| 名称 | 名称哈希 | 枚举值 |
| --- | --- | --- |
| Direct | `0x1e3dd538` | 0 |
| HwVizBinning | `0x55215024` | 1 |
| Binning | `0x467f0c25` | 2 |
| HwVizDirect | `0x16a727d8` | 3 |
| 未解码 | `0xa83e9042` | 4 |
| 未解码 | `0x4b049807` | 5 |
| Max | `0x9a581972` | 6 |

名称哈希匹配和实际枚举解析代码共同支持 `RenderMode=Direct`，不依赖猜测 `ForceGmem`、环境变量或 GSL 面板参数。

## Direct 与 GMEM 的关系

`0x2a3128` 读取 settings + `0x21dc`。`0x2a312c–0x2a3134` 仅在枚举为 1 或 2 时跳到 `0x2a3730` 的 GMEM 相关分支；其他值通过 `0x2a3138–0x2a315c` 清除一个 renderpass 标志，并把 renderpass + 100 的原因设为 7。

对应原因表 VA `0x4d2b60`（文件 `0x4d1b60`）的第 7 项为字符串 `Not rendering to Gmem`（`0x5d1e5`）。因此可以确认：驱动在该 renderpass 配置路径把 Direct=0 归为非 GMEM。这是实施 Direct 对照的静态依据；实际是否读到配置、是否有后续 profile/per-bucket 覆盖，以及所有场景的渲染结果仍需观察。

另一个原因表还包含 `Per-Bucket RenderMode Setting`、`Determined by app profile`、`Qcom Render Mode Control` 等路径。不能仅根据配置文件存在就认定每个 draw 都使用所选模式。

## 原配 v77 与 GSL 的范围

原配 v77 Vulkan 也有 `qgl_config.txt`，配置头比较位于 `0x11f614–0x11f62c`，RenderMode 键常量构造位于 `0x2725c4/0x2725cc`；同样的前四项枚举表位于文件 `0x313640`。这说明配置机制并非只有 v819 包才有。本脚本**不接受 v77**，也没有把 v819 的偏移应用到 v77。

v819 GSL 的 `parseFile`（`0x1f4538`）搜索 `./<filename>`、`/data/vendor/gpu/<filename>`、`/data/misc/gpu/<filename>`；`get_panel_settings`（`0x206a20`）使用 `adreno_config.txt` 和 `yamato_panel.txt`。这些是不同的配置机制。本次没有证明 GSL 的 `YAMATO_FORCE_GMEM_SHL` 等字符串是 Vulkan sysmem 开关，未对其作改动。

四个库都带 `.gnu_debugdata`，已离线解包到忽略的分析目录。内部符号名主要被散列混淆，可用于函数边界，不能当成可读官方实现源码。

## 下一步有界实测

### 已完成的独立进程验证

候选 runtime 只部署至 Android `/data/local/tmp/sq2-closed/vulkan-direct-runtime`，配置位于唯一私有目录 `/data/sq2vk-test/qgl_config.txt`。逐个 SHA256SUMS 通过后，真实候选通过计算、optimal 图像 clear/copy、八帧三角形和 64×64 AHB GPU/CPU 读回，返回 SQ2_VULKAN_READBACK_OK。随后针对该配置路径的 strace 实际记录两次 open/read，均读取完整 33 字节 `0x0=0x08675309\nRenderMode=Direct\n`，该轮离屏读回也通过。

这证明运行中的候选读取了预期文件并通过这些基础场景；仍未观测每个实际 renderpass 的模式，未复测 Wild Life 或窗口呈现，不能宣称复杂场景花屏已经解决。实测输出留在忽略的候选目录。最终 boot ID、system_server/SurfaceFlinger PID 不变，默认 Mesa Vulkan 哈希仍为 `0b36eeba…`，screen-only=Y；GPU fault/hang 筛选无输出。

### 剩余验证

1. 仅部署这个独立 runtime 和其私有配置，在单独测试进程确认它打开 `/data/sq2vk-test/qgl_config.txt`；保留原 runtime 用于对照。不要把配置写入全局目录或替换 SurfaceFlinger 的库。
2. 先复用已通过的离屏三角形/多尺寸图像读回；配置读取的日志或跟踪、真实驱动库哈希和像素正确性一起记录。出现 fence 超时/GPU fault 便停止，不原样重复造成黑屏的测试。
3. 基础读回通过后，再由现有、可撤回的 3DMark 专用选择路径进行 Direct 对照，检查前中后场景画面。Wild Life 跑完分数不是正确渲染的证据。
4. Direct 若修复花屏，只说明非 GMEM 路径可用，不能证明 A680 GMEM 根因或完整系统替换完成；若没有变化，先排除配置未启用/被覆盖，再调查压缩、子通道、同步和设备配置差异。

已有 Mesa 的 A680.1 + 3DMark sysmem 修复不能直接当作闭源驱动同一故障的证明。当前原配 v77 GLES 的共享缓冲区失败也不能仅用这个 Vulkan 配置解释。
