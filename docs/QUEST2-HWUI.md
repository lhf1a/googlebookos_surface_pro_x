# 真实 HWUI 与硬件 Bitmap 上传复现

2026-10-28，combined-v5；正常启动、桌面和启动镜像仍为 Mesa。本轮没有全局切换。

## 设备证据

`quest2-hwui` 使用实际 HardwareRenderer/RenderNode，在私有namespace由原系统EGL加载候选，输出到ImageReader，绕过SurfaceFlinger。三帧包含普通Bitmap、HARDWARE Bitmap、缩放、alpha、向量、渐变和文字；保存软件参考与实际PNG并比较稳定区域。最初standalone app_process缺字体初始化，Mesa对照在CPU drawText就abort，不是驱动问题。依据实际framework.jar，空字体map时调用原Typeface.setSystemFontMap(SharedMemory null)，验证default/native对象后继续。修正后结果：

- Mesa三帧全部通过；普通/硬件/缩放位图严格像素一致；alpha允许1、gradient允许2，文字强覆盖全部匹配。
- 闭源三帧仅HARDWARE Bitmap失败，每帧4096像素全部显示背景；普通Bitmap、缩放、alpha、向量稳定区、渐变、文字均通过。
- 实际maps验证libhwui与选定Mesa/闭源GLES及私有编译器，结束保持正常桌面PID、boot ID和Mesa hash。
- opt-in SQ2_HWUI_READBACK=1将硬件位图立即copy回ARGB_8888：Mesa全部精确一致；闭源各帧4096像素读回为透明零。它经过RenderProxy GPU同步路径，不是直接mmap原内存，不能单独断言原始fd内容。
- 实际硬件位图64×64，format1，usage严格0x100。

实际libhwui反汇编：nativeCopy表项0xa34660 -> 0x57fd20；HARDWARE helper0x3201e0以usage0x100分配AHB（0x32031c），upload vtable+64为0x7451b0。路径为eglGetNativeClientBuffer/eglCreateImage→glEGLImageTargetTexture2DOES（0x7456b8）→glTexSubImage2D（0x7456e8）→fence/flush。copy回软件通过RenderProxy#copyHWBitmapInto readback。原始反汇编只在out。

## 原生复现区分上传与绘制

新增`quest2-usage`不CPU-lock源，保留请求usage和实际metadata，GPU写或GL上传源，再采样到独立CPU-readable输出；通过native fence后逐字节oracle。普通两组源usage0x900与0x10000900在72²各3轮GPU绘制/采样通过；0x10000900在2880×54也通过。这暂不支持仅清除private bit28就能修复的结论，见QUEST2-ICON-AUDIT。

`SQ2_USAGE_UPLOAD=1`把源初始化改成对导入EGLImage纹理执行glTexSubImage2D，不向源加CPU usage，也不把sample-only源挂FBO。64²、usage0x100时：

- Mesa原命名空间对照三轮0坏字节，FD稳定15。
- 闭源第一轮16384字节中16351字节错误，采样为0，无GL报错；按失败退出1。
- 闭源usage0x900同样失败，因此不是单纯缺GPU_COLOR_OUTPUT bit。
- 默认GPU写同类外部缓冲区路径通过，普通内部RGBA8纹理CPU上传36²/72²/96²两phase也全过。

这是比截图更小的独立硬件Bitmap相关失败；同context即复现，不需要跨context、系统SF或Chrome。下一步审计donor外部/EGLImage纹理CPU上传的目标地址、缓存、提交与格式分支；不要用普通内部纹理上传通过否认此错误，也不要以系统软件bitmap回退替代闭源适配。

## 构建与运行

WSL执行build-quest2-hwui.sh、build-quest2-usage.sh、build-quest2-vulkan-device.sh；重建namespace helper并同步SYSTEM_PROBE_SHA256SUMS的全部变化文件。runner增加--hwui/--usage/--vulkan-device，只允许已有combined候选与相应独立manifest；锁和25秒外部timeout保留。

HWUI：`SQ2_COMBINED=1 SQ2_COMBINED_REVISION=5 SQ2_HWUI_OUTPUT=/data/local/tmp/sq2-closed/hwui-evidence/FRESH --hwui`，文件夹必须全新。Mesa基线加SQ2_HWUI_MESA=1，此专用入口只创建私有namespace、不覆盖Mesa目录，并核对已知Mesa哈希。不是任意模式逃过候选隔离的开关。SQ2_HWUI_READBACK=1会增加GPU同步，原始closed1无该诊断同样失败。

Usage：`SQ2_COMBINED=1 SQ2_COMBINED_REVISION=5 SQ2_USAGE_SOURCE=0x100 SQ2_USAGE_UPLOAD=1 SQ2_USAGE_WIDTH=64 SQ2_USAGE_HEIGHT=64 --usage`。Mesa版quest2-usage-mesa要求原Androidnamespace、screen-only guard，仍应持有同一串行锁和外部timeout。源码默认72²、3轮，来源usage允许0x900或0x10000900；0x100只允许上传模式。

原始输出留out/quest2-gpu/kmsg/hwui-*、usage-*、icon-upload-*，图像在设备私有hwui-evidence与本地out/quest2-gpu/hwui/evidence。错误结果是定位证据，不是已修复功能。真实状态栏错误仍可能有额外原因；Chrome Vulkan为独立阻断，见QUEST2-VK-CREATE-AUDIT。

## 上传分支对照：原始源内存与 PBO

后续实机在同一v5执行；没有改动donor或启动镜像。新增两个显式诊断选项：

- SQ2_USAGE_RAW=1：仅上传模式允许。在直接上传后glFinish，再以原fd只读mmap并用DMA_BUF_IOCTL_SYNC START/END READ检查实际源字节；不改变源分配usage。它改变同步/缓存时序，始终返回3并标记DIAGNOSTIC，不能当成普通路径通过。
- SQ2_USAGE_PBO=1：先glBufferData上传同一图案到PIXEL_UNPACK_BUFFER，然后glTexSubImage2D使用offset0；之后解绑/删除PBO，按原输出fence读回，不增加CPU源lock或glFinish。它只改变测试传输路径，不是系统API重定向补丁。

64²直接上传的RAW对照：同步后的16384字节全部为0（与图案不符16351字节），后续GPU采样也全零。由此排除了“只是输出GPU采样读回错，但源已经正确”这一解释，也未被该finish/DMA同步修复；尚未确定donor具体写错地址还是跳过上传。

PBO对照：64²三轮和150轮全部逐字节通过，FD保持10、映射归零；2880×54、64×47/48/49各三轮也过。但13×7、64×7、13×64首轮仍全零失败。不能将PBO宣传为通用修复。结果指向随尺寸/布局/传输策略变化的分支；简单75%占比假说不成立（64×47也通过），阈值或内部分支仍需实际跟踪，不能凭尺寸猜定。

原始输出在out/quest2-gpu/kmsg/upload-{RAW,PBO,pbo-*}.txt。全部测试结束正常Mesa哈希、boot ID与SF/system_server PID保持。下一步应跟踪同一输入在direct/PBO成功/PBO失败三组的资源传输描述和实际目标地址，随后修正真正分支；不在产品中简单把全部上传变成PBO。

## 暂存：上传路径跟踪与新的工作范围

用户改为保留 Mesa EGL/GLES，仅替换系统 Vulkan 后端。闭源 EGL/GLES 研究保留，不再作为当前系统替换的前置条件。

`SQ2_USAGE_TRACE=1` 是仅测试进程内的有界跟踪，复用固定版本的 CPU transfer hook；必须与上传模式一起使用，诊断退出码为 3，不算通过。直接 64×64 上传的图像传输描述中，一个 CPU 地址为 NULL；13×13 PBO 上传先完成 buffer copy，随后图像传输同样出现 NULL 地址。64×64 PBO 对照仅捕获 buffer copy，没有捕获该图像 CPU copy。结合原 fd 同步读回为零，下一步应调查图像目标映射/描述构造；尚未证明具体初始化根因，也未修复。原始跟踪留在忽略的 out，默认 Mesa 未改变。

## 2026-11-07：6.18 上两个独立上传机制修复，真实 HWUI 三帧通过

本节是最新观察；上文保留 6.3.8/combined-v5 的历史结果，不把旧四通道布局假设延伸到新内核。本轮在原 Mesa 桌面中只为测试进程建立私有挂载 namespace、装入原厂 Qualcomm GLES/Vulkan 与兼容库，使用新私有图形缓存。没有以 ANGLE 或软件 Bitmap 回退替代原厂 GLES，也没有替换系统 mapper、修改 GPU 寄存器/内核或重启桌面。

### 外部线性硬件 Bitmap：真实 CPU 映射与同步

固定 source AHB 的实际 usage 为 `0x100`，donor 在 `glTexSubImage2D` 路径向 mapper 请求 CPU usage `0x33`。真实 minigbm mapper5 因分配时无 CPU usage 返回 `BAD_VALUE`，原 donor 后续取得空地址；原 64×64 上传为 16351/16384 错误字节。不能通过给源分配强加 CPU usage、吞掉失败或返回虚构指针解决。

新 `probe/graphics/quest2-cpu-access.cpp` 只通过该私有 mapper3 DSO 的单一 dlopen 路径接入 `libsq2map.so`：正常 mapper 成功路径原样转发；仅在实际 BAD_VALUE 且 metadata、GPU usage、线性单平面 RGB、尺寸/offset/fd 边界和非 protected 条件成立时，等待真实 acquire fence，mmap 实际 DMA-BUF 并执行 START/END 同步。导入、映射和 fd 生命周期保持配对；END 失败保留隔离资源，不谎称成功。未支持的格式/布局仍拒绝，不改变原 allocation metadata。26 个实际函数 host 故障注入场景通过；它们不代替真机 coherency 验证。

相同 `0x100` 输入真机变为 0 错误字节，START/END 成功、free=0、remaining=0。进一步 165 次上传覆盖 13×7 至 2880×1920 均通过，正常 GPU 绘制、GLES 三帧及 Vulkan compute/image/raster 回归通过。真实 HWUI 中硬件 Bitmap 三帧也变为 bad=0。

### 普通内部纹理：6.18 的八通道宏平铺与旧 CPU4 补丁错配

只修上述映射后，HWUI 普通 64×64 Bitmap 每帧仍有 2048/4096 错误像素，缩放/alpha 相应错误、文字约 65% 匹配；纯色/渐变/背景正常。原 14 库同机对照有完全相同的普通 Bitmap 错误，只额外失去硬件 Bitmap，说明不是新 CPU-access shim 引入该回归。

当前 6.18 实际源码 `drivers/soc/qcom/ubwc_config.c` 的 `sc8180x_data.macrotile_mode=true`。`a6xx_calc_ubwc_config()` 复制此值、将 A680 highest_bank_bit 修正为 15，`a6xx_set_ubwc_config()` 将 macrotile_mode=1 写入 RBBM_NC_MODE_CNTL：即八通道、bank15，而旧 GLES CPU converter 仍被历史补丁固定成四通道。三帧 PNG 的全部 12288 个普通 Bitmap 像素均严格符合历史 4↔8 通道地址置换：当 `((x>>6)^(y>>4))&1` 时取 `(x^16,y^8)`，否则不变；按此预测三帧均 0 误差。这里是输出 PNG 比对，不冒充 raw tiled-memory 或 live register 采样。

独立 `runtime-618-layout8-15` 只将 GLES VA `0x2a6100` 的历史 `mov w6,#4` 恢复为原厂 `ubfx w6,w8,#10,#4`，重新使用能力字通道字段；保留 bank15、PBO、零面积、fence prune、私有编译器和已验证 CPU-access shim，其余 14 库及配置逐字节不变。没有退回原厂 bank16、固定伪造 capability 或改变内核。GLES SHA256 为 `e48bf0099077b91f5856eeabe7f751233eea0021e8c3f8fb1c19ca643ccc3987`。

相同真实 HardwareRenderer/RenderNode/ImageReader harness 的三帧最终结果均 PASS：普通/硬件/缩放/alpha/纯色/渐变/背景区域 bad=0；文字分别为 2132/2132、2097/2097、2126/2126 全匹配。实际 maps 确认 libhwui、原厂闭源 GLES，未装载 freedreno；boot ID、SurfaceFlinger/system_server PID 保持。证据为忽略目录 `out/qualcomm-graphics-618-20261107/hwui-qcom-layout8.json`。

### 可复现与边界

从已冻结的精确 15 库 CPU-access parent 离线生成全新目录：

```sh
python3 scripts/prepare-quest2-618-layout.py \
  --runtime out/qualcomm-graphics-618-20261107/cpu-access-candidate/runtime \
  --output out/qualcomm-graphics-618-20261107/runtime-618-layout8-15-FRESH
```

脚本核对完整输入清单与 ELF/指令、只允许四字节差异、核对完整输出 SHA，生成相邻 JSON byte patch 并回读所有文件；拒绝覆盖已有产物。原 14 库和原 15 库基线均保留。`layout8-15-transaction` 中的可执行 rollback 在独立副本实际恢复原 hash；抽取的真实 ARM64 通道/bank 指令在同一 capability 输入下 B/M/R 输出为 `mode=4 bank=15` → `mode=8 bank=15` → `mode=4 bank=15`，均 exit0。该指令测试本身不等于完整 GPU 验证；上面的 HWUI PASS 才是本次对应真机观察。

本节验收仅覆盖这些原生矩阵和真实 HWUI 场景，**尚不等于全桌面冷启动、所有应用、所有纹理格式、SQ1/SQ2 跨设备或长时稳定性验收**。新 boot 的 donor capability 字段尚未单独直接采样；八通道依据为源码、实际像素置换与单指令修正后结果。正常 Mesa baseline、原驱动恢复与回退仍保留；按 2026-11-07 AGENTS.md 与 DEVELOPMENT.md PORT-07 最新规则，先推进启动测试，双向切换及普通启动都不设缓存清理/回执门槛。仅在启动后遇相关问题时，按需停止 Chrome 并直接删除其已确认图形缓存，不备份缓存；不处理其他应用，不清空普通 cache 或浏览器数据。
