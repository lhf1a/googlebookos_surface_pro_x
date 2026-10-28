# Quest 2 GLES 接入系统合成的离线审计

2026-10-28。以提交 `769f2f54dc2d6e3f5e5d3dcaf9ad3529eb0d8f32` 的适配器和私有候选 `gles-linked-zero-area` 为基础；GLES SHA256 为 `c47968472a01e3d2a69dad87d3e000fe7914a0f4e0198c6c70e389ba43219476`。仅检查本地代码、匹配 donor、缓存系统 ELF 和既有设备转储，没有操作设备或修改启动。

**结论：还不能直接把当前适配器部署为系统 SurfaceFlinger 驱动。最小下一步是独立、有限时长的 RGBA 多层合成与 native-fence 探针，复现当前 SF 的实际消费路径。** 522项读回检查解决了特定 CPU 布局/PBO/API问题，不是这一合成路径的替代证明。RGBX/BGRA/FP16和视频颜色空间存在明确支持缺口，但缓存桌面全为RGBA，不能凭常见格式名认定它们已经阻止当前桌面启动。

## 1. 当前 SF 实际使用什么

既有 `out/quest2-gpu/visible/surfaceflinger.txt` 是可见窗口实测时取得的真实转储，反映当时Mesa桌面，并非本次重新采集：

- 第643行起为 `RE GLES (Ganesh)`；显示输出2880×1920、native window/default format均为1（RGBA8888），三个client-target slot格式也为1（463–479行）。
- 当时六个显示图层均为RGBA8888、非protected（planner部分）。wideColorGamut/HDR能力为false，RenderEngine protected context支持为0（459、648行）。因此FP16/HDR/protected尚不是这个缓存场景中的必要输出能力。
- 第13行明确列出同步使用 `EGL_ANDROID_native_fence_sync` 和 `EGL_KHR_wait_sync`；不能因旧探针 `glFinish` 后读回通过而认为异步合成同步已经验证。

缓存目标 `/root/.cache/sq2-probe-build/googlebook/system/system/bin/surfaceflinger` 的SHA256为 `76d41857c4c3c175fef5836d7631117529f7c401b723c39d5520702882eae3c7`；它动态依赖系统 `libEGL.so`、`libGLESv1_CM.so`、`libGLESv2.so`、`libnativewindow.so`、`libui.so`、`libgui.so`。系统libEGL SHA256为 `713e00f7c031a07fc7587515a40f8c2e871b5af076f5d721a65dea449e060fbb`。

其实际动态导入包括 `eglGetNativeClientBufferANDROID`、`eglCreateImageKHR`、`eglDestroyImageKHR`、`eglCreatePbufferSurface`、`eglCreateSyncKHR`、`eglWaitSyncKHR`、`eglDestroySyncKHR`、`eglDupNativeFenceFDANDROID`。还有容易被普通GLES2探针遗漏的两个绑定：

```text
glEGLImageTargetTexture2DOES@LIBGLESV1_CM
glBlendFunc@LIBGLESV1_CM
```

没有直接导入 `eglCreateWindowSurface` 或 `eglSwapBuffers`。这个事实不能排除所有getProcAddress间接调用，但结合Skia GLES转储，下一步应覆盖AHB/EGLImage作为输入纹理与输出FBO、native fence和混合；继续重复“闭源应用生成窗口，由Mesa SF显示”的测试不能完成SF替换验证。RenderEngine未作为独立共享库出现在此SF的DT_NEEDED中；不应假设缓存里存在可直接dlopen的匹配 `librenderengine.so`。

详细依赖、上述导入和源文件哈希保存在忽略的 `out/quest2-gpu/system-graphics-audit.json`。

## 2. 格式边界：代码已经明确的拒绝项

[quest2-qti-mapper.cpp](../probe/graphics/quest2-qti-mapper.cpp) 的 `supported_buffer`（89行起）与 `info`（106行起）决定native代理是否能建立映射。单平面仅接受format1、`DRM_FORMAT_ABGR8888`、32位sample、非protected；另有经过独立PlaneLayouts验证的YV12/NV12分支。`sq2_native_wrap`失败时，[EGL包装](../probe/graphics/quest2-egl-wrapper.c) 返回 `EGL_BAD_MATCH`，不应把这些格式拒绝误判为donor GPU渲染失败。

| 输入 | 当前桥接行为 | 对当前系统路线的含义 |
| --- | --- | --- |
| RGBA8888 / ABGR8888 | 单层、modifier0、真实fd/offset/stride/size；已覆盖多尺寸AHB、窗口、上传与读回 | 已具备下一轮当前桌面RGBA合成探针的基础；仍缺多层混合/显式fence路径证明。 |
| RGBX8888 / XBGR8888 | 明确拒绝format2 | 通用应用/不透明图层缺口。补齐时必须保留X语义、确认采样alpha为1，不能把format改成RGBA后宣称支持。 |
| BGRA8888 / ARGB8888 | 明确拒绝format5 | 通用图层/截图等潜在输入缺口；需按真实fourcc、组件顺序与donor格式逐项映射并用非对称RGBA图案查红蓝交换。缓存六层尚未出现此格式。 |
| RGBA_FP16 / ABGR16161616F | 明确拒绝format22与64位sample | HDR/宽色域或FP16共享buffer缺口；当前输出无wide color，不应先把所有内部Skia F16需求等同于AHB FP16导入需求。内部GL纹理不经过该mapper，需分别验证。 |
| YV12 / YVU420三平面 | 支持同一fd、modifier0、Y偏移0、真实分量/步长，早销毁EGLImage后仍可采样 | 原有视频色块只证明BT.601有限范围；不是所有视频颜色空间通过。 |
| flexible420格式35且真实NV12布局 | 两平面验证后返回QTI具体格式0x109；实际Cb/Cr地址步长保留 | 不包含NV21、P010、IMPLEMENTATION_DEFINED请求格式或多fd平面；未知布局继续失败。 |

底层不是只能分配RGBA：缓存minigbm `cros_gralloc_helpers.cc:31–75` 分别映射RGBA/RGBX/BGRA/FP16，`msm.c:39–42` 将对应fourcc列为render-target格式。这是代码能力证据，不是本次分配实测。以后扩展桥接时，应同时修正两处格式准入，不能只改 `getFormat`。`quest2-mapper5.h:65–70` 目前跳过RGB PlaneLayout组件细节，仅取sample/stride/size；增加BGRA/FP16前必须验证实际组件位宽和位置，避免用总字节数代替颜色布局。

[quest2-yuv-metadata.h](../probe/graphics/quest2-yuv-metadata.h) 已严格验证分量、子采样、平面边界与不重叠，明确要求Y和buffer offset为0。不要为了接入SF删除这些约束或假设所有plane共用fd0；新增布局须增加真实fd/plane关系的表示与检查。

## 3. 视频颜色空间是实际未适配的接口

`Extensions::getColorSpace`（`quest2-qti-mapper.cpp:163`）总是返回UNSUPPORTED；`getSurfaceMetadata`/`getSurfaceMetadata_V1`同样未实现。现有 `BufferInfo` 从mapper5取format/fourcc/modifier/usage/layout，但不取DATASPACE。反之，缓存minigbm `mapper_stablec/Mapper.cpp:399` 已提供真实StandardMetadata DATASPACE，不能以“系统没有该元数据”为由硬编码一种空间。

匹配Quest 2 v77 mapper实现的 `getColorSpace` VA `0xd790`，在 `0xd7e4` 调用 `gralloc::GetColorSpaceFromMetadata`；原配eglSubDriverAndroid也含 `PerformGetColorspace` 和“从gralloc colorspace映射至EGL colorspace”的路径信息。原实现不是恒定返回0。公开HIDL文件只是接口契约，缓存 `templates/QtiMapperExtensions.cpp` 是生成模板，不能当作原厂实现源码。

最小补齐应是读取真实DATASPACE，核对匹配donor的颜色空间枚举与range映射，再独立对照BT.601 limited、BT.709 limited和full-range输入；不应仅把UNSUPPORTED改成成功/0。先在现有YUV探针加具有不同颜色预期的这些输入，以区分布局正确与矩阵/range正确。现有测试设置的是默认元数据、BT.601 limited图案；实际最大误差1不外推至BT.709。此项可在RGBA合成探针通过后推进，不必先部署SF来诊断。

## 4. 同步与生命周期：已实现和仍未验证的精确区别

[native-buffer代理](../probe/graphics/quest2-native-buffer-lifetime.h) 复制已测168字节ABI，用donor实际inc/dec持有原GraphicBuffer，归零时释放映射；[window代理](../probe/graphics/quest2-native-window-lifetime.h) 复制192字节ABI，dequeue lease与donor引用分开计数，queue/cancel把原buffer及原fence交回真实window。window已过循环/轮换/销毁验证，但它是生产者路径。

mapper3 `lock`（263行起）复制HIDL借用的acquire fence，再把副本交给会消费fd的mapper5；锁定期间保留独立import。`unlock`（336行起）返回借用的release-fence handle，回调后关闭本地fd。缓存mapper5 `Mapper.cpp:175–239` 确认无论成功失败都会取得acquire fd所有权；这里不能删掉dup，也不能把回调里的release fd当作永久有效整数。

这些mapper CPU锁同步不等于SF的GPU server wait。现有 `gles-window.c:116–126` 由消费者poll producer fence，证明queue生成的生产fence可用；`quest2-readpixels.c:377/423` 的relay则在CPU读回前 `glFinish`。没有现成探针直接覆盖SF导入的 `eglWaitSyncKHR`/`eglDupNativeFenceFDANDROID` 双向传递和重用输入buffer前的GPU完成时序。包装层对这些函数原样转发，没有适配实现；原配libEGL虽导出对应符号，也还不是“本Surface所有fence行为已通过”的证据。

映射存储仍有128项/512MiB上限（`quest2-qti-mapper.cpp:46/78`），按真实fd/inode/size与native引用回收。一个2880×1920 RGBA可见区域约21.1MiB，24个这种分配已接近上限，实际对齐分配还可能更大。现有有限循环的零残留不能代表SF长期图层缓存永不达到上限。不要盲目去掉保护：下一轮保留上限并采集峰值/退出归零；对超限应可靠失败，而不是意外释放仍在用的映射。此适配器还逐getter/inc/dec输出调试日志，真正系统集成时需改为显式诊断开关，不能将这种高频stderr输出留为正常默认。

## 5. 扩展与protected的边界

包装层不重写 `eglQueryString` 的扩展列表，`eglGetProcAddress`除了图像/窗口/错误入口外都转发donor。匹配donor文件存在native-fence、wait-sync、image-native-buffer等导出/字符串，但本次没有新的设备扩展查询；不能把文件中的字符串当作当前display实际宣告。先在私有system-loader探针取得真实display扩展和目标函数，再运行有真实sync-file fd的测试；不增加虚假的扩展宣告。

protected usage位14在RGBA和YUV入口均被明确拒绝，`getMapSecureBufferFlag`只对已通过非protected准入的buffer返回false；没有把安全buffer偷偷映射为普通内存。当前Mesa SF转储也不支持protected context，因此它不是现阶段桌面回归前提。若donor实际宣告protected扩展，必须如实限制测试/系统能力，不因扩展字符串存在就宣称DRM视频可用。protected支持需要独立分配/导入/同步路径，不能复用这里的可读写mmap。

## 6. 只做一个最小下一步

新增独立 `quest2-composition` 探针，沿用普通Android libEGL和候选私有mount namespace，不启动第二个SF、不替换现有SF或init服务。限定为当前实际使用的RGBA8888/非protected/linear AHB，先复现如下路径：

1. 生产context将不同坐标图案写入两个输入AHB，其中一层为已知premultiplied alpha；只flush并导出native fence，不用 `glFinish` 代替同步。
2. 合成context经 `eglGetNativeClientBufferANDROID` → `eglCreateImageKHR` 导入，使用实际SF的系统 `libGLESv1_CM` 导出入口绑定image/设置blend，再以native-fence sync导入并server-wait生产fence。把opaque底层、透明上层、裁剪/平移依次合成到第三个真实RGBA AHB；输出路径不走EGL window。
3. 导出合成release fence，把独立持有的fd交给mapper5/AHB CPU lock或独立消费者后再按实际stride检查RGBA。明确fd所有权并验证销毁sync不使已dup的fd失效；用逐像素oracle检查alpha与遮挡，颜色混合舍入与buffer边界分开判定，不降低现有精确读回oracle标准。
4. 先64×64，再640×360和当前2880×1920；有限轮次复用/轮换buffer，覆盖早销毁EGLImage但纹理仍存活、两个image共享buffer、GPU完成后回收，最终引用/映射与fd数量回到基线。正常Mesa库/boot ID/框架PID必须保持。

这一步不需要先实现RGBX/BGRA/FP16，也不能证明Skia完整RenderEngine正确，但可直接补上当前SF明确使用而旧探针缺失的接口与异步混合证据。通过后再用同一合成探针按真实格式元数据逐一扩展RGBX/BGRA，以及视频DATASPACE；若缓存/新采集显示FP16参与实际共享buffer，再增加其路径。最后才考虑带独立恢复措施的一次性SF选择器和真实桌面合成验证，届时仍保留现有minigbm、drm_hwcomposer与正常Mesa入口。Quest 2的下游SDM/HWC不因本GLES成功就成为Surface显示控制器的替代品。

本文件是审计与下一步设计，尚未实现或运行上述新探针。
