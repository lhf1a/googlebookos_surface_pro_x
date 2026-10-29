# SQ2 视频硬解适配调查

2026-10-29，用户明确请求补齐硬解。状态：未实现；已临时加载媒体框架、VIDEOCC及其OF overlay，未更改启动镜像或 codec 配置。

最新目标：在Android上实现硬件解码器可用，完成标准为真实硬件解码帧验证、Android MediaCodec可枚举和调用、应用实际播放；只有内核模块加载不算完成。

## 固件搬运与读回实测（2026-10-29）

继续自远端 `codex/sq2-video-decode` 的 `30aee11`。当前工作站缺少上一轮thermal/video缓存，已从SHA256为275f05c5…的既有内核归档恢复独立 `/root/.cache/sq2-thermal/kernel`，重建foundation；没有构建或替换正常启动Image。

新增 `sq2_video_firmware_stage.c`、共享ELF校验头和 `build-sq2-video-firmware-stage.sh`。只接受Surface compatible、显式allow=1和现有1159200字节/4e93d345…固件；任何MDT调用前核对完整SHA256及ELF32 ARM、小端、紧接ELF头的程序头表、重定位标志、源范围、非重叠加载区和6MiB上限。此限制对应当前6.3 MDT解析器的实际假设，非通用ELF加载器。构建固定MDT源码哈希5dd1d028…，拒绝直接SCM/auth依赖，调用 `qcom_mdt_load_no_init`，不调用PAS初始化。

模块SHA256 `03c0cd37aa61a29c0a008be8de1c149655846e851933810e42f18940c833a57b`；23个未定义符号全部匹配当次运行内核导出，vermagic为6.3.8-sq2-android1 SMP mod_unload aarch64。符号闭包不代替完整ABI验证，原Module.symvers缺失限制保持。`audit-sq2-video-modules.py --module ... --output ...` 可单独审计探针而不覆盖foundation报告。

实机传输哈希匹配后，insmod allow=1返回0。新分配6MiB标准DMA内存，物理地址0xfd200000（本次值，已释放，不能复用）；三个LOAD段、5MiB跨度的数据逐段比对和BSS全零检查通过，MDT返回的重定位基址也匹配。辅助设备无OF/IOMMU/DMA转换，DMA地址等于物理地址，1MiB对齐且完整区间低于4GiB。返回前擦除并释放分配、注销辅助设备；随后rmmod返回0，两项sysfs均不存在。不带allow参数的独立负向加载返回EPERM，未创建设备或保留模块。

前后boot ID均2f0c17ee…，system_server/SF PID均2220/2046，boot_completed=1、screen_only=Y，VIDEOCC保持绑定。未写ESP、启动镜像、设备树或Android codec配置，没有重启。完整预检和日志留在忽略的 `out/video-decode/firmware-stage/`。

主机C测试覆盖合法重定位/BSS/hash段排除、截断头表、错误架构/字节序、非连续程序头、超长段表、源溢出、filesz>memsz、段重叠、32位地址溢出、超大跨度、无可加载段和非重定位段，全部通过。边界校验测试与实机copy/readback是不同证据。

**没有调用PAS初始化、认证或启动，也没有HFI或解码帧。** 本次只补齐真实固件搬运/读回证据；不能直接给现有Venus加devm分配后启动。下一步仍需完成Iris1电源/HFI/资源适配及固件关闭失败时的分配保留、恢复和remove策略。已释放的连续内存地址不是可永久占用的reserved-memory。

## 通用媒体框架首次实机加载

### 固件连续内存实测

`probe/video/sq2_video_memory_probe.c` / `scripts/build-sq2-video-memory-probe.sh` 新增独立分配探针。匹配6.3.8构建，13个未定义符号均匹配当次运行内核__ksymtab；模块SHA256 `d581fe6a5d089d434cb90e6d68cab69bfe5bd118c609445881ee8d04d9b9b2fd`，实机传输哈希一致。显式allow=1及Surface compatible保护，创建无OF/IOMMU/地址转换的临时platform设备，以32位coherent DMA mask调用标准dma_alloc_coherent；不调用SCM、不访问视频寄存器。

实机insmod成功：6MiB分配返回物理/DMA地址均为0xfd000000，1MiB对齐且完整区间低于4GiB；整区0xa5及0两种填值读回一致。函数返回前已dma_free_coherent并注销辅助设备，随后rmmod成功，核对module/sysfs辅助设备均不存在。boot ID仍2f0c17ee…、boot_completed=1、screen_only=Y。原始结果在out/video-decode/memory-probe-result.txt。

这证明本次运行可通过标准DMA接口获取独占连续内存，不证明PAS9允许在此处运行固件；不能硬编码或再次使用已经释放的0xfd000000。后续实际加载必须持有新分配直到远端处理器关闭确认成功，并处理认证失败、恢复和驱动移除。现有Venus core.c在错误清理、remove及恢复路径忽略venus_shutdown返回值，不能直接改为devm分配后自动释放，否则固件关闭失败时存在释放仍被硬件使用内存的风险。固件加载生命周期适配仍待完成；本轮没有启动固件。

固件/时钟进一步核对：临时挂载debugfs只读采样后已卸载，bi_tcxo=19.2MHz、video_pll0=399999902Hz，mvsc/mvs0/mvs1及iris_ahb均注册但enable计数0；这不是频率设置/硬解负载通过。VIDEOCC保持绑定，boot ID不变。

固件判断纠正：不能凭venus-5.2目录名把本机文件认定为SDM845固件。已通过SSH取回实际文件，SHA256 `4e93d34504da96320775476bbd509aa06f985b48fb781d4436c8eb812b929928`，1159200字节，内部版本为 `VIDEO.IR.1.2-00042-PROD-1`，确为Iris1版本字符串。三个LOAD段均可重定位，跨度5MiB，链接基址0x0f500000不是物理加载地址要求。[固定Surface提取脚本](https://github.com/linux-surface/aarch64-firmware/blob/9f07579ee64aba56419cfd0fbbca9f26741edc90/scripts/getfw.py)明确从qcdx8180包提取qcvss8180.mbn并复制到venus-5.2/venus.mbn；这支持现有路径的来源解释，但本地原Windows采集清单缺失，不能独立证明该二进制的OEM来源。应优先验证这份现有固件，而非先替换OnePlus固件；尚未执行PAS认证/启动。

社区OnePlus7TPro固件仓库 `sm8150-linux-mainline/firmware-oneplus-hotdog` 提交 `5e5c534784a848bc39a02c421cc5273d79678fb3` 的venus.mbn：SHA256 `9d4d4b4d26745a89b37847ebcc3a1f96a00d77e5a791bc3cb71a90e5c992bc4b`，1142816字节，内部版本VIDEO.IR.1.2-00035-PROD-1。inspect-sq2-video-firmware.py解析结果同为三个可重定位LOAD段、5MiB跨度。仅离线对照，未验证OEM签名、SQ2认证或兼容性，未发送至设备。

本轮运行态复核boot ID仍为2f0c17ee…、screen_only=Y、VIDEOCC仍绑定。内核启用DMA_CMA，CMA共32MiB、采样空闲约26MiB；但dma_alloc_from_contiguous/cma_alloc等不是运行内核导出，不能直接用独立模块调用。后续可核对无IOMMU辅助设备的标准DMA分配或独立启动预留；CMA空闲量本身不证明满足固件物理地址/认证要求。未占用任何现有保留区。

特别注意：Surface原DTS明确把0x8b700000的5MiB区标为CAMERA，不是视频固件区；不能因大小相符就占用。当前DT没有明确的Venus reserved-memory。下一步需要建立可证明独占的5MiB加载区（重启预留或受控分配并核对SCM约束），并核实SQ2原固件来源，不能用现有未知reserved块冒充空闲内存。此为待完成适配项，不是已判定无法实现。

VIDEOCC首次实机接入（2026-10-29）：内核确认OF_DYNAMIC/OF_OVERLAY及导出可用，改为临时overlay避免改启动盘。`build-sq2-video-clock-overlay.py`从已审计时钟DTB生成嵌入式overlay模块，要求allow=1、本机compatible为microsoft,surface-pro-x、目标节点不存在及三个外部phandle路径匹配。模块76e1340e…，时钟驱动fd6adde7…，传输后完整哈希一致；overlay全部未定义符号匹配运行内核导出。

实机手动加载overlay返回ret=0/id=1，随后insmod videocc-sm8150成功，`/sys/bus/platform/devices/ab00000.clock-controller/driver`实际指向video_cc-sm8150。boot ID未变、boot_completed=1；未写ESP、未重启、未加载视频固件。ueventd最初因临时模块不在其索引而自动加载失败，之后手动加载绑定成功，不应误判为probe失败。模块/overlay仍在运行；不要在时钟驱动仍绑定时卸载overlay，最简单恢复路径为正常重启。此项只证明VIDEOCC probe接入，尚无视频解码帧或Android硬解组件。

VIDEOCC候选：新增 `build-sq2-video-clock-dtb.py`，固定当前a5a25e43… CPU/GPU DTB，仅添加 `/soc@0/clock-controller@ab00000`。参考上游SC8180X节点（Linux提交062dc4693e2c10d77de06f61e6f3faf37c0a8383的sc8180x.dtsi），采用SC8180X+SM8150双compatible、0xab00000/0x10000、RPMH CXO、MMCX域和现有LOW_SVS OPP。当前6.3 GCC在probe直接开启0xb004 VIDEO_AHB和0xb040 VIDEO_XO，尚无新版GCC_VIDEO_AHB_CLK导出；旧VIDEOCC只消费bi_tcxo，故候选不捏造iface时钟ID。需要实机验证该旧驱动组合，不能由上游节点存在推断通过。

已生成out/video-decode/sq2-video-clock.dtb，SHA256 59966cdf5aacfddc2f9023d42285106bf8e339c67218ca9673458e5f1cdbcf41。全树对照验证原节点所有属性及保留内存不变，仅增加上述节点；未部署、未启动，不含解码器资源或固件启动。后续先验证VIDEOCC probe/父时钟/电源域，再接Iris1硬件节点。

2026-10-29：新增 `audit-sq2-video-modules.py`，读取当次运行内核__ksymtab导出，与九个候选模块的未定义符号及候选间依赖做闭包检查，missing=0；这不是完整ABI证明。六个通用模块的vermagic均为6.3.8-sq2-android1 SMP mod_unload aarch64，传输后SHA256与本地一致。

按顺序手动insmod：videodev、videobuf2-common、videobuf2-memops、videobuf2-v4l2、videobuf2-dma-contig、v4l2-mem2mem，六个均返回成功并在/proc/modules为Live。dmesg确认Linux video capture interface v2.00。boot ID前后一致，boot_completed=1，开始前screen_only=Y。模块仅来自retained root的/tmp/sq2-video-framework，未持久化；正常重启不会加载它们。

本轮尚未加载venus-core/decoder/VIDEOCC，未增加设备树节点或启动固件，没有/dev/video解码实例；Android仍没有硬解组件。该实测只关闭了通用V4L2框架可加载性的缺口。原始符号及报告位于out/video-decode，不纳入Git。

## 已核实

- 运行内核为 6.3.8-sq2-android1；配置快照关闭 CONFIG_MEDIA_SUPPORT，SM8150 VIDEOCC 也未启用。实机无 /dev/video*、/dev/media*，无 Venus 驱动或模块，运行设备树未找到 video/venus 节点。
- 匹配源码位于 WSL /root/.cache/sq2-thermal/kernel；旧 BUILD 中 probe-build/kernel 路径不存在。该源码 Venus core.c 没有 SC8180X/SM8150 match；videocc-sm8150.c 只有 SM8150 compatible。
- Android media.player 实际枚举 AVC/HEVC/AV1 为 c2.android 软件解码器。vendor XML 的 OMX.qcom 条目不是硬解已经接通的证据。
- 本机存在 qcom/venus-5.2 固件和多个 Android donor Venus/VPU 固件，但尚未确认任何一份适用于 Surface SC8180X 的认证、内存布局与 HFI。文件存在不等于可直接启动。
- Android vendor 含 QTI C2 服务、libqcodec2_v4l2codec.so 和 libv4l2r.dylib.so；尚未验证它们与主线 Venus 的 ioctl、缓冲格式及 Codec2 ABI 兼容。
- Bilibili HD 2.0.1 的一次画质列表日志确认有效 VIP、选择 qn=112、显示 1080P；4Ksubhead_state=2 的语义未查明。进程映射 libijkffmpeg.so，当时系统资源管理器未记录其 codec 实例。不能仅据此断言其隐藏 4K 的确切条件。用户报告同一 BV1NGZtBwELa 在 Chrome 可播放 4K；Chrome 的编码/解码路径尚未实测。

## 实现前的具体缺口

### 当前主线：855参考 + Venus/V4L2 + 开放Codec2

后续推进（2026-10-29）：实机只读确认screen_only=Y、现有venus-5.2/venus.mbn SHA256为4e93d34504da96320775476bbd509aa06f985b48fb781d4436c8eb812b929928，尚未证实该固件适合Iris1。补取同一OnePlus提交的msm_vidc_platform.c、venus_hfi.h、hfi_packetization.c等实际存在文件并更新哈希。sm8150_data明确选择VPU_VERSION_5；venus_hfi初始化消息使用HFI_PACKETIZATION_4XX，同时上电/中断函数选择vpu5_ops。这是区分硬件版本与消息版本的直接证据，不能把VPU5数字简单映射为HFI版本，也不能只添加compatible后套现有v4电源流程。

配置修复：MEDIA_SUPPORT_FILTER使底层配置菜单不可见，原disable被默认值覆盖。现在关闭该过滤、在首次olddefconfig展开后再次禁用MEDIA_CONTROLLER/DVB/USB和PCI媒体驱动/调谐器，强制检查MEDIA_CONTROLLER与USB音频耦合未开启。构建前清理独立media目录产物以免旧模块残留混入清单。此更改仍为compile-only；缺Module.symvers的限制保持。此次没有加载模块、触发固件或修改DTB；不能称为硬件启动尝试通过。

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
