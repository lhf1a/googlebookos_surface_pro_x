# 唯一标准编译基线：Linux 6.18 LTS

更新：2026-11-07。当前版本 **6.18.60-sq2-android1**。6.3.8专用编译链已退休；历史记录不是可执行入口。

## 保持当前验收内容

以 `configs/kernel-baseline.json` 的文件清单为准，不重新混装旧模块。当前原件：
- Image：`db374853bb5df0cdaa5215b1226256adedda3292a93084e874438a4ba706c982`
- initramfs：`5b3a70c349735e58b404b0d3825dc0dde12ae991706cbbab7e15ed65c9911185`
- DTB：`c137b9604b908e39aba4be6ecd98e55cd9986fe3fd7522c93069bef7489c956b`

有效启动参数为 **Mesa、原生60Hz、EL2、unrestricted suspend、nohibernate**；GPU为当前700/RPMh432档。
保留音频、SSAM电池、SPI触摸response、Venus队列锁、IPA预留与RMNET、Android拥有网络及启动后SSH等当前修正。
不因为旧文档默认高通/661而改变当前验收内容。单机用户确认与六帧视频证明不等于跨设备或长期认证。
Scene跨用户链接属于已授权设备数据状态，不复制到其他设备；Scene GPU读数缺失仍是已知问题。

## 一个入口，明确区分核验、组包与编译

在具备Python3的主机运行；源码编译需要Linux/WSL与匹配ARM64工具链：

```sh
python3 scripts/build.py verify
python3 scripts/build.py package --output out/6.18-release-FRESH
python3 scripts/build.py compile --help
```

默认输入为 `private/build-inputs/kernel-6.18/EFI/SQ2L618C`。`package`逐字节复制当前EFI包并核对清单，
**不是源码编译，也不是生成新的完整磁盘镜像**。旧磁盘重组入口已撤销，避免悄悄装回6.3.8。
`build-surface-pro-x.py`、`build-source-boot.py`、`build-source-disk.py`、`build-sq2-default.py`兼容文件名均转到同一CLI，旧参数明确拒绝。

`compile`使用显式source/config/build-dir/output/cross-compile；实际执行Image、modules、dtbs与模块导出。
默认配置为 `configs/kernel-6.18-baseline.config`。完整基线还需要匹配的外挂源目录与模块清单，具体参数以`compile --help`为准。
新编译输出与验收发布包分离，未经配套打包/验收不得覆盖发布pins。不能以仅核心编译通过代替全部外挂模块与initramfs功能对齐。

## 本地输入与恢复

- `private/build-inputs/kernel-6.18/`：当前私有EFI包、源码归档与构建清单；不纳入Git。
- `out/mainline-618-20261107/`：改动前Git bundle、未提交diff、未跟踪文件归档和本轮输出。
- `out/kernel-baseline-20261107/retired-638/`：退休工具逐字节备份。
- `configs/retired-kernel-tools.json`：退休清单与原hash。仅退休旧工具，不删除真机回退入口。
- 既有四角色与验证记录继续保存在 `out/kernel-6.18-migration-20261105/`；新记录只追加。

## 隐私与验收边界

当前私有包保留原SSH身份，不能当成匿名公共镜像发布。Git仅保存源码、配置、清单、测试与文档。
不把IP、序列号、用户编号或原盘UUID作为编译/运行准入。SQ1/SQ2通用交付目标保持，但本轮基线是当前SQ2已观察产物，不能假称6.18双机新镜像全验收。
本轮主线整理不写设备、不重启，不清除userdata或已有恢复能力。
通用开发与身份分离规则见 [DEVELOPMENT.md](DEVELOPMENT.md)。

## 已执行的完整源码编译形式

解开私有源码归档得到`kernel/`和`external/`后：

```sh
python3 scripts/build.py compile --source SOURCE_ROOT/kernel \
  --config configs/kernel-6.18-baseline.config --build-dir FRESH_BUILD \
  --output FRESH_EXPORT --cross-compile TOOLCHAIN/bin/aarch64-linux-gnu- \
  --external-sources SOURCE_ROOT/external --jobs 8
```

显式`--resume-build`只复用config与ABI匹配的构建目录。缺少外挂源时默认拒绝；`--core-only`必须明确指定且产物标记不完整。
本次完整入口28步exit0、15个外挂模块覆盖成功；发布包另经verify保持原hash。源码导出不等于新boot包已验收。


## 2026-11-07：独立 Qualcomm 全图形调试包（只在 6.18 上适配）

用户明确要求在已经适配的 6.18 上替换高通图形，不允许在 6.3.8 上演进。
本分支复用当前 6.18 Image、DTB、模块和已验收配套；旧版本仅作为历史用户态移植资料，
不恢复退休的 6.3.8 编译链，不改现存设备回退。当前默认 Mesa 发布 pins 保持不变。

私有重建链为 restore-sq2-qualcomm-graphics-runtime.py → prepare-quest2-cpu-access.py →
prepare-quest2-618-layout.py → prepare-quest2-618-direct.py → pack-sq2-qualcomm-full.py → prepare-sq2-618-debug-entry.py。
各入口参数以源码 --help 为准；输入是固定来源的私有 donor 与当前 6.18 父包，不含单机准入。
最终专用 packer 只接受通过隔离大窗口验证的 15 库 layout8/bank15/Direct runtime manifest，拒绝旧 14 库
和旧强制 4 通道中间包；这个 pin 是私有二进制来源/配对完整性，不是硬件身份白名单。

6.18 上修正两点：mapper3 私有 CPU 访问桥在真实 mmap/DMA_BUF 同步后上传 GPU-only AHB；
GLES 的 CPU 纹理通道从旧强制 4 恢复为实际能力字段，保留 bank15。未修改当前内核、DT、GPU档位。
真实隔离命名空间测试包括 165 次上传、多尺寸、内纹理上传到外 AHB 的独立读回、HWUI 三帧及
GLES/Vulkan 探针；通过不代表完整桌面冷启动/游戏/跨设备验收。

新包只加入 opt-in qualcomm-full-start 和配套 runtime，原父包所有非 init 文件逐字节保持，
原凭据 addon 仍最后生效。EFI 构建只新增 SQ2QDbg 独立目录与第三项，原两项内容/顺序/default 保持。
本地产物带实验室原 SSH 身份，不能匿名公开分发。

当前缓存策略以 2026-11-07 AGENTS.md / DEVELOPMENT.md PORT-07 最新规则为准：**先推进启动测试，仅在启动后遇相关问题时按需清理 Chrome 的已确认图形缓存，直接删除，不备份缓存**。
只动态发现 Chrome 用户/资料路径；保留账号、Cookie、历史、密码、凭据、下载、设置、普通网页缓存及其他应用全部数据。
Mesa↔Qualcomm 双向切换及普通启动均不自动或强制清理，不设预启动清理/回执门槛；按需处理前停止 Chrome，完成后重启 Chrome。原驱动、6.18 Mesa 默认与启动回退仍保留。
历史4752d501…调试包要求旧全应用回执，文档修改不会自动移除旧包中的门槛。旧全应用控制器已停用；
当前试启入口应取消全应用及 Chrome-only 预启动清理/回执要求；不得用伪造完成回执绕过旧门槛，不沿用全应用扫描。
本次规范修改未执行真机缓存删除、未部署新图形包、未重启。

本轮 private 候选/字面验证/缓存来源证据保存在 out/qualcomm-graphics-618-20261107，
既有四角色继续位于 out/kernel-6.18-migration-20261105；新增原生 payload/menu/cache 角色是补充而非替代。

## 2026-11-07：大窗口 HWUI 花屏的 Direct 兼容候选

固定输入 `runtime-618-layout8-15` 后执行：

```sh
python3 scripts/prepare-quest2-618-direct.py \
  --runtime out/qualcomm-graphics-618-20261107/runtime-618-layout8-15 \
  --output out/qualcomm-graphics-618-20261107/runtime-618-layout8-direct-15
python3 scripts/pack-sq2-qualcomm-full.py \
  --runtime out/qualcomm-graphics-618-20261107/runtime-618-layout8-direct-15 \
  --output out/qualcomm-graphics-618-20261107/package-layout8-direct
```

输出路径必须全新。准备器校验完整父 manifest、15 库、ARM64 ELF 唯一 RX 指令范围和新旧完整 hash；
只将 GLES VA 0x167734 的两条初始化指令改为原驱动已有的显式 Direct 模式。
context+136 的 bit3 置位，context+15784 为0；API能力字段、8通道/bank15、CPU上传、PBO与fence修正保持。
这绕过出错的多分块路径，不是 GMEM 根因修复；性能、功耗、SQ1及完整应用覆盖未验收。

新 GLES SHA256 `58e1aafe2de28dfb632df9c64b56b761906bf8aeae014975f71938f1061a4d83`；
新 runtime SHA256SUMS 的 SHA256 `395520969a5f1ce766dabde0af258204a17d9dc5c5ea37783f1937590b5c0689`。
packer 只接受新精确 manifest，旧花屏候选保留作回退输入。生成物仍为私有实验包。
本次 initramfs `db5e611943f9c4835ab13255fcb75bf5e65f83a2aec5c1562d90a107a29426c7`。
相对设备旧 f7422b5d 包，有效差异限 GLES、其 SHA256SUMS 和去缓存回执门槛的启动 helper。
原凭据最后生效，原内核/DT/模块和所有其他有效文件保持；不清缓存。
`package-layout8-direct/{MANIFEST.json,VERIFICATION.txt,ROLLBACK.sh,APPLY.sh}` 包含离线原件恢复与精确重应用证明。
注意 ROLLBACK.sh 恢复的是组包父5b3a70，不可拿它覆盖现机默认525d739f；设备debug更新另保存原f742及配套校验单。

可见探针支持 `SQ2_VISIBLE_PRODUCER=gles|hwui`（默认gles）与 `SQ2_VISIBLE_WIDTH/HEIGHT`（默认640×360）。
边界按实际屏幕和 GLES viewport 能力检查；私有挂载命名空间与5秒临时窗口要求保持。
2073×1382 HWUI 对照、回退、重应用及逐像素证据见该out目录的 `resume-hwui-direct-*`、`direct-deployment-review`。
应用设置/EasyTier最终结果以 HANDOFF 的实际新启动记录为准，不由离屏探针推定。

本候选已按用户授权设为设备默认并完成正常重启，boot73d4ee5a…；设置两个页面及EasyTier截图正常。
全局GLES/Vulkan基础与2073×1382 ImageReader WSI通过，实际证据见 `direct-postboot/ACCEPTANCE.json`。
设备默认改变不修改原验收父包pins；原6.18 Mesa与旧回退入口继续保留。

## 2026-11-07：将 OMX 前置 Venus 修正合入高通 Direct 包

当前分支ce03b44已带OMX客户端、VIDC桥、PM/min-output/safe-close源码；无需覆盖原工作树或重新移植旧分支。
`pack-sq2-qualcomm-full.py`新增接受完整hash固定的candidate3父包525d739f；原发布父5b3a70仍可用，其他输入继续拒绝。

```sh
python3 scripts/pack-sq2-qualcomm-full.py \
  --base out/quest2-video-618-20261107/pm-candidate3/initramfs.cpio.gz \
  --runtime out/qualcomm-graphics-618-20261107/runtime-618-layout8-direct-15 \
  --output out/quest2-video-618-20261107/resume-on-qcom/package
```

相对已启动的高通Direct包，仅两处core、两处decoder和视频校验清单有效变化；图形、init、凭据、其他模块原样。
具体合并hash/恢复证据见 `resume-on-qcom/INTEGRATION.json` 和package内的MANIFEST/VERIFICATION。
原厂OMX仍为独立进程验证；合入前置模块不等于已替换系统C2或已完成原厂真实解码。

新增客户端参数 --venus-nv12 显式选择 QCOM Venus 线性格式0x7fa30c04；和 --native-color 互斥。
该模式暂保留raw，不因名字跳过实际stride/slice/crop验证。bridge启用后仅把明确的cached system ION
(mask bit25/flags1/align0)转交真实DMA heap；flags0 uncached及安全内存继续拒绝。
最终组合包已启动，GetHandle、端口配置及四个输入分配通过；当前卡在uncached extradata，尚无原厂帧。
详细实际状态见 docs/QUEST2-VIDEO-PORT.md 顶部及 resume-on-qcom/PROGRESS.json。


## 2026-11-07：uncached 与原厂 OMX AVC 已通过的重建方式

当前阶段与验证边界见 HANDOFF 顶部；旧“uncached阻断”为历史状态。用户态五件：

```sh
python3 scripts/build-quest2-video-probe.py \
  --cc /mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
  --headers out/quest2-video-618-20261107/agent-probe/include \
  --output FRESH_OUTPUT --with-decode-client --with-vidc-bridge
```

`--with-vidc-bridge`同时构建quest2-ion-probe；运行参数是桥的绝对路径，环境QUEST2_VIDC_BRIDGE=1。
分配探针本身不提交视频/GPU命令。OMX解码还需QUEST2_VIDC_AUX_CAPS=1、原厂7库路径和
`--venus-nv12`，必须给全新输出目录。原厂库保持原字节，不安装全局preload。

内核配套在精确candidate3源副本上应用 `python3 scripts/patch-sq2-venus-omx.py SOURCE_COPY`，
使用私有6.18.60归档的config/Module.symvers和prepared build目录执行：

```sh
make -C PREPARED_618_BUILD M=ABS_SOURCE_COPY ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j8 modules
python3 scripts/pack-sq2-lts-venus-queue-lock.py BASE_INITRAMFS ABS_SOURCE_COPY SYSTEM_MAP FRESH_INITRAMFS \
  --expected-base-sha256 EXACT_BASE_SHA --preserve-lab-ssh
```

三模块配套构建，保留凭据tail并验证其他有效文件。不要覆盖原发布kernel-baseline pins。
本轮可复现输入/输出在uncached-native/{source,kernel-build,venus-candidate3,venus-omx-flush,package-flush}。
当前原厂AVC仅验证非安全线性NV12及裁剪元数据翻译；不是完整原厂msm_vidc/ION或原始extradata驱动移植。

## 2026-11-07：64位 HIDL OMX 构建与 XML 接线

使用已准备的Android平台头/真实系统链接库及未修改的AOSP16源归档：

```sh
python3 scripts/build-quest2-hidl.py \
  --source-archive SOURCE/hwservicemanager-source.tar.gz \
  --headers PREPARED_HEADERS --platform PLATFORM_LIB64 \
  --cache /mnt/spx-workspace/toolchains --omx-headers OEM_OMX_HEADERS \
  --output FRESH_OUTPUT
python3 scripts/prepare-quest2-hidl-xml.py \
  --variant ACTUAL_MEDIA_CODECS_VARIANT.xml \
  --framework ORIGINAL_FRAMEWORK_MANIFEST.xml --output FRESH_XML_OUTPUT
CC=cc QUEST2_SANITIZE=1 python3 -m unittest discover -s tests -p test_quest2_omx_decode.py -v
python3 -m unittest discover -s tests -p test_quest2_hidl_xml.py -v
```

当前准备输入在out/quest2-video-618-20261107/system-omx/{headers,platform,hwservicemanager-source.tar.gz}，
OEM OMX头在同级上层agent-probe/include。源URL及归档hash见configs/quest2-hidl-build.json。
headers包含hidl-gen生成头（旧生成器的hidl_enum_values需inline constexpr）及AOSP16依赖头；
此入口复用准备输入，不自动从零下载全部平台依赖。BUILD.json记录实际编译命令及三个产物hash。
manager补丁仅在实际becomeContextManager后发布enabled，不手动伪造系统属性就绪。

XML必须按设备ro.media.xml_variant.codecs选择（当前_hamoa），默认OMX parser与OmxStore都需
看到configs/quest2-media-codecs-omx.xml的两个AllocateBuffer quirks。include文件放同一vendor/etc。
原生同名OMX条目冲突时拒绝静默覆盖；只删除冲突AVC alias，保留其他codec与音频includes。
框架manifest仅解除真实移植的manager/token层级上限。HIDL通过前不广告其他OEM编解码器。

历史阶段仅本地构建/临时注册；2026-11-07续接已完成HIDL buffer/Surface像素验证及init集成，见本文末尾及HANDOFF最新记录。
重启恢复与后续验证状态见HANDOFF顶部，不沿用旧AIDL像素通过作为HIDL验收。

## 2026-11-07：重启后 SSH 等待统一为10分钟

设备端probe/test2/ssh-framework-boot按/proc/uptime等待sys.boot_completed，最长600秒，
完成后启动原start-ssh；不改变主机密钥、认证或网络路由。旧镜像等待约240轮，
新的600秒助手需打入启动包才会在设备生效。
主机端scripts/wait-sq2-ssh.py --ssh-json SSH_ARGV.json --previous-boot BOOT_UUID --log NEW_LOG.jsonl
默认600秒整体窗口、单次最多15秒，暂时拒绝/无路由持续重试；只有认证成功且boot_id变化才返回成功。
不重新发起reboot，不绕过StrictHostKeyChecking。两侧共6项时序/错误分支测试通过。

## 2026-11-07：ESP空间与有界启动日志

每次部署前、启动后核对ESP df与logs占用；及时移除已确认可丢弃的历史开发日志。
清理不包含EFI载荷、固件、LTE校准、本机缓存、凭据或应用数据。日志不能成为启动成功的前置条件。
启动诊断增量入口scripts/pack-sq2-618-boot-diagnostics.py只修改有效init/boot-kmsg/ssh-framework-boot，
从实际6.18父包追加，保留最后凭据成员，不拼入旧源init的其他变动。
boot-kmsg以aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror编译，
主机tests/boot_kmsg_test.c验证8MiB边界、32MiB空间底线、stat失败、真实/dev/full ENOSPC。
此为诊断容量保护，不代替每轮检查与清理历史目录；当前设备部署见HANDOFF顶部。

## 2026-11-07：HIDL持久服务、CODECCONFIG与Surface布局

新增 `scripts/pack-quest2-hidl.py BASE HIDL_DIR OUTPUT --expected-base-sha256 SHA`。
输入目录必须有完整SHA256SUMS；当前私有输入位于system-omx/boot-inputs/hidl-hardware-level6。
父包为同目录package-codec-config/initramfs.cpio.gz（cee28cdd…），包含配套CODECCONFIG模块及有界启动日志。
packer安装quest2-hidl-stage、init rc、HIDL服务/mapper、默认hamoa XML，并移除旧项目C2 runtime，
ccodec=1；不修改底层只读系统分区，保留凭据tail。正常init以system/hwservicemanager与mediacodec身份启动。
当前使用既有permissive开发策略，不能称生产SELinux移植完成。

CODECCONFIG配套能力由scripts/patch-sq2-venus-omx.py生成；已有flush源码可加--codec-config-only。
模块声明sq2_omx_config_flag=16777216，桥验证后将OEM0x20000转换为native0x01000000，再由Venus转换为HFI0x80。
配置buffer不占普通帧timestamp槽。重编三模块并按原配套pack入口追加，不能只更新一边。

HIDL只对线性NV12输出描述实际分配宽/高（stride/slice），独立crop保留可见尺寸；
AOSP SoftwareRenderer的NV12路径使用width跨行，直接报告可见width会忽略实际stride而花屏。
主机ABI夹具覆盖容量/几何拒绝；真机ImageReader六帧转为紧凑NV12后逐字节等于软件参考。
下游QUERYCTRL offset56(flags)是profile支持位图，桥从真实QUERYMENU生成；不能直接透传原生控制flags。
下游FRAME_RATE 0x992077为Q16 fps，转换为VIDIOC_S_PARM的65536/fpsQ16，不吞原驱动错误。

普通APK脚本仍scripts/build-sq2-video-app.sh，SQ2_GRAPHICS_WORK=/mnt/spx-workspace/toolchains/sq2-graphics。
默认及intent codec=auto、expected-codec=OMX.qcom.video.decoder.avc验证真实自动选码；仅显式codec参数可选其他组件。
当前300帧/EOS/实时/关闭通过，但全部rendered回调未齐，严格RESULT仍false；不能放宽断言来宣布通过。
旧OEM profile列表在ConstrainedHigh处提前结束；当前改为读取真实HFI profile/level pairs，实机验证见最新HANDOFF。

## 2026-11-07：移除旧实验C2，能力来自实际HFI表

当前HIDL packer不再接收C2目录；从父newc记录删除etc/sq2-c2-runtime-boot、etc/sq2-c2-experimental
及旧manifest-stage，其他原始记录头/载荷保留。init不再安装自定义C2服务/XML/VINTF。
系统c2.android.*软件音视频codec、原厂系统组件及libc2dcolorconvert不按前缀删除。
现有sq2.video.c2=experimental启动参数仅保留为旧入口的功能开关兼容，不再注册C2实现。

配套模块：在已包含OMX/CODECCONFIG的源码副本上运行scripts/patch-sq2-venus-avc-caps.py。
它修正HFI AVC5.2误映射5.1，补齐AVC6/6.1/6.2（HFI/OMX bits17..19）映射，按真实core caps建立SQ2 AVC V4L2菜单，并导出只读sq2_avc_caps。
导出受PM/core锁和错误状态保护，不执行固件操作；不改非SQ2控制菜单。
重建全部三件模块并用原Venus配套pack入口追加，新的模块包作为HIDL packer父输入。

HIDL服务启动前先加载实际Venus能力；--print-capabilities-xml生成两个parser实际读取的XML。
启动顺序为native backend ready→能力读取/XML生成→启动sq2-omx→刷新media枚举，无C2依赖。
profile/level按每个HFI pair返回，不靠原厂枚举表，也不拿测试样本作能力上限。
普通AVC与当前可用线性NV12范围单独声明；MVC/secure/HDR/UBWC不能由硬件表存在就假称适配已完成。

AVC能力回归使用tests/fixtures/quest2_video/avc-hfi-caps.txt的匿名固件记录；tests/test_quest2_omx_caps.py覆盖真实Level6及6.1/6.2、未知编码拒绝。
尺寸上限与block-count/blocks-per-second同时约束，不能把各字段独立最大值相乘承诺吞吐；concurrent-instances来自驱动core session上限，不等于最大规格同时运行数量。

最新HIDL-only v2实机getEnforce为Enforcing且服务身份正确；此前permissive为历史观测。完整domain例外/生产策略审计未完成。当前重启/能力/像素/APK结果以HANDOFF最新节为准。

## 2026-11-08：全codec能力与P010候选

venus-sq2-avc-export.h保留sq2_avc_caps ABI，并新增hevc/vp8/vp9/mpeg2/mpeg1同类只读导出；
缺完整固件表返回ENODATA。仍按原三模块匹配构建/打包，当前新包与证据见HANDOFF。
Quest2CodecCaps解析各自HFI profile/level/output2格式，测试fixtures来自实际固件。
CC=cc python3 -m unittest discover -s tests -p test_quest2_omx_caps.py -v
CC=cc python3 -m unittest discover -s tests -p test_quest2_vidc_bridge.py -v

诊断桥Android构建链接-llog，有界Q2BRIDGE日志保留原errno；poll仅记录readiness变化。
QP10到原生P010映射通过主机测试；不等于HIDL 10-bit/HDR已完成。
当前all-codecs二进制与RAM测试产物独立于已部署AVC服务；禁止误将候选声明为已验收发布。

测试向量：python3 scripts/build-sq2-hevc-test-vector.py --out FRESH --bit-depth 10
--dynamic-range hdr10（或hlg）。这是合成像素/metadata输入，不代表目标面板规格。
生成器自行核对ffprobe色彩、HDR10 mastering/content light及P010打包/低位精度。
Native测试仍用sq2-v4l2-decode-test.c配套生成的sq2-hevc-vector.h；先核对内核健康。
HDR10/HLG新向量已完成原生六帧P010独立CRC/EOS/关闭验证；HIDL元数据和显示仍未验收。

MediaCodec容器探针复用build-quest2-video-probe.py：
--media-file-binder ACTUAL_PLATFORM/libbinder_ndk.so；构建清单记录真实链接库hash。
运行sq2-mediacodec-file FILE CODEC FRESH_OUTPUT_DIR，保留原extractor CSD/profile/color metadata，
抓取首六帧，检查PTS/缓冲边界/EOS/帧数/关闭；外层需独立超时及持久化stdout，避免SSH失联丢证据。
探针超时明确标failed，不把configure/start等同有帧输出。

P010 HIDL候选将标准color54映射QTI0x7fa30c0a，以实际byte stride/2报告分配像素宽度，
保持crop与可见尺寸独立；已做ABI/容量/不齐行距拒绝测试。10-bit profile启用仍待实际解码、
高精度Surface路径和metadata接通，不能把未部署候选当系统已支持。

## 2026-11-08：可选色彩字段与迟到HFI事件

bridge的SOURCE_CHANGE必须带尺寸/位深/progressive(mask7)。colour(mask8)是可选字段，
缺失按高通下游event_notify零初始化语义传unspecified0；不由尺寸或NV12猜色域。
VP8/VP9缓冲逐像素验证及HEVC full-AU P010验证见最新HANDOFF，独立CSD与HDR显示仍未通过。

scripts/patch-sq2-venus-omx.py SOURCE --late-events-only 可增量应用已具OMX/CODECCONFIG的配套源。
完整apply亦包含此保护：丢弃无session的迟到session事件，仅SYS_ERROR允许sessionless；
无效session日志限速。所有锚点核验后才写hfi_msgs.c，未知输入拒绝。
python3 -m unittest discover -s tests -p 'test_venus_sq2*.py' -v
执行实际patch后的dispatcher并使用ASAN/UBSAN；不是实机冻结根因验收。
当前候选all-codecs/venus-late-events三模块已编译，vermagic匹配6.18.60-sq2-android1，
与前模块集无新增未定义符号。System.map只含内建导出，media/MDT独立provider仍需配套运行校验。
该候选已成套打包/部署，正常重启及AVC buffer/Surface CSD回归通过，详见HANDOFF最新节；冻结根因仍未验收修复。

## 2026-11-08：输入缓冲诊断与迟到事件详情候选

quest2-omx-decode --input-buffers N仅控制诊断客户端实际分配数，不伪造nBufferCountMin。
尺寸/count必须同一次OMX PortDefinition设置：原厂设置尺寸时会REQBUFS，随后再次S_FMT会EBUSY。
拒绝低于min、厂商拒绝或回读count不一致；未指定时保持原行为，共享session调用无需新参数。
CC=cc python3 -m unittest discover -s tests -p test_quest2_omx_decode.py -v

--late-events-only现在同时接受原始及已部署旧guard源码，增加限速事件ID/两个data字段诊断。
未知锚点仍不写文件；固定字段长度不足不读/打印data。全量apply同样包含该诊断。
all-codecs/venus-late-event-details为已编译未部署候选；默认仍package-late-events。
MPEG2 actual2/min1仍卡住的证据及恢复结果见HANDOFF，不能把新增选项当产品修复。

## 2026-11-08：原生固件HDR元数据平面

在完整已配套OMX/CODECCONFIG/逐codec能力/迟到事件保护的Venus源码上运行：
python3 scripts/patch-sq2-venus-extradata.py MATCHED_SOURCE
重新构建core/dec/enc三件，不能混用venus_inst布局；使用pack-sq2-lts-venus-queue-lock.py，
核验完整模块依赖、有效cpio和实际加载build-id。默认mask0不改变现有单平面服务。
控制0x00982f00为私有实验ABI，mask1=VUI、2=mastering、4=content light；只允许INIT前设置。
CAPTURE第二平面是原始HFI records，大小通过固件需求表取得；INIT前尚无需求时sizeimage可为0，
先建立OUTPUT队列再查询CAPTURE格式，不把0替换为猜测大小。容量/物理地址/END退休保护均适用。

native探针编译时选择既有HEVC向量，--extradata EXISTING_EMPTY_DIR /dev/videoN显式启用，
不安装服务/启动固件；逐像素检查沿用独立参考。每个record dump可用：
python3 scripts/inspect-sq2-hfi-extradata.py RAW_FILE
原始静态HDR值及VUI必须与输入独立ffprobe信息核对；metadata通过不等于HDR渲染通过。

v4已部署并完成HDR10/HLG原生metadata实测（见HANDOFF）。启用extradata时内部DPB同样需要
页对齐的metadata尾部，FTB的pixel alloc_len与metadata地址/大小分别设置；整块保留原退休生命周期。
buffer requirements解析先校验header和最多HFI_BUFFER_TYPE_MAX条记录，再复制，避免第12项越界。
CC=cc python3 -m unittest discover -s tests -p 'test_venus_sq2*.py' -v
python3 -m unittest discover -s tests -p test_sq2_hfi_extradata.py -v
固件需求是平面容量；只读取有效record链到NONE。实际HDR10静态记录仅首帧、VUI每帧；
HLG仅VUI且transfer18。元数据不可由像素/分辨率推测，也不能把上一会话静态HDR带入下一会话。

## 2026-11-08：真实固件metadata到OMX/HIDL

quest2-vidc-bridge.c显式QUEST2_VIDC_NATIVE_EXTRA=1，候选构建可加
-DQUEST2_NATIVE_EXTRA_DEFAULT=1；普通构建默认关闭。需要已部署的v4配套Venus三模块。
OUTPUT S_FMT成功后、REQBUFS前按HEVC7/AVC1启用私有control；辅助最小需求会话保持相同mask。
每槽独立system DMA heap metadata fd，原生offset0，CPU读取完整有界record链后写入原厂实际offset。
早期QBUF不能调用capture G_FMT（SOURCE_CHANGE前合法返回EINVAL），使用协商时读取的原生capacity。
失败仍报告corrupt/SYS_ERROR，原生导入DMA引用继续遵循kernel END/退休保护。

CC=cc python3 -m unittest discover -s tests -p test_quest2_vidc_bridge.py -v
CC=cc python3 -m unittest discover -s tests -p test_quest2_omx_decode.py -v

sq2-mediacodec-frames.c可使用-DSQ2_HEVC_VECTOR及-I真实生成vector目录编译HEVC full-AU缓冲探针，
链接-lmediandk及实际libbinder_ndk.so；按SQ2_VEC_BYTES_PER_SAMPLE选择P01054或NV1221。
不伪造输入color/HDR hints；输出打印真实AMediaFormat色彩与静态HDR块，像素仍需独立比对。
该HEVC诊断模式拒绝AVC特有的CSD参数；--surface-full-au可执行独立Surface诊断，不能当作产品路径已通过。
直接OMX诊断客户端通过真实GetExtensionIndex识别纯metadata变更，保留缓冲，避免误重配而丢帧。
已验证源码/实际构建命令/真机结果见HANDOFF及all-codecs/*metadata*-build.json。


## 2026-11-08：P010 SoftwareRenderer源码候选及独立验证

源码补丁patches/stagefright-p010-renderer.patch用于configs/stagefright-p010-renderer.json记录的
AOSP av源码版本，两个文件必须精确匹配；不能直接声称与现机Android17整库ABI一致。
按该源码树正常构建应重链libstagefright_color_conversion至libstagefright。
当前只有独立诊断产物，未提供系统整库替换/持久打包入口。

当前准备目录all-codecs/renderer-p010-source包含补丁后的media源码、native头、由真实
graphics/common AIDL/HIDL生成的头、libhardware头、libyuv源码/静态库和现机platform库。
源URL/hash和编译记录在该目录；不从Windows启动器目录生成源码或缓存。

    python3 scripts/build-sq2-p010-renderer.py \
      --prepared out/quest2-video-618-20261107/all-codecs/renderer-p010-source \
      --toolchains /mnt/spx-workspace/toolchains \
      --omx-dir out/quest2-video-618-20261107/system-omx \
      --omx-headers out/quest2-video-618-20261107/agent-probe/include \
      --stagefright out/quest2-video-618-20261107/all-codecs/libstagefright-current.so \
      --output FRESH_OUTPUT

构建器核验补丁后源码hash，生成实际几何校验函数include，编译实际AOSP两个源码及同源
FrameRenderTracker。独立tracker仅将未用gui/Surface.h头换为unistd.h，没有行为改写。
-Wall/-Wextra/-Werror下仅豁免上游未用参数/label/variable及旧声明警告。
BUILD.json记录准确命令、输入和二进制hash；输入准备仍需现有平台头/生成器，非从零系统构建。
两个probe仅诊断，不部署服务或打开codec。以现有认证SSH复制至全新临时路径、校验hash后，
Android namespace内用15秒timeout运行p010-renderer-check和p010-surface-check。
CPU测试覆盖真实10bit矩阵/裁剪；Surface测试实际队列/mapper读回，均不证明屏幕tone-map。
NDK解码帧探针新增--surface-full-au和RGB10读回需额外链接-landroid。

## 2026-11-08：Android17 renderer精确ABI集成与CFI验证

build-sq2-p010-renderer.py使用前述参数并加--with-system-adapter，输出libsq2-p010-renderer.so；
它使用实际系统codecbase的FrameRenderTracker，adapter中编译断言精确类布局。
CFI使用-flto -fsanitize=cfi -fsanitize-cfi-cross-dso；不要加入generalize-pointers不兼容选项。
HDR helper来自固定SurfaceUtils参考源码，adapter不反向链接父libstagefright以免循环。

scripts/patch-sq2-p010-stagefright.py --help给出固定版本补丁入口，配置
configs/stagefright-p010-elf.json固定Android17原库SHA/入口指令/RELRO槽位。
仅三个12字节entry thunks，原虚拟地址/导出索引/重定位保持；原Build-ID保留，以SHA区分。
python3 -m unittest discover -s tests -p test_sq2_p010_stagefright.py -v
使用私有实际库夹具（可由SQ2_STAGEFRIGHT_INPUT指定），包含拒绝错误hash和非RELRO槽位。

probe/video/sq2-mediacodec-frames.c加SQ2_IMAGE_HDR_METADATA并链接
probe/video/sq2-image-hdr-metadata.cpp可读实际AHB的SMPTE2086/CTA metadata；不是注入输入参数。
接受候选renderer-p010-adapter-cfi-v3及实际MediaCodec/Surface结果见HANDOFF。
所有构建清单记录实际命令与输入hash；未提供整库跨Android版本兼容承诺。

## 2026-11-08：renderer持久打包候选

    python3 scripts/pack-sq2-p010-renderer.py BASE CFI_RENDERER_DIR FRESH_OUTPUT \
      --expected-base-sha256 ACTUAL_PARENT_SHA

接受configs/stagefright-p010-elf.json中deployment_sha256固定的两个已验证CFI产物。
父包须已有HIDL集成，精确init锚点匹配；新增文件和init之外有效cpio完全不变，原凭据tail最后。
helper先验证bundle和实际系统原库SHA，离线overlay内准备完整库对后发布至system/lib64；
新vendor rc在post-fs恢复目录和两库system_lib_file，不能假定tmpfs默认label正确。
源码和全量系统升级需要重新核验ABI及pins，禁止对未知Android库继续写三处跳转。

    python3 -m unittest discover -s tests -p test_sq2_p010_pack.py -v

覆盖未验证库拒绝、未知系统在mount前退出、仅预期归档变化和凭据保留。
当前仅本地打包和RAM mount/hash/label通过；普通系统路径解码及持久启动未验证，见HANDOFF。

## 2026-11-08：HFI最后轮询判定与默认失败快照

在现有匹配的Venus源码副本运行：

    python3 scripts/patch-sq2-venus-boot-status.py MATCHED_VENUS_SOURCE
    python3 tests/test_sq2_venus_boot_watch.py MATCHED_VENUS_SOURCE
    python3 -m unittest discover -s tests -p test_sq2_venus_boot_status_patch.py -v

仅修正最后poll已ready却因count==limit返回超时；默认超时最后poll记录一次CPU队列/SFR。
轮询上限、正常成功路径、invalid UC_REGION、固件所有权/DMA隔离/错误锁存不变。
snapshot header必须为已知原件或本版，未知loop/header在写入前拒绝；历史generator同步。
构建仍使用当前6.18配套三模块与完整provider检查；不得热卸载或清sys_error验证。
本地候选及真实边界反例见HANDOFF，此项尚无新的真机PM成功证据。

renderer持久helper v2使用现有debug_ramdisk/keystore2.rc系统init覆盖层：
packer在该rc bind成功后插入helper，向system rc追加post-fs restorecon，
不再安装vendor rc执行system标签恢复。v1仅作历史候选，不用于当前部署。
系统路径AVC/HDR10/HLG实际MediaCodec验证已通过；新启动验收以最新HANDOFF为准。

renderer v2持久启动现已实测：system init post-fs标签、库SHA、普通untrusted应用实际maps、
HDR10/HLG正常system查找路径的6帧RGB10+AHB metadata均通过。当前HIDL持久声明仍AVC-only；
HDR回归需单独RAM native-extra-v2，不应把它误写成开机已广告HDR。
诊断APK安装前核对实际资产profile；当前原安装是HEVC，不能直接用它做AVC回归。
先正常唤醒显示再创建Surface，测试完force-stop；保留严格帧/回调/EOS/关闭判定，
285/300 rendered回调仍失败，不以截图替代连续显示证明。

诊断播放器现在同时编译RenderedFrameAudit.java，在原callback等待后报告queued/actual callback
PTS差集、尾部缺口、重复及未知PTS；原严格RESULT不变。主机验证：
python3 -m unittest discover -s tests -p test_sq2_video_render_audit.py -v
记录只在内存收集、结束后一次写出；初次定位回调缺口时不要在播放中截图，引入额外合成负载。
缺少回调不能直接计为真实显示丢帧，也不能仅凭该API缺口证明所有帧已显示。

## 2026-11-08：PTS审计真机对照与PM失败取证

已构建player-render-audit-v1的真实测试：同APK/AVC输入/节奏/boot，OMX292/300、
系统c2.android.avc.decoder300/300；高通严格失败保持，纯尾部解释已排除。
只将软件组件用于显式测试，不修改默认声明；missing callback不能直接当显示丢帧计数。
后续需区分实际Surface排队/呈现/fence，不能以伪造回调修复验收。

最后poll修正后的真实PM失败快照仍status0。现有HFI快照足以排除最后poll-ready误判；
不改变100次超时，不重复未改变的挂死测试，不热卸载/清sys_error。当前健康重启不证明PM修复。
持久日志先取回并核验完整SHA，再删除精确ESP副本。记录见HANDOFF最新节。

## 2026-11-08：整体移植设计入口

后续开发见[高通显示与视频栈整体移植方案](QUALCOMM-FULL-STACK-PORT.md)。
现有脚本只重建已实现的混合栈；尚无一条命令能构建已验收的KGSL/gralloc/msm_vidc/HWC
整套替换，不把本轮进度提交称为完整原厂系统交付。
6.18基线、私有输入隔离、配套校验、600秒SSH窗口与ESP空间维护继续适用。
