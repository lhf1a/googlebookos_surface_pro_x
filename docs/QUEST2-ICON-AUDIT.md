# 图标黑块：离线覆盖审计

2026-10-28，仅离线审计，无设备或 GPU 操作；不是修复结论。

## 已知画面与现有覆盖

combined-v5 的真实桌面截图存在图标黑方块、顶部横条；壁纸和部分文字可见。SF 转储中的 StatusBar、Taskbar、Launcher 为 format1 / AB24 / modifier0，usage=0x10000900；Sprite cursor 则是36×36 BGRA / usage0x8933。BGRA import 是先前真实启动阻断，但不能用已修好的 cursor 格式解释当前所有图标。

`quest2-readpixels.c:relay_size` 的 CPU 上传路径是 mutable GL_RGBA8 `glTexImage2D` + 完整 `glTexSubImage2D(GL_RGBA,GL_UNSIGNED_BYTE)`，客户端 row-length=0、alignment=1，然后 nearest、整数 texelFetch 到线性 AHB。alpha确有变化（64..255），所以不能说此前测试只覆盖不透明字节。然而上传测试没有打开混合、没有 sRGB internal format、immutable storage、mipmap、texture swizzle、alpha/R8 mask、external RGB image target、GPU hardware bitmap copy，且尚未等同 HWUI 的真实内部缓存。

`quest2-color.cpp` 的 AHB 九组合准入通过真实分量与 stride 校验，source/output 都申请 CPU read/write+GPU sampled/color output。`quest2-fp16.cpp` 同样显式申请 CPU flags。`quest2-composition.cpp` 则验证 nearest RGBA、premultiplied混合和跨context fence；尚不覆盖 HWUI atlas/ShaderSkia 的具体调用组合。

## 不同 usage 分支的实证

缓存源码 `minigbm-android/cros_gralloc/cros_gralloc_helpers.h` 明确把 bit28 (`GRALLOC_USAGE_PRIVATE_0`) 定为 FRONT_RENDERING_PRIVATE；helpers.cc 把它转为 BO_USE_FRONT_RENDERING。当前桥接 `quest2-qti-mapper.cpp` 的 getUsageFlags 原样返回 b.usage，因此这个厂商私有位也送给了 QTI donor。

固定的原配 Quest2 v77 `libgrallocutils.so` 中：

- `gralloc::IsUBwcEnabled(int,unsigned long)` 导出 veneer 0x121f0 跳到 0xa7f0。
- 0xa82c..0xa83c 构造并测试 mask **0x0002000010001000**，含 bit28。RGBA1 在后续格式 mask 中。
- 0xa8b0..0xa8cc 根据 GPU usage 和 GPU UBWC支持决定策略；0xa8cc / 0xa8dc 的低字节 CPU usage 非零检查会拒绝 UBWC。
- `eglSubDriverAndroid.so` 0xef38..0xef44 还有直接返回0x10000000的操作分支；不能假定该位只是 Android 框架额外加的。

因此 CPU flags 的测试缓冲区与真实0x10000900确实经过不同原配分配策略。**这不是当前 GLES 实际压缩错误的证明**：系统没有使用原配 gralloc allocator，桥接又明确返回 modifier0 和 privateFlags=8。仍需验证 donor 对 usage、privateFlags、真实布局三者的优先级，不能直接全局清位或伪造元数据。

原始 disassembly 留在忽略目录 out/quest2-gpu/firmware-reference/{grallocutils-icon-audit,egl-subdriver-icon-audit,gles-icon-audit}.disasm。

## 最小后续对照

1. 先用真正 HardwareRenderer/RenderNode 在私有命名空间画同一软件 bitmap、hardware bitmap、vector 和文字，对照 Mesa；输出真实 ImageReader 像素，不依赖当前桌面截图判断 producer。
2. usage 单因素对照：GPU 绘制 source AHB（不通过 CPU 初始化 source），source 使用0x900与0x10000900；GPU采样到另一个 CPU-readable AHB 才读回。不要为了方便给 source 加 CPU flags，那会改变要研究的策略。先实际分配与 metadata 检查，不能假定 allocator 接受任意 usage。按36²/72²及2880×54执行。
3. 若 software bitmap 与 hardware bitmap 分歧，拆 immutable storage、CPU subupload、GPU copy、AHB import，再测 RGB external texture。若均错而 vector正确，先查 upload/format/shader路径，而非重做已过的纯色FBO。
4. 原生GL的下一组应是 RGBA8 与 SRGB8_ALPHA8、mutable与immutable、nearest与linear、base-only与generateMipmap；以已知输入和独立 CPU oracle 区分颜色空间转换与地址错误。R8/ALPHA8 mask 和 swizzle 单独测试，不能沿用 RGBA 每像素四字节假设。

以上是减少歧义的实验顺序，未执行项不算支持能力。没有建议关闭校验、忽略返回值、把软件回退当作系统替换完成。
