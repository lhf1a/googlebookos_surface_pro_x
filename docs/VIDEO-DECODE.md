# SQ2 视频硬解适配调查

2026-10-29，用户明确请求补齐硬解。状态：未实现，未更改实机内核、DTB、启动镜像或 codec 配置。

## 已核实

- 运行内核为 6.3.8-sq2-android1；配置快照关闭 CONFIG_MEDIA_SUPPORT，SM8150 VIDEOCC 也未启用。实机无 /dev/video*、/dev/media*，无 Venus 驱动或模块，运行设备树未找到 video/venus 节点。
- 匹配源码位于 WSL /root/.cache/sq2-thermal/kernel；旧 BUILD 中 probe-build/kernel 路径不存在。该源码 Venus core.c 没有 SC8180X/SM8150 match；videocc-sm8150.c 只有 SM8150 compatible。
- Android media.player 实际枚举 AVC/HEVC/AV1 为 c2.android 软件解码器。vendor XML 的 OMX.qcom 条目不是硬解已经接通的证据。
- 本机存在 qcom/venus-5.2 固件和多个 Android donor Venus/VPU 固件，但尚未确认任何一份适用于 Surface SC8180X 的认证、内存布局与 HFI。文件存在不等于可直接启动。
- Android vendor 含 QTI C2 服务、libqcodec2_v4l2codec.so 和 libv4l2r.dylib.so；尚未验证它们与主线 Venus 的 ioctl、缓冲格式及 Codec2 ABI 兼容。
- Bilibili HD 2.0.1 的一次画质列表日志确认有效 VIP、选择 qn=112、显示 1080P；4Ksubhead_state=2 的语义未查明。进程映射 libijkffmpeg.so，当时系统资源管理器未记录其 codec 实例。不能仅据此断言其隐藏 4K 的确切条件。用户报告同一 BV1NGZtBwELa 在 Chrome 可播放 4K；Chrome 的编码/解码路径尚未实测。

## 实现前的具体缺口

### 当前主线：855参考 + Venus/V4L2 + 开放Codec2

用户选择此路线，Quest闭源材料保留为辅助参考，不再优先接入其服务。已从 LineageOS/android_kernel_oneplus_sm8150 固定提交 `32f26bf34ddf17df8e224ae0703656b9fe0c1a8d` 取得 `sm8150-vidc.dtsi` 和 `venus_hfi.c`，文件哈希见 `configs/sm8150-video-reference.json`。这是OnePlus设备的社区维护下游内核，不能称为Surface官方参考。

855 DTS提供：寄存器0xaa00000/0x200000、GIC SPI174、mvsc/mvs0/mvs1电源、三路GCC AXI及三路VIDEOCC时钟、非安全SMMU 0x1300/mask0x60、IOVA池0x25800000+0xba800000。这些仅为SM8150数值，尚未验证可用于SC8180X，不写入实机DT。

新增 `build-sq2-video-foundation.sh`，从现有配置在独立 `/root/.cache/sq2-video-foundation/build` 启用媒体/V4L2/Venus及SM8150 VIDEOCC，构建通用模块，不生成或安装Image/DTB。首轮编译完成；配置审计发现媒体自动选择额外启用I2C_MUX/USB音频媒体控制关联，随后关闭媒体子驱动自动选择和MEDIA_CONTROLLER并重编。构建缺原Module.symvers，允许modpost未解析符号警告，因此只证明编译，不能装载或视为ABI验证。产物、配置差异和日志留out/video-decode/foundation。

二轮编译完成：I2C_MUX额外变化已消除，但Kconfig仍自动启用MEDIA_CONTROLLER及SND_USB_AUDIO_USE_MEDIA_CONTROLLER，显式disable未能消除；不要把候选配置当作可直接替换运行内核的配置。下一步需核对其选择关系及既有USB音频模块ABI。

尚缺：SC8180X资源交叉核实、Iris1对当前Venus HFI/电源实现的补丁、匹配固件、实机V4L2帧验证及Codec2接入。构建基础模块不代表这些适配已完成。已有默认镜像与eDP工作均未改动。

### Quest 2 闭源路线：2026-10-29 离线核验

用户指定参考此前 Vulkan 的开发过程，以 Quest 2 XR2 Android 闭源视频栈为用户态 donor，SM8150 下游实现仍作为 Iris 1.x 硬件参考。

当前工作区缺少先前完整 vendor 材料，已重新下载固定 OTA prefix/vendor 两段，使用原重建器核验131项操作及 vendor SHA256 44f90742…，重新只读提取906个文件。没有验证完整OTA签名，也没有刷写任何分区。

新增 `scripts/inspect-quest2-video-reference.py`，从固定分区身份及提取清单逐文件复核长度/SHA256，再用 readelf/strings 做离线分析。实跑核验54个视频相关文件，原始报告保留在 `out/quest2-gpu/video-reference-audit.json`；固定文件哈希见 `configs/quest2-v77-video-reference.json`。不执行 donor、不接入系统服务。

- 32/64位 `libOmxVdec.so`、`libqcodec2_v4l2codec.so` 均存在；QTI Codec2服务和 OMX服务本体为32位。64位解码库存在不代表有可直接使用的64位服务。
- C2库包含 `/dev/video32`、`/dev/video33`、私有 `V4L2_CID_MPEG_VIDC_VIDEO_BASELAYER_ID` 及 NV12/TP10/RGBA UBWC字符串。OMX库另有 `/dev/video/q6_dec`，依赖 `libion.so`、`libqdMetaData.so`、`libc2dcolorconvert.so`、`libplatformconfig.so`。
- donor XML列出 c2.qti AVC/HEVC/VP8/VP9 decoder，非secure AVC/HEVC最大4096×2304。此为Quest声明，不能作为SQ2实测能力或复制进当前系统。
- 字符串证明候选含这些接口路径，不证明每个路径都在本机运行时必然触发。须通过私有探针补足调用证据。

后续分层推进：先独立进程验证64位原库依赖/初始化，记录实际open/ioctl与内存分配契约；同时以SM8150参考建立SQ2硬件后端。只在有真实解码结果后才接Codec2服务。Vulkan mapper桥接尚未覆盖视频的全部UBWC/10bit/ION语义，不能原样复用或返回虚假ioctl成功。尚未生成可部署硬解候选，默认系统不变。

需要可信 SC8180X/Iris 1.x 硬件参考来确定寄存器区域、IRQ、SMMU stream ID、电源域、时钟/reset、互联带宽、固件内存保留与 HFI 版本。不能把 SM8250/SC8280XP 的设备树或资源表改名后直接启动。

1. 获取并固定本机或同 SoC 的下游源码/固件参考，核实上述资源；研究 SC8180X VIDEOCC 上游改动的可回移性。
2. 独立目录构建媒体模块和必要内核增量，保留现有 screen_only、NVMe、FUSE、音频、触摸和温控。仅构建通用 Venus 模块不足以完成 SoC 适配。
3. 独立候选 DTB/启动入口验证固件启动和 V4L2 枚举，再用已知 H.264/HEVC 样本做真实解码及像素/帧数检查，逐步升至 4K。
4. 对照 QTI C2 与 AOSP v4l2_codec2 接口，接入可用的真实硬解组件；验证普通应用的 MediaCodec、缓冲导入、seek/释放和软解回退。
5. 最后复验 Bilibili 画质与实际 codec。硬解接通本身不保证服务端开放 4K。

参考：[AOSP v4l2_codec2](https://android.googlesource.com/platform/external/v4l2_codec2/)、[上游 SC8180X DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sc8180x.dtsi)、[上游 Venus](https://github.com/torvalds/linux/blob/master/drivers/media/platform/qcom/venus/core.c)。后两者是可变参考，不是已固定/测试的补丁输入。

原始日志含用户与播放签名信息，不纳入 Git。其它工作产生的 DISPLAY-REFRESH 和 eDP 未跟踪文件保持不动。
