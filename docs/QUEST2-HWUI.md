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
