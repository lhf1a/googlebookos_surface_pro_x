# 摄像头源码构建

当前前后摄支持不需要从Surface提取启动包，也不需要Windows摄像头驱动参与构建。
Windows/ACPI参考用于确认硬件资源和供电；运行代码是Linux驱动、
仓库中的SC8180X/传感器补丁、软件ISP和Android Camera provider。

这不表示整个Android/QTI平台已经成为纯开源构建：现有Hybrid缓冲桥接仍需匹配平台的
头文件、生成接口及链接库。这些是共用SDK/供体平台依赖，不是Surface单机身份或校准。
无需设备在线、旧camera二进制、initramfs、Windows驱动原件或拍摄文件。

## 准备输入

复制configs/camera-build.example.json到本地配置，修改以下路径。
所有相对路径按JSON所在目录解析，不依赖当前工作目录。

| 输入 | 内容 |
| --- | --- |
| kernel_source | Linux 6.18.60源码，含原始CAMSS、OV5693/OV13858、CCI和regulator源码 |
| kernel_build | 标准内核构建输出，含.config、Module.symvers、生成头文件；kernel.release必须为6.18.60-sq2-android1 |
| toolchains | 既有项目工具链布局：sq2-graphics/android-ndk-r28c、Mesa 26.2.4 android_stub、sq2-c2-core-reference/libcutils、sq2-c2-native-reference/libs/binder/ndk、sq2-c2-aidl-generated/include |
| interfaces | AOSP hardware/interfaces冻结AIDL源码，具体版本由build-spx-camera-aidl.py指定 |
| aidl_compiler | Android SDK android-15/aidl |
| dependencies | jpeg目录内的jconfig.h/jpeglib.h/jmorecfg.h；system-media内的camera_metadata.h/camera_metadata_tags.h/camera_vendor_tags.h；native-headers/libs/nativewindow/include |
| libraries | 匹配Android平台的libcamera_metadata.so、libnativewindow.so、libjpeg.so、libcutils.so |
| platform_headers | 平台mapper5生成头文件，包括Stable-C IMapper与metadata接口 |
| binder_library | 平台libbinder_ndk.so |
| layout_headers / hidl_headers | 既有图形构建的布局生成头文件及HIDL头文件 |
| allocator_headers | android.hardware.graphics.allocator的生成NDK头文件 |
| graphics_library_dirs | libgrallocutils、libqdMetaData、libcutils、平台libc++、allocator-V2/common-V2 NDK接口库和libbinder_ndk所在目录，按搜索优先级排列 |
| cross_compile | ARM64内核工具链前缀，默认aarch64-linux-gnu- |

AIDL版本：common V2、FMQ V1、graphics.common V5、camera.common V1，
camera.metadata/device/provider V3。每份冻结源目录必须含其.hash。
平台接口/库须与目标Android一致；这些输入不能用任意系统同名库替代。
如内核尚未编译，先使用项目标准compile/full-image源码内核流程准备匹配build树。
新入口会在输出创建前拒绝错误kernel.release，不修改现有内核配置或伪造版本号。

## 编译与接入

    python3 scripts/build-spx-camera-bundle.py --inputs private/camera-build.json --output out/camera-from-source

入口依次执行：

1. 在输出目录的源副本上应用摄像头补丁，编译sensor/CCI/CAMCC/SPMI和CAMSS模块。
2. 从冻结AIDL源码生成并编译Binder接口。
3. 编译当前仓库的Camera HAL及软件ISP。
4. 从当前probe/graphics源副本应用0004/0005/0006，编译mapper/allocator。
   链接命令由输入路径生成，不读取历史BUILD.json或旧编译目录的源码。
5. 输出camera/CAMERA.json和13个运行ELF，验证ARM64、模块ABI和完整性。
   诊断工具可在build目录生成，但不进入camera运行包。

把该目录交给已接入的默认构建：

    python3 scripts/build.py full-image --graphics hybrid --private-root /path/to/platform-inputs --camera-inputs out/camera-from-source/camera --output out/full-camera-image --esp-only

本工作区标准入口仅提供Hybrid；总流程见 [STANDARD-BUILD](STANDARD-BUILD.md)。source内核模式仍会针对本轮内核重编
camera模块，用户空间使用刚生成的包。boot/stage helper和profiles直接读取当前源码。
无需先运行full_image_camera.py --import-initramfs；该旧入口仅保留作已验收包的迁移工具。

构建日志位于build/*.log，命令记录为build/commands.json；产物和本地依赖不提交。
2026-11-16已用新入口完整重编并通过full-image安装器校验，未部署新产物。
真机v10格式/录像证据见FRONT-CAMERA.md；构建成功不扩展为新二进制或SQ1的真机验收。
