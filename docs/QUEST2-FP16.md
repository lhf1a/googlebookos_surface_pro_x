# Quest 2 GLES FP16 适配与实测

2026-10-28。为实际SurfaceFlinger的384×384 shader-cache预热补齐FP16；当前仍是私有候选，正常启动保持Mesa。

## 改动

`quest2-mapper5.h`保留PlaneLayouts真实components、plane sample尺寸及subsampling；`linear_rgba_layout`逐项核对RGBA类型、offset、位宽、fourcc、step、stride、边界和非protected/linear/单层条件。RGBA8继续使用32-bit AB24；FP16为64-bit AB4H，R/G/B/A分别16位、offset0/16/32/48，水平/垂直采样1。metadata顺序可变，分量不能重复或缺失；RGBX/BGRA仍明确拒绝，不靠改format伪装支持。

mapper3的代理准入与getter统一使用该布局验证；宽度按真实stride字节与sample_bits换算，物理height语义保持。mapper4仍限制原RGBA8，不替换设备上的原mapper4。旧Vulkan AHB/native适配器及donor保持，只有14库集合中的mapper3一项变化。

私有候选v2首次共存测试退出139：发现新旧库以相同弱符号导出不同BufferInfo布局的color_info辅助函数，具有跨DSO写错布局的确定缺陷；未取得该SIGSEGV完整栈。现将整个metadata头内C++类型/辅助函数隐藏，保留公开C/HIDL边界，防止私有ABI符号抢占。v3的dynsym不再导出这些helper，两加载顺序回归通过。废弃v2不再可由runner选择，不能部署。

## 固定候选

`combined-v3`从已验证combined-v1逐字复制13库，只替换新mapper：

```text
libqti-mapper3-bridge.so
e9eaf75316eb4285bf8e8f79acefbabb72790232baa353eccd6cee9cf44884dc
```

GLES/Vulkan/GSL/ION/AHB/wrapper保持v1原字节。prepare验证完整输入hash、新mapper hash和私有helper未泄漏到动态符号表，拒绝已有输出。新增matcher对应resolver GOT offset0x1aab0，仅用于这个固定版本；其他13项实际绑定规则保持。

## 实机证据

FP16探针使用真实CPU写入FP16源AHB → 早销毁EGLImage后的纹理 → GPU texelFetch → FP16目标AHB → 真实GPU native fence → mapper5 CPU锁。源包含精确binary16的0、1/8、1/4、1/2、3/4、1、2和−1/2，逐16位bit比较，alpha为1；目标预填不同sentinel。没有把FP16改成RGBA8，没有成功路径glFinish，不把CPU拷贝作为GPU绘制。失败cleanup可glFinish。

| 检查（最终v3） | 结果 |
| --- | --- |
| Mesa FP16对照384×384三轮（新隐藏helper前版本的同一像素oracle） | 全部bit一致 |
| 384×384连续150轮 | 全部bit一致、FD稳定10、引用回零 |
| 13×7 / 2880×1920各3轮 | 全部bit一致、FD稳定10、引用回零 |
| RGBA8异步多层合成150轮 | 通过、FD稳定11 |
| Vulkan compute/image/raster + AHB网格三轮 | 通过 |
| 同进程两加载顺序、13项真实GOT绑定 | 通过，各GLES3轮/Vulkan12帧 |
| 原414项读回/错误检查 | 全通过 |
| color-layout纯CPU正/负例 | 34项通过，覆盖RGBA8、FP16、分量顺序、缺失/重复/错误分量、stride、采样和protected拒绝 |

日志位于忽略目录out/quest2-gpu/fp16-probe，最终证据以v3-*命名；closed-*为visibility隔离前的阶段测试，不代替最终版本。FP16 probe SHA为`eab47b5829f48cad2fbb9eaeeebe91c89c641dad6d43ba66888a4cf590f9551a`，layout测试为`5781df4690c897175a01a7540c2e23ff326787ababd62ef648d55c1070793c4a`。

## 实际桌面仍未通过

第二次真实SF限时切换使用v3，操作员确认黑屏/卡住，SSH未恢复；独立watchdog是否完成无法读取。随后用户长按电源重启，已重新连接并确认原Mesa EGL/Vulkan完整hash、boot_completed=1、screen-only=Y。boot ID变化与用户手动重启一致，不能记录成自主board crash。

重启后pstore为空，没有保留下来的SF tombstone；原watchdog状态/maps位于RAM /tmp并已丢失。当前不能判断是GPU/内核级挂起、SF阻塞、回退忙挂载或其他问题，更不能把离屏FP16通过当作桌面修复。不要原样重复全局切换。

后续脚本已增加受限trial目录内持久化maps/进度记录、busy挂载的受身份校验lazy detach回退；只通过语法检查，尚未在设备执行。持久化是尽力留证，不能保证硬卡死前最后写入落盘；watchdog也不能修复内核/GPU整板死锁。下一步需补独立有限kernel日志采集、定位真实SF挂起点，并继续系统格式/颜色空间/正常日志控制，最后再做可恢复SF试验和持久化。

## 构建与复现

先`bash scripts/build-quest2-v77-ahb.sh`生成新mapper，只取该文件，不把同时重建的AHB库部署进去。复制到out/quest2-gpu/fp16-probe/libqti-mapper3-fp16-hidden.so，patchelf设置RUNPATH为/vendor/lib64/egl，再运行：

```sh
python3 scripts/prepare-quest2-fp16.py
bash scripts/build-quest2-fp16.sh
bash scripts/build-quest2-combined.sh
```

同步完整combined-v3、新FP16探针/清单、namespace helper/系统清单/runner。retained root运行：

```sh
SQ2_COMBINED=1 SQ2_COMBINED_REVISION=3 SQ2_FP16_CYCLES=150 \
  sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-gles-system.sh --fp16
```

FP16_WIDTH/HEIGHT使用SQ2_FP16_WIDTH/HEIGHT，默认384；cycles默认3。所有GPU测试串行并保留外部timeout。实际桌面切换当前不作为通过步骤复跑。
