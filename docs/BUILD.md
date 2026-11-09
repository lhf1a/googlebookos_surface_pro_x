## 唯一发布基线：v21-release（2026-11-10）

开发只在 WSL Ubuntu-24.04 的 `/mnt/spx-workspace/chromebookOS/work/qualcomm-618` 进行。
当前发布 pins 为 `configs/kernel-baseline.json`，图形路线是 mapper5 + 原厂 gralloc + 源码 QTI HWC/SDM。
Linux 为6.18.60-sq2-android1，默认 `EFI/SQ2Nat`，initramfs `e826df919c358a61ff023f6021ba9dae43d166b1d781a3b107ed8320286456a0`。

```sh
python3 scripts/build.py verify
python3 scripts/build.py package --output out/v21-release-FRESH
python3 scripts/build.py compile --help
```

默认私有输入：`private/build-inputs/kernel-6.18/EFI/SQ2Nat`。`package` 完整校验并逐字节复制8个EFI文件，
不会重新编译、写盘或带入旧 Mesa 包；产物带现有私有SSH身份，不得作为匿名公共镜像发布。
`compile` 是独立候选构建，历史20261107源归档本身不含全部v21修复；必须配对当前
SoundWire、DPU和Venus补丁及ABI，不能称为已完整重建当前运行栈。

清理版从已验收父包71db2317…复建：

```sh
# 对已验收 venus-capture-start-v2 源目录的副本应用，保留原运行协议与结构布局。
patch -d MATCHED_VENUS_COPY -p1 < patches/venus-release-diagnostics.patch
make -C MATCHED_KERNEL_BUILD M=ABSOLUTE_MATCHED_VENUS_COPY ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- CONFIG_VIDEO_QCOM_VENUS=m -j8 modules
python3 scripts/pack-sq2-release.py --base ACCEPTED_V21_INITRAMFS \
  --expected-base-sha256 71db231753f4346f5cd6cdac4ef34410eb017f45b147c84aed424b894daf0d3a \
  --bridge CLEAN_BRIDGE --modules MATCHED_VENUS_COPY --output FRESH_INITRAMFS
```

精确编译命令保存在本地 `out/qualcomm-full-stack-20261108/v21-release-cleanup/` 的
`bridge-build-clean.json`、`kernel-build-command.json`；模块去掉debug符号后组包。
打包器压平历史叠加，保留newc权限/uid/gid/设备号，更新校验清单，原SSH身份member仍在最后，
验证有效文件全集、固件、音频HAL与身份未改变。不得恢复持久日志开关或将探针加入标准发布。
以下日期章节是历史复现记录，已删除的诊断工具可从Git历史查阅，不是当前受支持入口。

## 2026-11-10：SoundWire 能力初始化及 DPU EBUSY 候选

工作根仍为 `/mnt/spx-workspace/chromebookOS/work/qualcomm-618`。
`patch-qcom-soundwire-ahb.py MATCHED_KERNEL` 验证6.18 qcom.c原/结果SHA后无fuzz应用
`patches/qcom-soundwire-ahb-capacity.patch`，已应用幂等、未知源码拒绝；必须以匹配配置/Module.symvers
重编 soundwire-qcom，正常重启加载，不能热卸已注册控制器。`tests.test_qcom_soundwire_ahb`
执行真实AHB读写函数的并发红/绿对照与错误退出；不把源码检查当作听音证明。

`build-sm8350-display.py --dpu-events` 末尾现在应用 `sm8350-dpu-busy-commit.patch`；
配置固定原/结果和patch/hash，保留42TU配套构建。复建命令及产物在
`out/qualcomm-full-stack-20261108/dpu-busy-commit-v1/{build-command.json,build/BUILD.json}`。
`tests.test_qti_dpu_busy_commit` 编译真实 Commit 和真实输出fence helper，12种成功/失败/旧后端场景。
RAM验收冻结入口 `dpu-busy-commit-v2/runtime/runner.sh`；先直接fence，再恢复SF/zygote后页面采集，
有界结束自动撤绑定并恢复原显示。新setter getter不改类布局；libsdmutils差异来自编译路径。

SoundWire构建与专用私有增量打包命令在 `soundwire-ahb-capacity-v1/{build-command.json,pack.py}`；
`qti-system-display-v21-audio-blackout/package-audit.json` 保存有效变更及保留项。
该包只纳入这两项修复，不自动提升 HEVC CSD/五组件 HDR 候选。实际部署/听音状态见HANDOFF。

## 2026-11-09：HEVC CSD 前缀实验的冻结复现入口

当前是 opt-in 源码 OMX 候选，不是正常 builder 默认启用：`out/qualcomm-full-stack-20261108/omx-hevc-csd-prefix-v1`。
`MANIFEST.json` 固定源码/依赖/六个编译单元及 d8699271…产物；`command-v2.json`、
`prepare-prefix-source.py`、`build-candidate.py` 和 `HOST.json` 保留精确命令与 30 项 ASan/UBSan 场景。
输入契约仅为 clear HEVC 的独立 Annex-B CSD + 完整 AU，不是 arbitrary-bytes parser。

`COLLECTOR-BASIC-V2.json` 固定单库 RAM 五阶段采集；`runtime-basic-v2/runner.sh` 只接受其指定
fresh 目录，先停旧服务/PID，再绑定；恢复先停服务、撤销绑定、核验原库和日志属性，再启动。
`audit-basic-pixels.py` 与 `audit-basic-hfi.py` 独立核对 capture-basic-v2；具体参数与结果
见 `independent-basic-v2-{pixels,hfi}.json`。不得只看 runner0；v1 的 mapwatch 竞态失败保留。
这批仅 30 帧固定向量，HEVC 高分辨率 DRC/flush/reset 与真实 Bili 切换继续验收后再集成正常构建。

## 2026-11-09：DPB extradata-size 失败分支的单一清理

完整 `patch-sq2-venus-extradata.py` 现在让原 allocator 的共同 `fail:` 标签清理当前失败对象，
不在 size 检查分支提前 `kfree`。已有生成源码可精确增量修复：

~~~sh
python3 scripts/patch-sq2-venus-extradata.py MATCHED_SOURCE --dpb-cleanup-only
python3 -m unittest tests.test_venus_sq2_extradata -v
~~~

未知/重复锚点拒绝，已修复源码幂等；原有固件所有权与真实 END 退休保护不变。
实际完整 v20 helpers 的旧 ASan double-free 红例及修复绿例保存在
`out/qualcomm-full-stack-20261108/venus-dpb-cleanup-host-v1`，正常候选必须重新配套构建，
不把临时 early-DPB 诊断模块带入正常包。具体部署状态以 HANDOFF 最新节为准。

## 2026-11-09：capture 启动顺序与未提交 VB2 生命周期

从已验证v18的`venus-omx-flush-lock-v1`复制源码到新目录，先运行：

~~~sh
python3 scripts/patch-sq2-venus-omx.py NEW_SOURCE --capture-start-only
python3 scripts/patch-sq2-venus-omx.py NEW_SOURCE --unsubmitted-capture-only
~~~

前者仅让SQ2 decoder在真实capture启动前保留队列；后者复制匹配VB2 header，
并在静态SET_BUFFERS发布前绑定scope。两者须配套：只部署前者的v19已实测残留73211904字节，
完整客户端返回0也不能验收。不能热换provider或通过清退休列表掩盖残留。
完整apply也包含两步；对于已应用capture gate的v19源码，只应用第二步。

真实源码host对照与构建证据：
- `venus-capture-start-host-v1`：旧源码红例、新14种启动/队列场景。
- `venus-unsubmitted-vb2-host-v1`：实际allocator/retire ASan/UBSan，旧header红例与未提交lease回收。
- `venus-static-register-host-v2`：实际静态注册6场景及全部HFI地址发布点审计。
- `native-capture-start-v2/{kernel-build-command,KERNEL_BUILD,package-command}.json`：完整匹配模块集，严格MODPOST。

旧已发布lease仍等真实END/PAS，通用retire的无scope语义不变；不得根据无实例数量猜DMA已安全。
新内核运行探针必须核对新core/decoder note及反汇编，不能只复用v18绝对地址。
`native-capture-start-v1/audit-startup.py`对比旧18次preSTART FTB与候选真实session顺序，
候选还必须6精确帧/PTS、零state错误和最终vb2_bytes0；正常/BT709联合回归不能省略。
本节是复建入口，具体实机是否通过以HANDOFF最新记录为准。

## 2026-11-09：HEVC 独立配置数据对照

`sq2-mediacodec-frames.c` 的 `SQ2_HEVC_VECTOR` 现在支持 `--csd-stripped`（ByteBuffer）和
`--surface-csd`（Surface）：首AU提取Annex-B VPS/SPS/PPS组成Android `csd-0`，图片保留SEI/VCL；
`--csd`额外保留原完整AU，供“独立配置+重复头”对照。未提供参数仍保持完整AU输入。
`SQ2_SPLIT_INBAND_CSD`可逐AU独立排入真实参数包；不改变正常OMX服务/bridge映射。
旧向量header无`SQ2_VEC_BYTES_PER_SAMPLE`时，须依据实际Main/NV12格式显式定义1，不能猜Main10。

~~~sh
python3 -m unittest tests.test_sq2_mediacodec_csd -v
~~~

实际ARM64命令与输入SHA见`out/qualcomm-full-stack-20261108/hevc-csd-v18-v1/BUILD.json`。
主机测试通过不代表独立CSD硬解通过：当前真机完整AU六精确帧，独立参数仍零帧，详见HANDOFF。
生成的unflagged对照仅在ignored out，结果失败；不得将去标志做法带入产品。
新v18跟踪仅用核对后的函数参数、HFI包布局和本次编译offsetof；禁止沿用v17 venus_inst字段偏移。

## 2026-11-09：源码 SM8250 OMX 与 Surface HDR 清理候选

在本文件的 WSL 工作根执行。候选采用 LineageOS `lineage-19.1-caf-sm8250`
提交 `05f3cd8dbfa66f2e79ffda3cb294697374510160`，Quest2 UAPI 来源
`6f22d8e7cb7b48f3026f72bc3dbefef787a62ecc`；准确准备输入由
`configs/sm8250-omx-source.json` 校验。私有依赖仅从已配套设备提取，保留在 ignored out。
此入口依赖已有准备目录，不是从零重建 Android 的入口。

~~~sh
B=out/qualcomm-full-stack-20261108
V=out/quest2-video-618-20261107
python3 scripts/build-sm8250-omx.py \
  --prepared "$B/source-omx-sm8250-v1" --toolchains /mnt/spx-workspace/toolchains \
  --full-stack "$B" --renderer "$V/all-codecs/renderer-p010-source" \
  --omx-dir "$V/system-omx" --private-libs "$B/source-omx-v1/platform" \
  --output FRESH_OMX_OUTPUT --hdr-sequence-reset
python3 scripts/build-sq2-p010-renderer.py \
  --prepared "$V/all-codecs/renderer-p010-source" --toolchains /mnt/spx-workspace/toolchains \
  --omx-dir "$V/system-omx" --omx-headers "$V/agent-probe/include" \
  --stagefright "$V/all-codecs/libstagefright-current.so" \
  --output FRESH_RENDERER_OUTPUT --with-system-adapter --clear-absent-hdr
python3 scripts/patch-sq2-p010-stagefright.py \
  "$V/all-codecs/libstagefright-current.so" FRESH_RENDERER_OUTPUT/libstagefright.so \
  --recipe configs/stagefright-hdr-clear-elf.json
~~~

必须使用四入口 HDR recipe；旧默认三入口只覆盖 P010 renderer。
OMX 仅在实际 CONFIG_CHANGED 清理内部 HDR cache，保留同 sequence SEI 持久性和客户端默认值。
SurfaceUtils 对空 SMPTE2086/CTA861.3 分别传 nullptr，避免 Surface 遗留前一 sequence metadata。
保留 CFI 和 RELRO；不得对未知系统库套用固定入口。

~~~sh
API="$V/all-codecs/renderer-p010-source/native/headers/media_plugin/media/hardware/VideoAPI.h"
python3 tests/test_sm8250_omx_hdr_sequence.py --source FRESH_OMX_OUTPUT/media \
  --headers "$B/source-omx-sm8250-v1/headers" --video-api "$API" --output FRESH_OMX_TEST
python3 tests/test_sq2_hdr_surface_clear.py --helper FRESH_RENDERER_OUTPUT/HdrMetadata.cpp \
  --video-api "$API" --output FRESH_SURFACE_TEST
python3 -m unittest tests.test_sq2_p010_stagefright tests.test_sq2_p010_pack -v
~~~

前两项编译实际源码片段并运行 ASan/UBSan；旧源码红例保留。ELF/打包13项已通过，
但持久 packer 仍只接受旧已验证三入口产物；本候选未获完整联合验证，不更新部署 pins。
实际命令/产物清单见 `$B/source-omx-repro-command.json`、`hdr-surface-clear-v1-command.json`。
严格构建 OMX `9d9a5b94…` 已生成但未实机测；实机测试使用 `9304b81f…`，不可混写为同一产物。

实机 `source-omx-hdr-sequence-v2`：固定320×240同流 HDR10→Main8 SDR→HLG30帧，
独立 HDR10/HLG12帧与 AVC30帧，像素/PTS/格式/实际AHB metadata通过。
完整 `source-omx-hdr-joint-v1` 随后在 AVC SDR DRC 输出13帧后固件致命错误，整组失败。
这不证明 HDR 分辨率切换或物理屏幕 tone-map 完成；Main10继续过滤。
DRC／Bilibili画质切换是当前必须解决项。诊断需保留实际HFI交互和DMA所有权，
终端固件错误后停止实验并正常重启恢复，不清错误、不热卸载provider或重复PAS。

## 2026-11-09：v18 私有 OMX flush 锁修复复建/复验

工作根固定WSL /mnt/spx-workspace/chromebookOS/work/qualcomm-618；B=out/qualcomm-full-stack-20261108。
源码适配scripts/patch-sq2-venus-omx.py --flush-lock-only须作用于已有ctx_q_lock的配套源码；
完整apply也包含此改动。新增两字段必须重编全Venus模块组，不能热替resident core或混用旧布局。
本轮从venus-session-end-v1复制c/h/Makefile/Kconfig，只改core.h、vdec.c、venus-sq2-omx.h；
命令/源hash/strict MODPOST结果见native-omx-flush-lock-v1/{kernel-build-command,KERNEL_BUILD,source-changes}.json。
打包复用pack-sq2-lts-venus-queue-lock.py，基线v17 ae39c924…，保留私有SSH尾；
v18 caeb6a4b…有效6项变化、1296条不变，见qti-system-display-v18-flush-lock/package-audit.json。
部署与Android正常启动核对在qti-system-display-deployment-v12；原Image/固件/音频/OMX/桥/显示未改。

~~~sh
python3 -m unittest tests.test_venus_sq2_omx tests.test_sq2_session_end_trace_audit tests.test_sq2_venus_capture_layout -v
python3 "$B/native-omx-flush-lock-v1/audit-initial-flush.py" "$B/native-omx-flush-lock-v1/baseline-capture" blocked
python3 "$B/native-omx-flush-lock-v1/audit-initial-flush.py" "$B/native-omx-flush-lock-v1/candidate-capture-v2" unblocked
~~~
10项直接相关检查全过含实际header ASan/UBSan；旧header红例在old-header-red/。
较广focused-unittests为33通过+5项未配置旧reference fixture而skip，不能标38全过；
首次将CLI测试误当unittest的导入失败也留存，不算产品/固件故障。
真实客户端只测初始私有flush，baseline两次IRQ回调阻塞超时，candidate五组0/0.235–0.550ms。
运行模块note逐字核对，旧/新结构字段不混读；真实包字段/函数参数采集无需猜偏移。
Bili原始取证/只读HAL uprobes在bilibilihd-seek-av-v3，完整runner0，无丢失并清理。
正常联合入口qti-system-display-v18-acceptance-v2/runtime/runner.sh；完整runner0，
该目录audit-commands.json记录四独立审计全0，150精确帧/120物理输出回调及额外BT70960帧/30物理通过。
嵌套SHA256SUMS应先子清单后父清单，不能用改掉清单检查来通过。
内屏SDR、Main10过滤和所有full_*false保持。

## 2026-11-09：v17 Venus 独立 END 确认修复复建/复验

工作根固定WSL /mnt/spx-workspace/chromebookOS/work/qualcomm-618，
B=out/qualcomm-full-stack-20261108。只改probe/video/venus-sq2-session-end.h一个判断，
保留session_error，END超时/负回包/core_error保护不变。struct/模块ABI未改。

真实源快照venus-session-end-v1由已运行venus-capture-layout-v2派生，仅该header不同。
native-session-end-v1/kernel-build-command.json记录make命令，严格MODPOST（没有WARN降级），
KERNEL_BUILD.json记录源码/产物SHA；旧dec/enc二进制逐字相同。
pack-sq2-lts-venus-queue-lock.py命令在native-session-end-v1/package-command.json，
包含真实System.map/depmod及私有SSH尾保留；不自动部署。
qti-system-display-v17-session-end/package-audit.json独立校验两份core+SHA清单三有效变化，
1299条其他有效记录/metadata和凭据尾不变。部署/正常启动见qti-system-display-deployment-v11。

源码回归：
~~~sh
B=out/qualcomm-full-stack-20261108
python3 tests/test_sq2_session_end.py out/qualcomm-full-stack-20261108/venus-session-end-v1 --strict
python3 tests/run_sq2_venus_close_guard_harness.py out/qualcomm-full-stack-20261108/venus-session-end-v1 out/qualcomm-full-stack-20261108/native-session-end-v1/close-guard-repeat --assert-safe
python3 -m unittest tests.test_sq2_session_end_trace_audit -v
~~~
20个END场景ASan/UBSan、12个实际close方法场景和5个审计解析/负例覆盖通过；
主机注入不等同于固件证据，旧header在新回归下的红例已留存。

真实采集入口native-session-end-v1/v2/v3/runtime/runner.sh；
v1正常180帧/30个真实END，v2相同损坏AVC导致“成功END+旧session_error→拒绝确认”，
v3新模块确认END但OMX收尾慢，runner1/timeout124反例保持。
字段偏移来自native-session-end-v1/offset-build-command.json对精确core.h的编译，
实际模块.note校验匹配才使用；私有kprobe/trace实例有界采集、清理并恢复全局设置。
审计成功基线/原失败分支：
~~~sh
python3 scripts/audit-sq2-session-end-trace.py --capture "$B/native-session-end-v1/capture" --expect baseline --output "$B/native-session-end-v1/independent-end-trace-audit.json"
python3 scripts/audit-sq2-session-end-trace.py --capture "$B/native-session-end-v2/capture" --expect retained-error-rejected --output "$B/native-session-end-v2/independent-end-trace-audit.json"
~~~
retained-error-confirmed模式还要求完整runner/最终驱动健康，不得据v3原runner1强行标通过；
v3仅独立观察JSON证明真实END确认且随后同PID自然回到零实例/缓冲。
不能把quarantine函数入口或固定error参数当作实际隔离。

正常v17整组入口qti-system-display-v17-acceptance/runtime/runner.sh，动态发现屏幕几何/stack，
原OMX/桥/显示/mapper维持，结果见HANDOFF。内屏仅SDR，Main10仍过滤，full_*保持false。

## 2026-11-09：物理 P010 / 灰阶 / 同流与联合候选复跑

工作根明确为WSL /mnt/spx-workspace/chromebookOS/work/qualcomm-618；
以下B=out/qualcomm-full-stack-20261108。内屏仅SDR，目标HDR→SDR。

构建：native-p010-window-v1/build/commands.json（HEVC JNI/Java），
native-p010-window-v4/build/commands.json（SDR VUI对照），native-p010-gray-v1/build/commands.json（灰阶）。
实际target libnativewindow.so来自renderer-p010-source/platform，使用system libc++，不用NDK libc++_shared。
AVC原JNI默认行为保留。最新桥构建在native-p010-inband-v2/build/command.json及BUILD.json，
增加隐式bitdepth G_FMT与旧有效layout差异的标准S_FMT选择；旧七参数/v2最小size契约保持。
物理/灰阶用surface-v3旧桥，不能混同最新桥。同流v3的ignored桥快照只加拷贝前raw-record日志，
不修改FW payload、不放进正常启动包。

复验命令（工作根执行）：
~~~sh
B=out/qualcomm-full-stack-20261108
python3 "$B/native-p010-window-v4/audit-physical-p010.py"
python3 "$B/native-p010-window-v4/audit-physical-p010.py" --trial "$B/native-p010-gray-v1"
python3 "$B/native-p010-inband-v2/audit-inband.py" --capture "$B/native-p010-inband-v3/capture" --output "$B/native-p010-inband-v3/independent-inband-audit.json"
~~~
前两项分别120/90输出/回调通过，含PTS/SF最终合成类型/SDR target/KMS别名去重/CRC和PNG；
灰阶从原PNG重新测量对标准公式，误差<1级。四项require-dpu失败明确保留，严格DPU像素未通过。
两个临时副本负例拒绝，原始捕获不改。
第三项目前应exit1：30帧像素/PTS/格式/VUI正确，第10..29帧HDR静态metadata残留，
脚本先写分项JSON，再返回整体失败，不把runner0当成完成。

联合入口native-p010-joint-v2/runtime/runner.sh。v1尺寸变量误插cleanup已保留；
v2真实1080p DRC九帧后timeout124/Venus quarantine，未完成物理阶段、未部署。
失败捕获/dmesg完整取回，原正常绑定恢复后做一次正常v16重启，见HANDOFF。
干净boot相同DRC正常/候选对照入口native-p010-drc-compare-v1/runtime/runner.sh；
任何错误/timeout/quarantine停比较，不热卸provider或手工清隔离。

72项相关主机检查命令和结果在native-p010-joint-v1/host-test-command.json、host-tests.txt，
CC=gcc、无skip；较早65项与本组72项不能重复相加。
Main10仍过滤，full_*仍false，正常v16/唯一回退保持；源码手册未提交推送，原始资料仅out。

## 2026-11-09：原生 P010 成对 ABI 与 42 帧实机验证

工作根仍为WSL /mnt/spx-workspace/chromebookOS/work/qualcomm-618。
正常包仍v16；以下是RAM候选的真实构建/运行记录，不是默认Main10或物理tone-map完成。

成对源码：quest2-omx-service.cpp、quest2-omx-native-layout.{h,cpp}、
quest2-omx-native-layout-check.h、quest2-vidc-bridge.c。
P010 Venus0x7fa30c0a对应V4L2 P010，布局stride为字节、ANW stride为像素。
bridge先标准S_FMT选择实际格式，再协商该格式padding；v2回调携带真实minimum_size，
必要时探测真实较高gralloc分配，不伪造size。旧回调ABI仍导出，bridge明确查v2符号。
pack-sq2-native-omx.py按实际桥的v2引用检查服务配对；旧3个导出契约保持可复建。

最终候选服务/桥的完整构建命令与source/output哈希在
out/qualcomm-full-stack-20261108/native-p010-surface-v3/build/commands.json及BUILD.json。
新读回复用quest2-mapper5-video-buffer.cpp的实际布局，AHB import持有独立引用，
释放和原allocate调用者严格usage校验保留；已知consumer描述符与实际分配的0x2400提示差别
有真实日志和既有node-usage契约，不接触产品像素。
客户端构建记录为native-p010-surface-v7/build/commands.json（实际mapper5布局读回）、
native-p010-surface-v8/build/commands.json、metadata-build-command.json和
avc-metadata-build-command.json（复用sq2-image-hdr-metadata.cpp读取真实AHB元数据）。
必须链接目标system libc++.so，不能混入NDK libc++_shared.so。

最终运行入口native-p010-surface-v9/runtime/runner.sh，三客户端HDR10→HLG→AVC，
结束恢复原init服务/桥。runtime各新增ELF必须带可执行mode；v8的HLG126设置失败保留。
源Main10 profile过滤保持，未安装或广告候选；内屏只验SDR及HDR→SDR，物理tone-map尚未过。

独立复验命令：
python3 out/qualcomm-full-stack-20261108/native-p010-surface-v9/audit-native-p010.py
从实际编译向量重建FFmpeg P010/NV12参考，逐帧对像素/PTS/AHB dataspace及HDR metadata。
该轮42帧通过，两个临时副本负例拒绝；原始捕获不修改。
65项完整主机命令保存在同目录host-test-command.json，使用CC=gcc避免默认clang缺失造成skip；
host-tests.txt确认65项全过。实际NV12/P010布局ASan/UBSan命令为layout-test-command.json；
最小分配、16-bit末sample边界、格式切换/部分失败恢复及v2 ABI配对均有实际源码检查。

HFI对照：vui-hfi-event-v1/runtime/runner.sh复用69帧向量和private trace实例，
在ARM64 hfi_event_notify入口固定读取x2所指消息的size/event_id/data1/property_count
（offset0/12/16/20）；不解引用属性payload。ABI来自匹配hfi_msg_event_notify_pkt。
每组实际SEQ_CHANGED数1/1/3，与driver回调对应；无丢失，恢复全局设置并删除自身probe/实例。
最终健康和限制见HANDOFF；不把事件头当成全HFI包或普遍固件能力证明。

## 2026-11-09：SPS 编号 / CODEC_CONFIG / DRC 的可复跑诊断

工作根仍为WSL /mnt/spx-workspace/chromebookOS/work/qualcomm-618。
内屏确认无HDR能力；内屏验SDR及HDR→SDR，外接HDR按实际sink/link能力，不更改能力声明。

probe/video/sq2-mediacodec-frames.c的add_csd只取首组SPS/PPS；
copy_au_part共享精确Annex-B过滤，诊断宏SQ2_SPLIT_INBAND_CSD将每段真实参数
以AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG提交，不解析/合成颜色。
默认full-AU和原QUEUE日志格式保持。诊断构建命令保存在
out/qualcomm-full-stack-20261108/vui-inband-csd-v1/commands.json；
DRC的最终命令为vui-drc-color-v2/build-command.json，runtime/runner.sh是实际运行入口。
DRC头需SQ2_VEC_DYNAMIC及逐帧width/height，pacing使用既有上限内的100000us；
v1无codec创建的exit64保留为实验设置失败，不放宽工具参数范围。

probe/video/sq2-v4l2-decode-test.c以SQ2_VEC_FRAMES检查完整mask/输出帧数/输入归还，
向量帧数静态限定1..32，并归一化输入timestamp sec/usec。原硬编码63使30帧误报失败。
运行python3 -m unittest tests.test_sq2_mediacodec_csd tests.test_sq2_v4l2_vector_bounds
tests.test_sq2_v4l2_runner tests.test_native_video_window_normal -v：
3个unittest及import时执行的既有runner阶段/清理检查通过；不把后者另算unittest。
实际helper在ASan/UBSan运行，覆盖初始CSD、三种AU字节过滤、1/6/30/32及0/33拒绝。

同尺寸两组CODEC_CONFIG共60帧的像素/PTS独立核验见vui-inband-csd-v1/independent-pixel-audit.json。
DRC最终9帧按每帧真实几何将I420转NV12逐字对比独立参考，并核对raw VUI/SDK/PTS，
见vui-drc-color-v2/independent-pixel-vui-audit.json。
vui-sequence-trace-v1/runtime/runner.sh复用已验二进制，在private trace实例只启用
vdec_event_change/hfi_event_notify函数追踪，三个阶段标记及退出码保存在原始trace。
独立69帧/回调审计见independent-sequence-audit.json；同尺寸各仅初始化1次，DRC为3次。
全局tracing保持、每CPU无丢失，结束移除私有实例并恢复原服务/桥；这是函数证据，
尚非原始HFI消息审计。不要把诊断日志版服务/桥或探针装进正常启动包。

本组未部署/重启/修改EFI，正常v16-bounds与唯一回退保持；完整结果和健康状态见HANDOFF。

## 2026-11-09：v16 Composer fence 输入/短命令修复与真实错误诊断

用户已确认内屏不支持HDR：内屏按SDR验收，HDR内容验证解码/元数据和HDR→SDR映射；HDR直出仅按外接sink实际能力启用与验收。

最终source patch为patches/sm8350-composer-acquire-fence.patch，固定基于此前command-errors
后的两份QTI源码；configs/sm8350-display-reference.json固定原/结果和patch哈希，
scripts/build-sm8350-composer.py在原错误传播patch后无fuzz应用，并记录输入及结果。
最终composer-v22为46TU，配原display-v20（42TU）；完整命令见
out/qualcomm-full-stack-20261108/composer-fence-errors-v1/composer-build-command.json，
复建最终版需选新output目录，不能把旧v21/首版v16作为最终部署产物。

scripts/build-sq2-composer-fence-probe.py构建现有诊断；诊断程序新增运行参数
--fence-errors SW_SYNC_PATH，
由run-sq2-composer-fence.sh TRIAL --fence-errors运行。需要真实标准sw_sync接口；
当前内核CONFIG_SW_SYNC未编入，故私有out用匹配源的sw_sync.c/sync_debug.c与原
完整.config/Module.symvers编译临时fixture，完整MODPOST。sw-sync-build-command.json/
SW_SYNC_BUILD.json保存来源/实际变更；控制节点0600、独立debugfs目录、无module_exit。
runner核对已加载note后复用，不热卸provider；正常包不能包含fixture，重启验证其消失。
支持已有标准接口时可直接运行probe --fence-errors PATH，不需要改正常内核。

scripts/audit-sq2-composer-fence-errors.py --capture CAPTURE --composer-build MATCHED --output JSON
独立核验sync_file负状态、INVALID FD的BAD_PARAMETER/前后DRM一致、实际context/seqno
DPU等待、原缓冲归属/15协议错误恢复、trace无丢失与成对运行库。明确限受控测试源。
最终证据composer-fence-errors-v3/independent-error-audit.json；v1失败/v2中间结果保留。
实际源码与审计负例等34项：tests.test_sm8350_composer_acquire_fence、
test_sq2_composer_fence_errors_audit、test_sm8350_composer_command_errors、
test_sm8350_display_build、test_sq2_composer_ownership_audit、
test_sq2_composer_gpu_acquire_audit、test_pack_sq2_qti_system_display。
新66场景覆盖FD/资源/兼容/缓存不变及短命令；可用QTI_ACQUIRE_SOURCE选择确切原/结果源码。

正式v16-bounds三阶段pack-command.json及package-audit.json保存完整闭包；
最终SHA c967ade0…，相对v15为3个composer ELF+2清单，1297其他记录和凭据尾保持。
正常启动、真实GPU acquire、自动BT709、normal-v9联合回归已通过，见HANDOFF。
normal-v9的严格mixed全trace失败保留，额外audit-complete-output-pairs.py只验完整60个
非空完成及交叠，显式列出零字节未配对返回，不能将该局部审计当全trace或性能证明。

## 2026-11-09：v15 自动 SDR VUI 与 SF 传递函数配置

工作根为WSL /mnt/spx-workspace/chromebookOS/work/qualcomm-618。
公开bridge builder使用 --with-vidc-bridge --native-extra-default；后者必须配前者，
仅bridge编译增加QUEST2_NATIVE_EXTRA_DEFAULT=1，其他工具/默认构建保持。
真实重建命令和BUILD.json位于out/qualcomm-full-stack-20261108/
vui-propagation-v5/build/；最终正常bridge SHA256 2a120b31…，service仍7a6aa750…。
Q2COLOR/Q2VUI仅QUEST2_COLOR_TRACE诊断宏启用，正常包不带该宏/observer/指导候选。

configs/sq2-qti-system-composer.rc在on init设debug.sf.treat_170m_as_sRGB=0，
在SurfaceFlinger启动前保留真实SMPTE170M；不是强制所有视频为709。
v15按qti-system-display-v15-display → v15-video → v15三阶段封包，
各pack-command.json记录完整参数；沿用成对display/composer-v20、mapper5/v36与
匹配msm f3ba9cf7…，最终包029a1a8a…；相对v14仅5有效记录，1297其他记录/
metadata与最后凭据member保持。不得将本地私有启动包作为匿名公共镜像发布。

scripts/audit-sq2-bt709-video.py保留严格HWC0x10c10000/DEVICE及KMS709 limited条件；
vui-v1/v4必须拒绝，vui-v5与正式v15-acceptance通过。精确命令在各audit-command.json，
参考仍sdr-color-v1/bt709/reference.nv12，不能改参考来放行。
正式联合回归及三独立审计位于qti-system-display-normal-v8；动态资源/原init服务，
无RAM替库或属性修改。normal-v7-vui的单项CRC失败保留，不能以客户端exit0代替验收。

52项检查：python3 -m unittest tests.test_build_quest2_video_probe
tests.test_quest2_omx_decode tests.test_quest2_vidc_bridge tests.test_sq2_bt709_video_audit
tests.test_pack_sq2_qti_system_display tests.test_pack_sq2_native_omx -v。
诊断指导候选的测试仅out保留，不计入52项。主机硬件边界为注入，不代表实机全栈通过。
正常重启至少120秒再SSH；本轮首次151.2秒。完整状态与未完成边界见HANDOFF最新节。

## 2026-11-09：VPU/HWC 归属与内核 SDR 色彩增量

最终本轮61项相关主机检查及git diff --check通过；不是完整项目/跨设备/PM验收。

工作根仍为WSL /mnt/spx-workspace/chromebookOS/work/qualcomm-618。
scripts/build-quest2-vpu-hwc.py --composer-build MATCHED_COMPOSER --mapper-build MAPPER5
--output FRESH_DIR复用真实BUILD.json的平台flags，构建直接OMX/HIDL composer客户端
和仅该诊断启用observer的bridge；不要将observer bridge装入正常OMX服务。

run-sq2-composer-fence.sh TRIAL --vpu-hwc需要regular/drc码流、AU/PTS清单、
匹配二进制、CRC/wakelock helper和SHA256SUMS，结束始终恢复正常服务和动态用户。
两组各30帧，固定100ms观察窗口，不是性能测量。audit-quest2-vpu-hwc.py完整命令在
vpu-hwc-v2与vpu-hwc-bt709-v1的audit-command.json。BT709仅针对已知测试向量显式使用
SQ2_COMPOSER_VIDEO_DATASPACE=bt709，不作为正常产品覆盖，不宣称自动OMX VUI修复。

内核：对已验caps/fence/UBWC的6.18 dpu_plane.c执行
scripts/patch-sq2-dpu-sdr-color.py DPU_DIR，同时加入公开dpu-sdr-color.h；
匹配完整.config/Module.symvers构建msm.ko，完整MODPOST，不热卸载活动模块。
真实命令在display-kernel-sdr-color-v1/build-command.json，模块f3ba9cf7…；
成对显示服务仍由既有builder重建display-v20/composer-v20。
pack-sq2-dpu-display.py仅换msm记录，保留所有其他metadata和最终凭据member。

新增检查：python3 -m unittest tests.test_quest2_vpu_hwc_audit tests.test_sq2_dpu_sdr_color
tests.test_sq2_bt709_video_audit tests.test_quest2_omx_decode tests.test_quest2_vidc_bridge -v。
源码系数/选择器、未知来源和所有权错误使用ASan/UBSan；不替代实机物理像素验收。
真实BT709自动传播使用scripts/audit-sq2-bt709-video.py验收，旧sdr-color-v2必须失败；
不得删除color-standard=1等条件放行当前实际BT601输出。完整状态见HANDOFF最新节。

## 2026-11-09：真实 GPU acquire/UBWC 缓冲归属通过，修复继承平面交接

以已部署正常v12及真实NV12 release/FMQ修复为基线，整体目标仍进行中。
继续既定mapper5 + 源码QTI HWC/SDM + Linux6.18 + 原厂gralloc，不重新比较mapper路线。

新增GPU生产者直接HIDL诊断：3个真实AHB RGBA UBWC target，按运行时物理分辨率
2880×1920各渲染10次，共30帧、27次缓存复用。GPU直接写入用于client target的同一AHB，
EGL导出真实native sync_file后立即交HWC，不先在CPU等待该acquire；GPU读回逐像素
验证放在物理present fence完成后。合计165,888,000个RGBA像素精确，是GPU内存读回，
不作为DPU严格像素或面板端到端时延证明。

每帧日志记录AHB ID/slot与导出、信号状态；独立tracefs实例记录同一生产者的render/
export/present标记。以实际display commit wait的context/seqno，关联该帧render区间
创建的唯一对应GPU scheduler fence，避免只按FD、anon-inode或相近时间猜身份。
两轮均30个acquire导出时pending，30个内核display wait；实际在wait期间GPU发信号
分别29/27次，其余为已完成后进入等待，不强称每次都阻塞。
调用栈含drm_atomic_helper_wait_for_fences→commit_tail→commit_work。
随后逐帧关联DPU release到下一target替换，重用前已完成；末帧由empty flush撤下。
两轮全局tracing设置保持、所有CPU overrun/commit overrun/dropped events为0，
原始trace统计分别1787与1786项。未注入fence错误。

首轮正常v12实际KMS仍多出一个未由当前SDM提交的XR24/fbcon平面，虽然被不透明
全屏GPU target覆盖，不能写成干净交接。根因是DRMPlane::InitAndParse将assigned CRTC
置0，丢掉上个master/fbcon的实际绑定，既有UnsetUnusedResources无法找到它。
新sm8350-dpu-inherited-planes.patch仅对无下游CAPABILITIES的标准DPU路径，采用
drmModeGetPlane提供的真实CRTC作为初始assigned状态，复用原有TEST_ONLY/commit/
失败重试/按CRTC清理。未按plane号、fbcon名字、样机或CRTC编号写特判；
主机实际源码方法验证解析失败、legacy不变、其他CRTC保留、TEST_ONLY不改归属、
失败commit重试、未请求平面撤下及被请求平面保留。硬件调用为注入，不冒充实机失败注入。

匹配display-v19/composer-v19（42/46TU）在原init服务RAM成对绑定实测：
30帧GPU/acquire/复用继续通过，目标CRTC仅预期AB24 UBWC client target；
原继承平面已撤下，末帧flush和原服务/库/用户恢复完成。
独立GPU审计与负例、实际源码和打包相关39项测试通过。
证据：out/qualcomm-full-stack-20261108/composer-gpu-acquire-v1与v2/
independent-gpu-audit.json；v1按显式baseline保留inherited_plane_handoff=false，
v2严格验收且核对实际service/成对库hash，未用允许遗留选项放宽修复验收。

qti-system-display-v13已部署为默认SQ2Nat并完成正常重启，SHA256
c9e2eedae75c80b7e7d2662e5e685da23d2f94989432f64fedb788981a62a34f；
boot d0af5276-5ab5-420e-8e66-efe44b5daf29，请求后第140秒首次SSH观测成功，
仍为6.18.60-sq2-android1。62显示文件/13链接/17HIDL核验通过；
实际composer进程67ed8868…、libsdedrm7ab1e17a…、libsdmutilsc33e0d21…匹配成对构建。
在任何视频测试占用VIG前，独立boot-drm审计仅有预期AB24 UBWC client target，
证实正常启动也完成继承平面交接。唯一SQ2QDbg回退5b9f7012…保持；
没有新增备份或入口，5显示ELF+2校验清单变更、1295其他有效记录和凭据尾保持。

正常v13的qti-system-display-normal-v5一次连续16客户端单项及runner均exit0：
7图形、5解码、4物理窗口。150帧可见解码像素/PTS对新生成FFmpeg参考精确，
含320×240→1920×1080→320×240真实DRC；paced mixed实际输出交叠793ms、
17次fd切换，不作为吞吐或端到端时延。原生NV12 DEVICE、多视频2x/0.5x、
UI遮挡A/B/A及撤窗平面/CRC恢复由三个独立审计通过，4物理窗口120输出/回调。
最终同boot、OMX PID5288贯穿、六服务running、Awake、用户secure=false、
disabled=true/showing=false，试验holder释放，总wake locks=1（用户应用保留）。
无实验进程/挂载/图层，Venus error/sys_error/hfi_error/insts/vb2_bytes全0，
ESP可用1,243,372KiB，/logs4,180KiB。证据：qti-system-display-deployment-v7/
及qti-system-display-normal-v5/独立审计和final-health.txt，均在本地out。

复跑主机检查：python3 -m unittest tests.test_qti_dpu_inherited_planes
tests.test_sq2_composer_gpu_acquire_audit -v；GPU构建入口
scripts/build-sq2-composer-fence-probe.py，服务端运行run-sq2-composer-fence.sh
TRIAL --gpu-targets。独立审计入口scripts/audit-sq2-composer-gpu-acquire.py，
修复验收必须传成对--composer-build/--display-build，不使用--allow-inherited-plane；
后者仅用于保留首轮反例，不是正常放行条件。完整复跑参数/哈希在各BUILD.json。


逐缓冲VPU/decoder acquire归属、真实fence错误处理、严格DPU像素、物理高分辨率视频/
DRC/更多颜色格式、HDR/secure/videoUBWC、性能/PM、KGSL/下游msm_vidc、
真实QTI C2与跨SQ1/SQ2仍待推进。full_fence_lifecycle_verified/full_hwc_verified/
full_stack_complete保持false；本轮源码/手册尚未提交推送。私有库、截图/trace/raw仅out。

## 2026-11-09：真实 Composer HAL 缓冲所有权与 FMQ 异常契约已闭合一条受控路径

沿用 mapper5 + 源码 QTI HWC/SDM + Linux 6.18 + 原厂 gralloc 主线。
新增标准 HIDL2.4/FMQ 诊断客户端，连接正常 init composer/allocator/Power HAL；
不接管 DRM master，不使用 eventfd 模拟 sync_file。动态 hotplug 发现显示，
先以真实透明 RGB client target 完成 CLIENT 启动帧，再转 NV12 DEVICE。
ClearClientTarget 请求按已清零的目标处理；不放宽合成策略或硬件校验。

HIDL SET_RELEASE_FENCES 属于前一帧，不能把返回 FD 关联到本帧。
原正常服务与修复候选各完成30帧、3个不同 AHB ID、30个逐缓冲 release关联、
27次HAL槽缓存复用；CPU重写前poll及SYNC_IOC_FILE_INFO均确认release已signal，
各点无负error status。末帧先用CLIENT替换取回最后DEVICE的release，再destroy/empty flush。
观察到真实msm-dpu/dpu-video-buffer-release点；FD号和anon-inode不作为唯一fence身份。

实际负例发现并修复 QtiComposerClient.cpp 的两个错误：2.1/2.2/2.3三个execute接口
固定回报NONE，掩盖parse/writeQueue失败；无效长度在parse失败后仍endCommand，
触发底层未消费payload断言并终止服务。新patch在错误处直接返回BAD_PARAMETER，
正常命令再endCommand，三接口回传实际err并保留reset。
原末条无效命令也不再因isEmpty误报成功。主机运行实际parser/三接口方法的24场景，
旧版15个契约不匹配、修复0；FMQ存储/写失败为注入，未称主机模拟完整Binder。
实机12个错误矩阵（3接口×未知末条/未知前缀/截断/错误payload长度）
全部BAD_PARAMETER，且每个之后正常validate/present及真实present fence通过。
未等待异步present的早期恢复诊断曾触发真实EBUSY；保留失败，现按fence等待后继续。

证据：composer-hal-ownership-v5与v8/independent-ownership-audit.json；
v6/v7失败记录保留，v11包未部署并标记superseded。源码patch可重放：
patches/sm8350-composer-command-errors.patch，匹配构建sm8350-composer-v18（46TU）/
sm8350-display-v18（42TU），未直接修改ignored源码冒充保存。
诊断/审计/打包相关26项测试通过。原始记录和私有产物仅out，不上传。

qti-system-display-v12已替换默认SQ2Nat并完成正常重启：SHA256
b435e962b5460dce3ba22b952f7f99567ff9e22e7a0e0f27f78815b81f25555d；
boot ef82f1f3-61f1-46e3-ae78-7a934b13d85b，首次SSH在请求后140秒，内核仍6.18.60-sq2-android1。
实际composer进程SHA c2429eeb49bfa18011155eaa94c1cb4f0e74e45b432e6c35c019ed46c113f42c，
62显示文件/13链接/17HIDL核验通过。仅3显示ELF+2校验清单变更，1297其他有效
记录保持，凭据尾逐字相同；唯一SQ2QDbg回退5b9f7012…保持，没有新增备份或启动项。

正常启动后qti-system-display-normal-v4一次连续联合回归16个客户端单项及外层exit0：
7图形、5解码、4物理窗口客户端。150解码帧可见像素/PTS对新生成FFmpeg参考精确，
DRC320×240→1920×1080→320×240；paced mixed真实输出区间交叠825ms、19次fd切换，
不作为吞吐/时延或硬件同时执行证明。物理NV12 DEVICE、多视频2x/0.5x缩放、
UI遮挡A/B/A和撤窗硬件平面/CRC回收由三个独立审计通过，4窗口共120输出/回调。
最终同boot、OMX PID5476贯穿、六服务running，实验holder释放，无试验进程/挂载/图层，
Awake、总wake locks=1（用户应用保留），Venus error/sys_error/hfi_error/insts/vb2_bytes全0。
动态用户经cmd lock_settings核对secure=false，disabled=true/showing=false。
早期helper误用am is-user-secure的平台差异已修正并实机核验，没有清除凭据。
证据：qti-system-display-deployment-v6/、qti-system-display-normal-v4/三独立审计及final-health/
lockscreen-check.txt，全部位于out/qualcomm-full-stack-20261108。


该受控证明限CPU写入linear NV12 320×240单DEVICE层。逐缓冲GPU/解码acquire归属、
fence错误注入、严格视频DPU像素、更广物理分辨率/DRC/格式、HDR/secure/videoUBWC、
性能/PM、KGSL/下游msm_vidc、真实QTI C2和跨SQ1/SQ2验收仍待完成。
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete仍false。
本轮源码/手册尚未提交推送。

## 2026-11-09：v10 退窗时钟 CRC 对照与真实内核 fence 顺序/等待路径通过

当前仍是正常v10、boot f36892b1-b79f-4562-bf8c-62fa96ec64a0、默认SHA5a753828…；
本组没有更新系统包、EFI或重启。三轮各30帧全屏原生NV12继续DEVICE-only，
无RGB client target，1080连续硬件CRC与撤窗回收通过；原始日志/截图/trace仅本地out。

自然跨分钟对照显示，前后桌面仅1642像素变化，完整差异bbox[110,24,291,49]，
全部在实际StatusBar frame[0,0,2880,68]内；裁剪目视确认时钟26→27及相邻日期排版变化，
其他桌面像素逐字相同。随后用临时RGB图层遮住运行时发现的68高状态栏、
置于全屏视频下方再跨分钟：前后5,529,600桌面RGB像素完全一致；
双LM CRC在视频前保持192扫描，撤窗后原pair精确恢复179扫描，随后移除遮挡回正常桌面。
遮挡期间视频仍为唯一HWC DEVICE/SDE/KMS NV12，未借新增client target规避全设备路径。

这组受控复现将当前现场的内容变化与显示恢复路径区分开。历史最初v10全屏轮缺少
before PNG，保留其左LM变化原始记录，不据新实验追溯证明所有历史差异原因；
严格视频DPU像素仍未验收。临时遮挡、进程及图层均已移除，没有修改状态栏/时间设置。

三轮使用独立tracefs实例，global tracing设置前后一致，所有CPU overrun/
commit overrun/dropped events=0。新 scripts/audit-sq2-dpu-fence-trace.py 按context/seqno
跨driver名称变化关联信号/销毁，而非依赖FD号或render callback；6个解析/负例测试通过。
实际分别观测36/39/35组同任务相邻retire/release创建，所有可比较release信号均处于
下一已观测commit的init→present区间；每轮最后一个standing-scanout release在trace末仍pending。
销毁有限观测分别66/72/64，不把仍持有的signaled引用或末帧pending当成已证泄漏/错误。

第三轮wait_start栈含dma_fence_wait_timeout→drm_atomic_helper_wait_for_fences→
commit_tail→commit_work，4个真实GPU scheduler fence在显示commit等待期间发信号后结束等待。
这证明实际kernel acquire等待路径；仍不提供每个HAL帧/缓冲身份映射或fence error status，
full_fence_lifecycle_verified/full_hwc_verified/full_stack_complete保持false。
下一步需用真实composer HAL ownership/reuse与release/present接口闭合逐缓冲契约，
并继续高分辨率/物理DRC、HDR/secure/videoUBWC、性能/PM、KGSL/msm_vidc/C2与跨设备目标。

最终同boot、六正常服务running，唤醒holder释放，总wake locks=1（用户应用保留），
Venus error/sys_error/hfi_error/insts/vb2_bytes全0；无试验进程、挂载、图层或trace实例残留。
证据：out/qualcomm-full-stack-20261108/display-sync-discovery-v1/result.json；
display-sync-clock-v1及display-sync-clock-mask-v1/independent-clock-audit.json；
三轮各自independent-fence-audit.json、independent-fullscreen-audit.json和final-health.txt。
本组新增诊断源码与手册尚未提交推送。

## 2026-11-09：v10 正常默认已实测多 NV12、缩放、UI 遮挡和全设备扫描

qti-system-display-v10已部署并正常启动，SHA256
5a753828ea820a55a29b1480a890bfd08a17e8479f2938d2b1488b5d27b1d110；
boot f36892b1-b79f-4562-bf8c-62fa96ec64a0，144秒后首次SSH成功。
62显示文件/13链接/17HIDL核验通过，唯一SQ2QDbg回退5b9f7012…保持。
无密码用户disabled=true/showing=false，没有新增备份或清用户数据。

display-v18/composer-v16分别42/46TU构建通过。源码按真实VIG、blend stage和缩放能力
选择多个opaque NV12层，保留视频顺序并复用框架清洞；全设备合成时省略未重渲染的
client target，资源/真实atomic拒绝仍有GPU回退。新71个ASan/UBSan源码场景通过；
C数组类型与诊断SDK接口的实际编译失败已修正并保留失败记录。主机硬件边界为注入，
最终直显/缩放/清洞以下列实机证据验收，完整fence仍未据此完成。

主runner的16个图形/客户端单项exit及外层均0，含7图形、5解码、4物理窗口客户端。
150帧ByteBuffer/native ImageReader/mixed/DRC像素与PTS对FFmpeg参考逐帧精确；
paced mixed非空DQ输出交叠836ms、29次fd切换，不作吞吐/硬件同时执行或延迟证明。

两个独立视频进程各30帧，实际同时为DEVICE：NV12 plane41/FB135为320×240→160×120，
plane49/FB136为320×240→640×480，另有RGBA UBWC target plane89/FB113。
两NV12真实FB512×512、pitch512/512、offset0/262144；编号仅本轮观测。
左右LM各有六内容×五轮，前29帧各2扫描，撤窗后两侧原桌面均稳定656扫描。

RGB UI覆盖层遮挡中心视频左半部，再撤下、再覆盖：同一NV12末帧仍DEVICE，
硬件CRC出现A/B/A稳定87/90/90扫描，只有对应左LM变化，右LM保持。
这是该部分UI遮挡/清洞功能证据，不能推广为所有UI效果或严格DPU像素验收。

同boot另做全屏30帧：320×240原生NV12按9x/8x缩放到2880×1920，
HWC仅一个DEVICE视频、SDM仅SDE，实际KMS无RGB client target，已实测全设备省略路径。
1080连续CRC含六内容×五轮，前29帧各2扫描；撤窗后NV12释放，
右LM匹配原桌面，两LM随后稳定659扫描。左LM与开始时不同，原因未确定；
不写成完整CRC pair恢复，也不把这项小源缩放当成高分辨率解码验收。

合计5个物理窗口客户端150个output/release/callback；原有150帧原始像素检查另计。
最终仍同boot、六正常服务running，无试验进程/挂载/图层；holder均释放，
最终总wake locks=1（用户应用保留），Venus error/sys_error/hfi_error/insts/vb2_bytes全0。
完整逐帧fence/严格DPU像素、更广分辨率/物理DRC/格式、HDR/secure/videoUBWC、
性能/PM、KGSL/下游msm_vidc、真实QTI C2解码及跨设备仍未完成，fullHWC/fullstack=false。

证据：out/qualcomm-full-stack-20261108/qti-overlay-composition-v1/result.json、
independent-normal-audit.json、independent-composition-audit.json，
qti-overlay-fullscreen-v1/independent-fullscreen-audit.json及final-health.txt。
部署/启动：qti-system-display-deployment-v5。源码/文档尚未提交推送。

## 2026-11-09：v9 正常默认已验收首条原生 NV12 DEVICE 直显

默认SQ2Nat的qti-system-display-v9（SHA1125875d…）已正常启动，新boot
ce7df37a-84e1-4c94-b025-5e274d7af6e5；等待138秒后首次SSH成功。
62显示文件/13链接/17HIDL记录与运行服务hash匹配，唯一SQ2QDbg回退5b9f7012…保持。
无密码用户锁屏disabled=true/showing=false；没有新增备份或清理用户数据。

display-v17/composer-v15成对42/46TU构建，通过正确矩阵metadata及源码策略/资源分配，
将一个边缘opaque linear NV12 BT.601 limited 1:1层交给VIG；其他组合/资源失败/
TEST_ONLY失败保留GPU回退。71个ASan/UBSan场景覆盖实际源码方法及注入硬件边界；
主机注入不是实机TEST_ONLY。v8→v9仅5显示ELF+2校验文件变化，其余1295有效记录保持。

qti-overlay-v1直接使用正常启动服务，连续13阶段和外层exit0。7项图形回归保持；
五个解码阶段150帧像素/PTS对FFmpeg参考逐帧精确，mixed实际非空DQ输出区间交叠826ms、
19次fd切换，DRC320×240→1920×1080→320×240通过；不作吞吐/时延证明。

实际视频层已为DEVICE，SDM为SDE/VIG；同一msm/card0/crtc0上NV12 plane41/FB127，
真实FB512×512、pitch512/512、offset0/262144，裁剪320×240→[1280,840,1600,1080]，
BT.601 limited；RGBA UBWC client target为另一plane89/FB115。编号仅本次观测。
这是原生NV12物理平面，已区别于此前SF CLIENT合成视频；不靠更改Java窗口层级或关闭SF优化。
1080连续双LM CRC包含六内容×五轮；前29帧中index8仅1扫描，其余2扫描，末帧保持178扫描。
sample299撤窗后完整桌面CRC pair恢复并稳定781扫描；NV12 plane和视频层均已释放。

截图不作为DPU像素读回，render callback也不作为物理present；逐帧fence信号/复用、
严格像素、UI遮挡全面覆盖、缩放/多层、视频UBWC、HDR/secure及性能/PM仍未验收。
最终同boot、六正常服务running、实验holder释放且无残留试验进程/挂载；
总wake locks=1（保留用户应用），Venus error/sys_error/hfi_error/insts/vb2_bytes全0。
full_hwc_verified/full_stack_complete仍false，KGSL/下游msm_vidc与跨设备目标保持。
证据：out/qualcomm-full-stack-20261108/qti-overlay-v1/result.json、
independent-normal-audit.json、independent-overlay-audit.json和final-health.txt；
部署/启动见qti-system-display-deployment-v4。源码和文档尚未提交推送。

## 2026-11-09：v8 正常默认及13阶段连续回归通过

qti-system-display-v8 成对封包已部署并正常启动，278,606,849字节，
SHA256 f627e6835ab534e0a6fa2c55a768d7392a5d422898cb7c66a67df0c10bbdfd40；
append SHA b6fce1ede1832bd35f5e7bf69b71f14f6fff97b2bfff1ee5a0ba51bc1e462529。
service SHA7a6aa750…、bridge SHA282834a0…与v3/v4 RAM实测完全相同；新boot
f1a784c2-080a-4528-ba3c-388c62777ff3。HIDL17项、显示62文件/13链接及实际运行ELF hash
核验通过，原init身份和环境保持，唯一回退5b9f7012…未变。封包源只替换service/bridge/
SHA256SUMS，其他archive记录/metadata与凭据尾保持；10项封包测试及gzip-t通过。

normal-v3一次连续13phase和runner均exit0：七项图形（AHB/EGL/GLES1/Vulkan/
Vulkan-ImageReader/YUV/extension）、五解码（ByteBuffer/native Surface/mixed两路/DRC）
及实际物理窗口。150解码帧可见像素/PTS精确、三路ImageReader timestamp精确，
DRC320×240→1920×1080→320×240；mixed实际输出重叠808ms，仅为有节奏功能验证。
物理30output/release/callback、1080连续CRC通过；前29帧各2扫描、末帧177。
退窗sample300双LM恢复桌面仅稳定24扫描，之后LM0变化、LM1稳定780，不能混写。
截图模型max2/MAE0.077921，strict physical pixel仍false；callback不是物理present。

最终同boot/OMX PID5590、六服务running、Awake/wake0、Venus错误/insts/vb2_bytes0，
无试验进程或挂载。已验配对继续作为默认native OMX运行；完整HWC/full stack仍false。
构建证据qti-system-display-v8/{BUILD,HOST_VALIDATION}.json，部署证据
qti-system-display-deployment-v3/，独立验收qti-system-display-normal-v3/
{independent-normal-audit,independent-normal-summary,physical-independent-audit}.json及final-health.txt。
更多实际计数、布局、hash和剩余边界见HANDOFF最新节。私有产物和原始记录不上传。

## 历史阶段（2026-11-09，v8部署前）：逐节点 native OMX 候选 v3 构建及真实联合验收

候选service SHA 7a6aa750f3db494457088ac7045cb253312510332568b15c5310f7b1e08c8ce2
位于 out/qualcomm-full-stack-20261108/native-omx-node-v1/service-build-v3/。
同目录BUILD.json保存真实-Werror命令和输入hash；layout object SHA
7c85c630c003e1a47c258bcfc481cd478d04c506df1023be4539b621d3aa8bc1。
非宏正常service独立构建通过，SHA8bbfbcdb…，不强制添加逐节点bridge接口。
候选从RTLD_DEFAULT解析成对bridge节点API，导出usage-layout helper，新增依赖仅现有匹配
libgrallocutils；无NDK libc++命名空间混入，不依赖新的native-layout环境。

修改入口为 quest2-omx-service.cpp、quest2-vidc-node.h/bridge、native-layout helper及
quest2-omx-node-usage.h。实际使用的consumer wire、usage变换及visible/port/storage边界
测试通过ASan/UBSan；bridge10项和真实layout校验负例通过，未知bit/secure/UBWC仍拒绝。
tests/test_quest2_omx_node_usage.c不是完整HIDL状态机mock；状态机集成行为另由真实混合节点
实测验证，不声称覆盖所有状态组合。
v1/v2发现错误的visible=allocation height校验，v3按真实port保留padded ANW，未删除尺寸、
容量、usage或真实导入检查。构建/失败记录均保留。

native-omx-node-v3 在正常v7服务环境仅RAM替换OMX/service桥，五解码phase各30帧，
逐帧可见像素/PTS精确，native三路ImageReader时间戳精确，混合节点与流中DRC通过；
物理30帧/30callback、1080连续CRC及撤窗781扫描回桌面通过，外层exit0。
CRC的一帧仅1扫描；末帧模型最大差2/MAE0.077921，严格物理像素、fence及性能不据此完成。
独立审计位于 native-omx-node-v3/independent-decode-audit.json 与
capture/physical-independent-audit.json；详细布局/运行身份/归一hash见HANDOFF最新节。
原service/bridge已恢复、未写EFI或重启，默认仍v7软件输出；正式封包/重启验收待后续事实。

同一配对二进制的v4追加33333µs输入节奏混合试验，两路各30帧像素/PTS及native
ImageReader时间戳精确，EOS/stop/delete/exit0。真实非零CAPTURE DQBUF双fd交错，
输出区间重叠802ms（日志精度1ms），证明输出活动重叠，不作同时硬件执行或吞吐验收。

v4独立审计：out/qualcomm-full-stack-20261108/native-omx-node-v4/independent-mixed-audit.json。

### v8 配对封包构建记录（正常启动和回归已见最新节）

最小入口 scripts/pack-sq2-native-omx.py 以固定v7 hash为输入，仅替换
etc/sq2-hidl/quest2-omx-service、etc/sq2-hidl/lib64/libquest2-vidc-bridge.so与
etc/sq2-hidl/SHA256SUMS三项；
先核原17条清单、只更新两个二进制hash，其余archive记录/metadata及最后凭据成员保留。
原init/rc/env、视频选择器、mapper/显示闭包保持。新封包测试5项与原QTI封包5项共10通过，
gzip-t通过；这不是部署或启动验收。

qti-system-display-v8：278,606,849字节，initramfs SHA256
f627e6835ab534e0a6fa2c55a768d7392a5d422898cb7c66a67df0c10bbdfd40；
append SHA256 b6fce1ede1832bd35f5e7bf69b71f14f6fff97b2bfff1ee5a0ba51bc1e462529。
service SHA7a6aa750…、bridge SHA282834a0…与v3已验配对一致。v8已写入默认SQ2Nat并正常启动，新boot f1a784c2…；13阶段连续回归随后通过，详见最新节。

## 历史阶段（2026-11-09，native v3前）：36帧正常视频尾部通知区分

默认仍qti-system-display-v7/mapper5-v36，未再部署或重启。追加真实六帧GOP后，
原前30帧callback全部出现，新缺口移到32–35（32/36、严格harness仍exit1）。
1080连续CRC含六内容×六轮及撤窗后123扫描桌面；一帧占3扫描，不称完美cadence。
结果支持SoftwareRenderer后续dequeue驱动通知机制；NDK允许信息性callback缺失，
不是已证API违规或显示丢帧，亦非“回调已修”。源码/输入与实机证据见HANDOFF最新节。
本节记录当时正常服务、wake0/Awake及Venus健康；之后逐节点native usage/ANW/mixed/DRC
已完成RAM联合验收，见本文最新节。完整HWC、HDR/secure、PM及整栈目标仍未完成。

## 2026-11-09：正常显示包 v7 / mapper5 v36 已重启并完成分阶段回归

实际完整打包命令见 `out/qualcomm-full-stack-20261108/qti-system-display-v7/pack-command.json`；
mapper命令见 `mapper5/build-command-v36.json`，复现使用fresh输出目录与原固定私有输入。
配套display-v16/composer-v14、mapper5/v36和native-vulkan-system-v5，基包仍330728ef…；
前默认2664c067…已替换，唯一回退5b9f7012…保持。

| v7关键产物 | SHA256 |
| --- | --- |
| initramfs.cpio.gz（277,788,786字节） | 4fb449dbb165607d985088a8e3b4d9c8c7e79e9e9cef171af162626a30082251 |
| display.append.gz | 775dcd3dc58925c9be6390b0a3d049a93ef2b408073b64f4a339fa0fb7782386 |
| mapper.quest2.so | 8af79a070311b38bb0890a4ef0f6e96a186f923a83e7ed075dd199e5d5edbc0f |
| mapper.qti.so（extension companion） | 45d7a3b9efdc335f4bf95876f2f4a038e0fef7b76d2b4f25992d9f41538448ab |
| quest2-allocator5-service | f6fab31f35a27d7843b8fcc4e45de0b3fc0f0ce26bf9453d4c7d7140cb9172bf |

65输入/62payload/13链接/4HIDL支持文件hash核对通过；五项packer测试和真实extension源码
ASan/UBSan测试通过。实际stage覆盖video开/关与重入，保持原非init条目/凭据末成员。
新companion不直接链接mapper，而经真实sphal loader取得同registry；scalar metadata必须用
正式size query/encode/decode，不采用raw8假设。C2二进制仅移除固定libmapperutils依赖，
保留可执行节，companion放lib64，旧hw/mapper.qti仍清理；未改全局linker或compiled policy。

v7已正常启动为boot bcd26c8c…，62安装hash匹配。normal-v2九独立phase最终exit0；
原runner因诊断extension的vendor优先环境缺PermissionCache符号而exit1，改为
`LD_LIBRARY_PATH=/system/lib64:/vendor/lib64`后只续跑extension和两视频phase。
不能把这记录为一次连续外层exit0；正式服务运行与这次诊断启动错误分开记录。
135YUV、126标准lockPlanes、原AHB extension/ownership通过；默认OMX ByteBuffer和
SoftwareRenderer→ImageReader各30帧像素/PTS对重新FFmpeg精确。物理窗口两轮callback
26/30、27/30仍strictfail，虽各660连续CRC包含六内容×五轮全部30段；仍非native OMX零拷贝。
官方NDK r28c `media/NdkMediaCodec.h:510–512` 明确OnFrameRendered仅用于信息通知，
可显著延迟/批量送达，已render的帧也可能没有callback。因此26/30、27/30仍是现有harness
额外逐帧计数判据的失败，未被修复；单此结果不证明API违规或显示丢帧，也不构成继续推进
native OMX前必须重写Framework的理由。实际呈现、fence及性能仍按独立证据验收。
第二轮截图源模型比对76,800像素：最大通道差1、MAE0.055026；尚无独立量化容限，full_frame_pixel_pass=false。两轮CRC均未覆盖窗口退出，不能据其证明退出后回到桌面。

收尾另取final-desktop事后样本：正常launcher截图无视频窗口，60个连续CRC样本的59个
frame delta均1、CRC pair恒定，观察器恢复source=none。Wake Locks size=0，旧wake.pid进程
已退出，当前用户disabled=true/showing=false、Awake；Venus sys_error/insts/vb2_bytes均0，
boot仍bcd26c8c…。这份独立事后样本确认收尾桌面和资源状态，不证明退出瞬间的切换时序。
证据：`qti-system-display-normal-v2/final-desktop/{device.txt,crc.txt,sf.txt,desktop.png}`。
完整scope和正常验证证据见HANDOFF及 `qti-system-display-normal-v2/normal-independent-audit.json`。
BUILD/HOST_VALIDATION的构建时待验字段只表示当时状态，不替代正常启动记录。
整栈/HWC、HDR/secure、性能/PM/跨机和enforcing验收仍未完成，私有产物不分发。

## 历史构建阶段（2026-11-09，v5）：正常重启与生产路径回归

候选原hash2664c067…未变，SQ2Nat落盘hash/gzip/SHA256SUMS通过后已正常重启。
实际完整打包命令保存在`out/qualcomm-full-stack-20261108/qti-system-display-v5/pack-command.json`；
复现需改用全新output目录，原私有输入/凭据不作为公共分发内容。
新boot7bb4e664…、sys.boot_completed1，60安装文件与BUILD全匹配；真实正常init下QTI composer/
AIDL allocator/HIDL manager/SF和原Power HAL运行，服务身份和注册已核，唯一回退5b9f7012…保持。
`qti-system-display-normal-v1`的五阶段exit0：27AHB/exec、45EGL+54BufferQueue、GLES1 pbuffer
8帧265,224像素8fence、Vulkan计算/光栅3AHB和24ImageReader WSI/24fence通过。
因此生产路径和GLES1基本绘制不再是未验证候选；构建时BUILD/HOST_VALIDATION的待验字段属于当时状态，
实际运行以deployment-v1/first-boot-state.txt和normal-v1日志为准。原始日志/私有包不上传。
正常桌面物理KMS与默认视频本轮验证仍待补；开发permissive域、完整生命周期/HDR/secure/性能/PM等边界保持。

## 历史构建阶段（2026-11-09）：原生 Vulkan 联合通过与正常显示包 v5

实测system-v16沿用display-v16/composer-v14、mapper5/v33和native-video-window-v2，
新增native-vulkan-v6。计算/光栅、3AHB精确像素/重新导出、type0/4小/大GPU压力及ImageReader WSI
24帧795,672像素bad0/24native fence均通过，原图案/视频/空栈blank回归保持；完整scope见
`sm8350-composer-system-v16/{result.json,vulkan-independent-audit.json}`。不是Vulkan物理窗口或性能验收。

`scripts/build-quest2-native-vulkan.py --memory-policy`只加入原HOST_CACHED TLS策略HAL/helper，
恢复标准AHardwareBuffer和hw_get_module进口，移除旧synthetic-handle bridge依赖；外部导入/export不改cache。
`--system-layout`生成`native-vulkan-system-v5`，固定使用/vendor/lib64配置/库路径。
private版本实测v6与生产版本同策略源码；donor/TLS helper执行段同，policy编译路径不同不保证执行段逐字同。
v6仅修WSI探针consumer usage，保持producer GPU usage与mapper5 CPU锁校验。实际构建命令/输入SHA在BUILD.json，
private/system差异在`native-vulkan-system-v5/LAYOUT-COMPARISON.json`。

`scripts/pack-sq2-qti-system-display.py`现要求`--vulkan-build`及原base/display/composer/mapper/runtime-stage/
android-contract/output参数，fresh输出；配对真实空栈flush修复和全部native库hash，保留原内核/固件/凭据尾成员。
正常独立HIDL manager不依赖视频flag；只在旧video rc存在时移除共同manager段，保留OMX其余内容。
安装原GLES1实体及别名，删除三旧mapper实现和五已审计句柄适配器，逐ELF拒绝退役adapter的DT_NEEDED。
不改全局linker或compiled SELinux policy，只追加窄contexts；同名类型存在不替代enforcing权限验收。

| v5实际产物 | SHA256 |
| --- | --- |
| initramfs.cpio.gz | 2664c067b80c67629d7678d2b6acb9a3924e53861f46e6ef00b18bb66be8f8ac |
| lib64/hw/vulkan.freedreno.so（policy入口） | b6704dc13dd7336f6b6a8e1f17980f94e7289e35d57d385c1f36b18a3c11ea84 |
| lib64/vulkan.qti-native.so | 3d67ae4ef6cbb7339f1ddaa27cfed61931f82b5fdcd86767b5e8f765bbc95af6 |
| lib64/libquest2-native-vulkan-memory.so | 95fdf4fcb84ab93bb2f515cfcf2ce81fae7d81731be2b66ba061ea264aebbf0f |
| lib64/egl/libGLESv1_CM_adreno.so（原实体） | 396987f605b4f7aa1be5d10cacf0216bce400a3c13bb36f9cf29fac1d9bc5ace |

`qti-system-display-v5/BUILD.json`和`HOST_VALIDATION.json`记录60vendor文件、13alias、4HIDL支持文件；
四项正式测试及实际生成stage的video开/关×重复apply通过，owner唯一、无额外mount。
独立复核全部62输入、payload和输出hash；主机mount替身不代替真实启动。
TLS helper的ASan/UBSan测试覆盖512cache矩阵、嵌套和线程隔离，不宣称完整HAL生命周期主机测试。
正常生产路径、init/桌面/重启尚待验证，GLES1实绘也未验；候选BUILD保持deployment_ready=false。
原已验initramfs为330728ef…，当前写入默认为2664c067…，唯一回退5b9f7012…保持。
2026-11-08 19:29:33.969 UTC已将v5写入SQ2Nat，远端hash/gzip/SHA256SUMS一致；
已发出正常sys.powerctl reboot。唯一SQ2QDbg回退5b9f7012…保持，**当前为已部署、等待正常启动验收**；
不提前写成新用户态已生效。证据`qti-system-display-deployment-v1/deploy.txt`及`reboot-request.json`。


## 历史阶段（2026-11-09，system-v14）：display-v16/composer-v14 空栈清屏配套

配套 **sm8350-display-v16（42TU/五库）+ sm8350-composer-v14（46TU）**。
分别1476输入/10产物、1445输入/6产物的hash与ARM64经HOST_VALIDATION核对；九个定向测试
文件全部通过，包括实际源码的empty Flush负对照、fd所有权/错误重试及100转换。
完整命令在sm8350-reference-v1的display-build-command-v16.json、composer-build-command-v14.json；
复现仍用全新output目录。构建时“待system-v14/未部署”的清单是当时状态，实机结论在system-v14/result.json。

新增sm8350-dpu-flush-fence.patch与sm8350-hwc-empty-layer-flush.patch：真实unstage旧plane，
同次atomic返回retire/present；不创造buffer release、不过滤错误；command-mode原策略保持。
新native-video-window-v2共用原解码路径，并区分callback payload与callback实际到达时间。

| 实际产物 | SHA256 |
| --- | --- |
| display-v16/libsdmcore.so | 639a53a0e19a6572a0ebd38f7c963e2629742b0f801e83a6321183ff74320bcf |
| display-v16/libsdedrm.so | 24fcb737bb13f286ae6abbb5c75c24b46930dde4a786e6f39b17f77d30d94a97 |
| composer-v14/vendor.qti.hardware.display.composer-service | e6ae78e0cf7d27dbd852ae1268cd0fda85eeec77466e8b9fb8469573160c66cb |
| native-video-window-v2/libsq2-native-video-window.so | ce171acae3c34336b48154e170b2d667093f41d99c8ee592b31da85cee7145e0 |
| native-video-window-v2/sq2-native-video-window.jar | bb555bb617404173b94a2bc1365ac6d9428d234a7d8795194dbf05e9d5e7591b |

system-v14实测6次Flush成功/真实retire fd13，图案和视频撤窗各8plane FB0/null、CRTC保持active、
30/30连续CRC全黑；图案120CRC及视频30输出/定时release/APIcallback/EOS、420CRC完整序列保持。
新共享函数ImageReader30帧NV12/PTS精确回归，实际callback提前target49.5–66.4ms，不能作present。
图案四个中央区域1,382,400 RGB像素与源图精确，边缘shader未源匹配；末帧视频AOSP源契约
模型最大差2/MAE0.077921但量化界限待核。分别见pattern-pixel-check.json、video-source-contract-check.json；
callback-timing-audit.json记录30个通知全部早于target。
物理blank通过不替代完整release/present信号、复用/关闭/重新显示生命周期及默认用户态接入。
原默认服务恢复、EFI不变。原始证据/库仅本地，整体移植和默认替换仍未完成。

## 历史阶段（2026-11-09，system-v13）：控制内容与native window构建/证据

已验证显示配套为 **sm8350-display-v15 + sm8350-composer-v13**；后者 46 TU、1444 输入/6 产物
hash 与 ARM64 已由 HOST_VALIDATION 核对。完整命令为 sm8350-reference-v1/composer-build-command-v13.json，
继续用全新 output 目录复现。新增 sm8350-hwc-empty-layer-output.patch，修空层输出计数/旧状态；
实际源码负对照与 100 次空/非空转换通过。旧 v12 首帧记录保留为历史。

| 本轮实际产物 | SHA256 |
| --- | --- |
| composer-v13/vendor.qti.hardware.display.composer-service | 914095b18287d24dccc75ba1d5f7220539b2a801e772f4f18783616b3d3f7bc7 |
| native-video-allocator-trace-v1/quest2-allocator5-trace | b7f880954ecbb4a505e4777088ba3e6eb6e91fcd09f30ce9ab3d6c10a1f3d9bc |
| native-video-window-v1/libsq2-native-video-window.so | ab07cbd57e68462fac9f5b1e0c14bed94231d8546cb89f4c0c936b0c2b073be7 |
| native-video-window-v1/sq2-native-video-window.jar | 862683581a259a88fad738d3138ceef667cbd23dbbb9f7fab4673fab97b43f89 |

allocator 的 QUEST2_ALLOCATOR_TEST_TRACE 仅打印成功分配后的真实布局，默认构建无该输出。
原 NV12/mapper/v33 后端不变；native window 使用实际 SF width/height/layerStack，JNI 持有
ANativeWindow 至 codec 停止、注销 callback、释放；PTS 排序定时 release，不按消费者回调 pacing。
原解码循环保留 ImageReader 独立基线，native-video-window-v1/BUILD.json 与相关 BUILD 记录
实际命令/输入/产物；构建时 device_run=false 不替代 system-v13 实机记录。

system-v13/result.json 汇总真实图案/视频/CRC/恢复证据及 sha256：两种图案CRC各60/120、
截图四象限采样精确；AVC30 output/release/API callback/EOS、六种CRC顺序重复五遍。
NV12 实际经 SF CLIENT GPU 合成到 UBWC target，不是视频UBWC或YUV plane直显。
callback actual_ns==target_ns 不作物理 present 时间。独立 AOSP crop/transform 末帧模型全76800
像素最大差2、MAE0.077921，严格量化界限待核；详见 video-sampling-reference-v1/cpu-source-contract.json。
原始 pixel-check.json 的无crop ffmpeg参考不作颜色失败结论。

退窗残留仍为当前实证缺口。后继 display-v16/composer-v14 的真实 empty Flush/retire/error补丁，
由 tests/test_qti_dpu_empty_flush.py 提取真实源码：旧版三缺口负对照与100转换经ASan/UBSan通过；
host eventfd仅注入底层接口并验证fd透传/所有权，不冒充sync_file或物理fence。
system-v14 实机结果待核，不能仅据构建/host测试标为blank/release生命周期通过。
默认服务/EFI保持；整体移植与默认替换仍未完成。

## 历史阶段（2026-11-09，system-v12）：display-v15/composer-v12 完整服务真实首帧

当前配套为 sm8350-display-v15（42 TU、五库）和 sm8350-composer-v12（46 TU、完整服务及支持库）。
源码参考仍为 e9bbe9a980d408af36b52f2abaae7c35647b7425。完整命令分别保存为
out/qualcomm-full-stack-20261108/sm8350-reference-v1/display-build-command-v15.json、
composer-build-command-v12.json。复现使用全新输出目录，不覆盖现有构建/实机证据。

在原能力、属性、fence、mapper5、SDM、DRM lifecycle、eDP/events、可选 panel 与真实
validation target 基础上，保留 display-v14 的拒绝 op/object/result 和真实 Commit 日志，
加入 sm8350-dpu-histogram-capability.patch；composer 保留 system Binder、primary 清理及
sm8350-hwc-client-target-property.patch。histogram 的真实属性/版本缺失即不启用，
Linux DPPS 在入口返回 NotSupported；不更改 contract 为伪成功。
无 client-target 格式请求时输出 RGBA_8888/UNKNOWN，真实请求仍转换并传播错误。

    python3 scripts/build-sm8350-composer.py \
      --display-build out/qualcomm-full-stack-20261108/sm8350-display-v15 \
      --allocator-command out/qualcomm-full-stack-20261108/sm8350-reference-v1/hwc-allocator-build-command-v4.json \
      --dependencies out/qualcomm-full-stack-20261108/sm8350-composer-deps-v1 \
      --extra-platform out/qualcomm-full-stack-20261108/sm8350-composer-platform-v1 \
      --output FRESH_OUTPUT_DIR

BUILD.json 保存全部命令、输入与产物 hash；配套 HOST_VALIDATION.json 已核对 display
1475 输入/10 产物、composer 1443 输入/6 产物及 ARM64，实际源码的 histogram 能力/状态、
client-target 输出与负对照、optional panel、SDM contract/FB、session 初始化失败测试通过。
这些构建时清单的 deployed=false 不代表未做隔离运行，也不代表默认部署。

| 本轮实际文件 | SHA256 |
| --- | --- |
| sm8350-display-v15/libsdedrm.so | 24fcb737bb13f286ae6abbb5c75c24b46930dde4a786e6f39b17f77d30d94a97 |
| sm8350-display-v15/libsdmcore.so | 35b24f60432f4f680e22f358c1a598b9c0592476a43c0530be3593858c3ef8cb |
| sm8350-composer-v12/vendor.qti.hardware.display.composer-service | fd707780a79bfd1d7eb1025c5b1c616266ea527285d4c09b451e55c97811b8f3 |
| patches/sm8350-hwc-client-target-property.patch | 3a6f73c40fb58b036820c00d3e7b31b70176ed21ec19d38e4bda21b569f8a054 |
| patches/sm8350-dpu-histogram-capability.patch | 630fe16030000af3a66106411a702bdb5ddd65c61703198239b973e275018a0b |

system-v12 使用上述配套、mapper5/v33 和当前 caps-fence-ubwc-v1 内核。
私有 Binder 中运行真实原 Pixel libperfmgr Power HAL，匹配原二进制
31ed1725c9d235df30afbe5551324e3f9903a5d8ec4e7cb164c26d3ffec114e0
及 powerhint.json 45173af4f7f02037f9981b0056a3054e013647ebc0e978f2d0a58f53da70b991；
真实身份/链接/owner 与清理顺序见该轮 inner-composer.sh、outer.sh 和 power-* 记录。
私有 owner 与默认 owner 互斥，清理后先恢复原 Power HAL 再恢复桌面，不隐藏 VINTF 声明。

实测原 SF 的真实 UBWC target id15/fd26/format12 → Commit=0，
release fd29、retire/present fd28；plane89/FB118、QCOM_COMPRESSED、2880×1920@60、CRTC105 active。
这些资源编号只记观察，脚本仍按实际发现资源。qti-dpu-crc-observer 只启停 debugfs CRC、
不打开 DRM/不提交帧，120 样本的后119次 frame delta=1、27次变化，退出恢复 none。
原动画约5秒结束且CRC尾部为0；外层/helper均exit0，默认服务及健康恢复。

本轮 full_hwc_firstframe=true，但无独立像素对照、完整服务 fence 生命周期、原生视频物理窗口、
HDR/secure或性能/PM验收；动画退出后的空层栈错误仍待处理。控制图案/视频后续结果尚未计入。
原始证据只在本地 sm8350-composer-system-v12/{STAGE.json,experiment.json,logcat.txt,
drm-window-state.txt,hardware-crc.txt,sf-window-dump.txt,window.png,device.txt,exit}，不上传日志/私有库。
当前默认用户态仍为原 DRM HWC/minigbm 和 nocompression，仅此前 UBWC 显示内核已持久化；
默认 initramfs 330728ef…与唯一回退5b9f7012…保持，整体移植和默认替换未完成。

## 历史阶段（2026-11-09，system-v9）：首次 TEST_ONLY 与待补私有 Power HAL

当前完整 HWC 配套构建为 sm8350-display-v13 与 sm8350-composer-v10，仍为隔离实验。
display-v13 保留能力/属性/fence/mapper5/SDM、DRM 生命周期、eDP topology/events、
可选 panel 处理，加入首次真实 validation target 与分阶段诊断。
42 TU/五库完整命令为 sm8350-reference-v1/display-build-command-v13.json，
仍使用 --dpu-plane-properties、--dpu-capabilities、--dpu-output-fence、
--hwc-allocator-mapper5、--dpu-sdm-contract、--drm-device-lifecycle、
--dpu-display-topology、--dpu-events；validation target/phases 随配套构建应用，没有新增伪 CLI。

首次缺 SF client target 的 kernel-managed DPU GPUTarget 仅为 TEST_ONLY 分配真实临时缓冲。
描述/实际几何必须一致；原始 layer 不变，先清临时 FB 引用再释放 mapper buffer；
Commit 仍要求 SetClientTarget 后的真实句柄/flags/layout/color，不让临时 buffer 变成显示帧。
实际 v13/source 的 test_qti_dpu_validation_target.py 经 ASan/UBSan 通过。

composer-v10 依赖 display-v13 与 allocator-v4，46 TU、完整服务、四个支持库及 HWCLayer 探针。
实际 service main 在 QService/Core 初始化前选择 /dev/binder，标准 AHB 的真实 AIDL allocator
可达，HIDL RPC 接线保持；失败仍正确返回且不注册。旧 vndbinder QService 消费者未验收。
test_sm8350_hwc_session_init.py 用实际 v10/source 验证 Binder 选择和失败路径。
完整命令 sm8350-reference-v1/composer-build-command-v10.json；按全新 --output 复现：

    python3 scripts/build-sm8350-composer.py \
      --display-build out/qualcomm-full-stack-20261108/sm8350-display-v13 \
      --allocator-command out/qualcomm-full-stack-20261108/sm8350-reference-v1/hwc-allocator-build-command-v4.json \
      --dependencies out/qualcomm-full-stack-20261108/sm8350-composer-deps-v1 \
      --extra-platform out/qualcomm-full-stack-20261108/sm8350-composer-platform-v1 \
      --output FRESH_OUTPUT_DIR

两构建的 BUILD.json/HOST_VALIDATION.json 核对输入/产物 hash、ARM64、编译/链接与实际源码测试。
旧 DRM 名称、panel fatal、primary 非递归锁清理自死锁及 SDM runner 反向 patch 修复保持；
上述主机检查不代替真实系统服务/物理显示。

display-kernel-caps-fence-ubwc-v1 从已验证 v2 仅恢复原硬件格式检查下的 UBWC modifier 广告。
完整模块构建通过，module SHA 9ebaadd824d0b134a71700ef4fbc9d4727d2463f5355fc4682af63d68fd0ed6f。
scripts/pack-sq2-dpu-display.py 的完整参数见 package-dpu-display-ubwc-v1/pack-command.json，
仅替换 msm.ko 并保留有效 cpio 元数据及凭据末成员，产物为私有包。
该包已部署 SQ2Nat 并正常重启核对 build-id 1dacaf73eac94ebfacbe76f45a9429bc48e1a8f1；
initramfs 为 330728ef37f8b4be0103784049808b767d30a67124945b2bc59f4d0146a73ab2。
原 SQ2QDbg 唯一回退保持；真实 vendor.minigbm.debug=nocompression 保持默认分配线性。

composer-system-v9 已实测完成真实 AHB allocate/import/metadata/donor 几何/FB 注册/
首次 TEST_ONLY/FB 与 buffer 释放，done result=0；旧首次 validation/allocator Binder 阻塞已越过。
SF 主线程栈停在 PowerHalLoader::loadAidl → AServiceManager_waitForService，因私有 Binder
缺已声明的真实 IPower/default。下一步接入真实 Power HAL 后继续 first Commit，不改成假服务。
实际 KMS FB=0、CRTC inactive，完整 HWC 首帧仍 false；system-v9 exit=1，原显示服务恢复。
证据 sf-backtrace-window.txt、logcat.txt、power-service-default.txt、drm-window-state.txt。
后续 HAL 结果未验证前不提升状态，正常 DRM HWC/minigbm 未被持久替换。

独立 probe/graphics/qti-dpu-ubwc-scanout-probe.cpp 使用已匹配 display-v10 ABI 与 mapper5/v33，
不改原 qti-dpu-scanout-probe.cpp。精确编译参数、产物 hash、配套依赖与实验脚本在
sm8350-ubwc-scanout-v1/{BUILD.json,build-command.json,staging-manifest.json,inner.sh,outer.sh,run.sh}。
ARM64 -Werror、真实 GLSL shader 编译及三个脚本语法检查通过；同 boot 实机八帧已通过，
LINEAR/真实 GPU UBWC 的双 LM CRC 精确一致，结果与日志 SHA256 见 result.json。
CLI 参数为 debugfs 的 dri 根，内部按真实 DRM minor/CRTC 找 CRC，不固定 cardN/plane ID。
真实 AHB usage 0x1a00、RGBA8888 UBWC、GPU shader → donor base0/pitch → 双 VIG 扫描；
四缓冲 TEST_ONLY，L0/L1/U0/U1 重复两轮，逐帧比较双 LM CRC 与真实 fence，最后 disable/回收均过。
真实布局 payload offset=94208、KMS base offset=0、pitch=11520、allocation=22212608；
两组 CRC 为 b3f31b86/8adf2ef3 与 b4e51740/cd789617。仅 RGB UBWC 独立 atomic 验收。
先取得 DRM master 再初始化 EGL；不做 CPU 上传、伪线性 metadata 或显示前 CPU fence 等待。

inner 只运行私有 mapper5 manager/allocator 与 root probe；debugfs 私有挂载，服务/PID 有界。
正常 Binder 的 PARTIAL holder 必须在停止 zygote 前 READY，不能放到私有 Binder。
异常清理核验 exe/namespace，TERM 等待后必要时 KILL，确认退出后恢复默认 composer/SF/zygote；
等待 input/window 和 system_server 稳定，再按动态当前无密码用户 wake/dismiss，不改持久 PM。
失败时保存有界 display/GPU/IOMMU dmesg 前后差异及动态 DRM state；原始日志仅本地 out。
正常重启先等待 120 秒再 SSH。本轮 probe 后默认服务恢复、wake holder 释放及 Venus 健康已核对。

已验证基线仍是 scanout-v2 的双平面 XRGB dumb buffer 八帧/双 LM CRC/release/disable，
以及 allocator-v4 与 composer-v4/HWCLayer 各 120 缓冲/36 像素/FD11；
它们不替代完整 HWC、压缩显示、视频、HDR/secure 或性能验收。

## 2026-11-08：当前执行路线与源码 QTI 显示构建

执行路线已由用户确认为 mapper5 + 源码 QTI HWC/SDM + Linux 6.18 显示适配，
详细顺序、较大步幅的开发节奏与验收见 [开发方案](QUALCOMM-FULL-STACK-PORT.md) 的“当前执行方案”。
保留 mapper5/v33 与 sm8350-importer-v3 实测基线；不以重新实现 mapper4 作为 HWC 构建前置。

scripts/build-sm8350-display.py 使用显式 source/references/cc/drm-headers/drm-library/platform/output，
先核对 configs/sm8350-display-build.json 的源码与依赖，再在新目录构建五个配套显示库。
--dpu-plane-properties 在新源码副本应用零 fuzz 补丁并核对结果 hash，加入 alpha/blend/input fence
转换及请求失败传播；保留未打补丁模式用于对照。两者都不安装 HWC、不改默认服务。

已保存完整命令 out/qualcomm-full-stack-20261108/sm8350-reference-v1/display-build-command-v4.json。
复现时将 --output 改为全新目录，不能覆盖已有证据。BUILD.json 记录实际命令、输入/产物 hash。
验证命令：

    python3 -m unittest discover -s tests -p test_sm8350_display_build.py -v
    python3 -m unittest discover -s tests -p test_qti_dpu_plane_properties.py -v
    git diff --check

属性测试通过 DRM_HEADERS 指向 libdrm 源码头；当前默认指向既有工具链 libdrm-2.4.133。
qti-display-load-probe 只验证链接加载；qti-dpu-plane-properties-probe 动态枚举 msm primary，
拒绝 master 状态，只构造属性请求，不 commit。v4 五库加载与八平面 2080 次属性构造已过，
不能据此宣称 capabilities/输出 fence/完整 composer/物理显示已接通。本次文档更新没有重跑构建或实机。

## 2026-11-08：SM8350源码参考与composer导入类诊断

参考pins见configs/sm8350-display-reference.json，来源不是Quest原厂二进制的精确源码证明。
out/qualcomm-full-stack-20261108/sm8350-reference-v1/display-source为逐Git blob核对的完整304文件。
此节记录历史参考阶段；当前已确认 mapper5 + 源码 QTI HWC/SDM 主线，见本文顶部。
以下导入类是主线已有组件，但单独通过仍不代表完整 HWC。

scripts/build-sm8350-composer-importer.py显式输入source/output/cc与mapper/common/binder/core/hardware
头及platform库。先校验两份原始QtiComposerHandleImporter源码，再在全新输出应用
patches/sm8350-composer-mapper5-import.patch，零fuzz/结果hash核验，编译实际源类及诊断探针。
所有真实依赖头/源码/命令入BUILD.json。完整复现命令reference-v1/importer-build-command-v3.json。
python3 -m unittest discover -s tests -p test_sm8350_composer_importer.py -v

实机使用原mapper5/v33命名空间/allocator；新独立trial目录inner/run限定55秒，不停正常显示服务，
并按实际PID/exe/mount namespace清理。v3标准metadata须通过官方decoder解析，不能按裸uint64读取。
66缓冲/132导入/18保留像素已过；内部allocator/完整HWC尚未完成；
SDM源码五库现已构建，真实硬件初始化和物理显示仍未验收，详见本文顶部。

scripts/patch-sq2-qti-display.py EXPLICIT_DPU_PLANE_C只恢复真实硬件过滤下的UBWC广告。
python3 -m unittest discover -s tests -p test_sq2_qti_display.py -v
display-kernel-v1的完整模块命令/源diff/生成头审计保存在out；已编译但未部署。
quest2-sdedrm-probe及properties源仅诊断，现有精确命令hwc-sdm-v4/build-commands.json；
原厂在缺capabilities时确实崩溃，不以跳过初始化或虚构blob代替后续显示移植。

## 2026-11-08：mapper5/v33 RGB UBWC候选与能力枚举

build-command-v33.json复用原mapper5构建器全部固定输入；新增QtiGralloc-contract.h来源pin，
仅作为Compression_QtiUBWC命名参考，不编译其旧HIDL依赖。核心mapper/allocator/EGL probe与v32同hash。
quest2-rgb-layout.h验证实际RGB布局/metadata前缀/容量；测试
python3 -m unittest discover -s tests -p test_quest2_rgb_layout.py -v
直接以ASan/UBSan运行该validator，覆盖已采布局、错offset/size/格式与溢出边界。

quest2-ahb-egl5-probe --ubwc通过标准AHB分配四格式/两usage/三尺寸，GPU象限写入、
native fence、GPU采样到RGBA8后逐像素检查，拒绝CPU锁；默认不带参数仍跑原RGB/窗口回归。
quest2-mapper5-drm-probe --ubwc新增30缓冲诊断；默认仍21线性缓冲。
两模式都动态读取plane IN_FORMATS（blob边界检查），记录format+modifier实际组合。
压缩模式核对标准metadata，但KMS plane使用原厂GetBufferLayout的base offset/pitch；
不是通过把标准metadata改成伪线性而取得AddFB成功。探针不取得master、不做modeset。

v32/ubwc-test.sh与v33/regression-test.sh复用私有Binder/ODM命名空间及真实PID清理，
外层分别220/250秒、服务计时有界；run.sh写terminal exit。不修改全局服务。
v32验证24UBWC AHB/72GPU轮，v33验证27标准AHB/45GPU/45同步/54窗口/108YUV和两组DRM；
result-v33.json与BUILD/host-tests/final-health记录证据。当前plane仅linear，HWC/atomic未接通。
内核布局对照编译当前msm_media_info.h，源码与结果在v32/kernel-ubwc-layout.c/txt，
只证明布局公式一致，不证明DPU实际解压或FP16扫描输出。

## 2026-11-08：system-surface-v13框架原生Surface功能诊断

VIDC桥q2_set_format仅对显式native-layout的未使用CAPTURE槽位，在实际EBUSY后执行
真实释放/设置格式/恢复相同数量。bridge-empty-capture.c经实际dispatch验证错误、ownership、
默认模式及数量边界；python3 -m unittest discover -s tests -p test_quest2_vidc_bridge.py -v共9项通过。
桥独立构建命令在mapper5/system-surface-v5/build-command.json，不替换持久服务。

quest2-mapper-hal-namespace创建私有Binder时继承原节点uid/gid/mode；v9/build-command.json。
SF使用实际init UID1000/GID1003/groups1026,3009/SYS_NICE，避免原厂libgsl在root下选择primary；
不是改变GPU驱动探测或伪造DRM master。实验身份启动器/有界脚本仅在out对应版本目录。
仍为实验继承SELinux上下文，不能据此认定正式服务域通过。

sq2-mediacodec-frames.c对Surface逐帧验证请求allow-frame-drop=0并回读，收到上一张ImageReader
图像后再释放下一帧，等待上限沿用12秒；这是功能诊断，不是性能模式。
v13头由真实AVC样本重复5次、ffprobe包位置与显示顺序生成30帧及PTS，参考由FFmpeg独立解码；
build/fixture/result及源/产物hash在system-surface-v13。文件为I420，先按真实平面重组NV12再比较。
共享HEVC/HDR分支仅完成编译，本轮未做HDR实机回归。

system-surface-v1..v13的inner/outer/run保存有界服务harness及恢复流程。
vendor诊断DSO目录须进入LD_LIBRARY_PATH；启动动画还需Android环境及graphics身份。
v11真实窗口因UBWC usage失败；v12无消费节奏控制时30输出/25图失败，均保留原证据。
只把v13记为受控30帧/PTS/EOS/复用通过，未完成UBWC物理上屏/突发性能/正式部署，详见HANDOFF。

## 2026-11-08：mapper5/v30显示格式与DRM导入验证

共享QuestGralloc后端/mapper5增加RGBX8888和BGRA8888，保持真实component mask与字节顺序。
直接metadata/exec探针扩展为45缓冲，标准EGL探针在mapper5模式增加两格式，
旧mapper构建模式保持三格式；GPU共45轮，held-lock45轮，窗口仍原54帧。

build-quest2-mapper5.py新增可选--drm-headers，指向已有NDK sysroot/usr/include/drm，
构建quest2-mapper5-drm-probe；不安装服务。完整命令mapper5/build-command-v30.json，
源/头/产物hash见v30/BUILD.json。不要用本机不存在的/usr/include/libdrm路径。
DRM探针只使用ioctl，动态发现驱动名为msm的primary节点，不固定cardN，不取得master、不modeset。
标准AHB分配后用Stable-C元数据和实际pixel DMA-BUF导入；AddFB2/GETFB2/RMFB及GEM关闭均检查。

drm-v31仅更新该探针，基于同一v30 mapper/allocator编译；读取真实plane格式目录后区分
未广告格式的EINVAL与意外失败，保留18成功/3FP16拒绝/0意外失败三个计数。
RGBA10 FB成功但未被plane广告，不能当作支持直接扫描输出。命令/结果见drm-v31/BUILD.json。
完整GPU/YUV框架验收单独运行test-framework-v30-only.sh，不把v30 DRM失败隐藏为全部通过。
所有候选在新私有目录和挂载/Binder环境执行，生命周期清理及真实设备边界见HANDOFF。

## 2026-11-08：原生HIDL解码探针hidl-decode-v8

probe/video/quest2-omx-native-hidl-probe.cpp无参数仍验证导入生命周期；
传入全新帧目录时执行实际AVC输入、端口重配置、mapper5 AHB读回、EOS与完整释放。
断言和外层有界runner用于诊断，不能将此探针安装成产品服务。

编译使用原有HIDL生成头/平台libc++、mapper5-video-buffer对象，外加生成的sq2-h264-vector.h。
完整参数out/qualcomm-full-stack-20261108/mapper5/hidl-decode-v8/build-command.json；
输入/输出和源码hash见同目录BUILD.json，最终结果result.json。
头包含sq2_vec_stream、AU offsets、每AU的sq2_vec_pts和宽高/帧数。
v8使用已保存实际输入AVC：ffprobe -show_packets得到AU位置/长度，-show_frames的pkt_pos
映射到显示序号；每个AU的PTS按显示序号×33333生成，重复序列时累加时间基。
不能按H.264包的解码次序直接生成单调PTS，B帧会造成错误验收。具体映射保存在fixture.json。

服务和mapper沿用surface-v3/v27，test.sh复用私有命名空间及PID/inode清理。
30帧经独立FFmpeg对重复输入解码的NV12参考逐字节验证，6缓冲各复用5次；
不以同一探针自己生成像素期望。真实初始SOURCE_CHANGE通过，流中尺寸改变未覆盖。
没有改变正式build-quest2-hidl.py默认构建/部署路径，不开放native实验服务为默认。

## 2026-11-08：隔离原生HIDL服务候选surface-v3

正常quest2-omx-service默认保持软件输出；只有显式QUEST2_NATIVE_SURFACE_TEST构建
才打开preset ANW/NV12路径。隔离候选额外链接quest2-omx-native-layout.cpp并导出
quest2_omx_capture_layout，使用原厂gralloc公共几何；不链接libandroid/EGL进vendor服务。
精确命令保存在out/qualcomm-full-stack-20261108/mapper5/surface-v3/BUILD.json；
探针为probe-build-command-v3.json，普通构建为default-build-command.json。
最终源码/二进制hash见mapper5/result-surface-v3.json。

test-hidl-native-v3.sh在原有--mapper5 --egl隔离器内启动真实服务，
将候选绑定到/vendor/bin/hw/quest2-omx-service以保持正确linker namespace。
18个ANW/4个ashmem导入及状态生命周期通过，尚无HIDL解码帧。
普通构建6帧回归参考必须匹配实际ELF内嵌码流，不能复用其他样本的缓存NV12。
Surface测试仍因私有上下文缺SurfaceFlingerAIDL而阻塞，不能默认部署或声称原生Surface完成。
退出按实际PID、exe inode和mount namespace核对后清理，等待timeout monitor，不只kill monitor。

## 2026-11-08：mapper5/v27真实OMX解码候选

build-quest2-mapper5.py新增可选--omx-headers，需四个AIDL allocator输入同时提供；
使用configs/quest2-video-omx-headers.json固定OMX头，ARM64 -Werror编译原解码C探针、
QUEST2_NATIVE_MAPPER5模式capture与新AHB/metadata C++模块，链接系统libandroid/EGL/GLES。
同时构建现有VIDC桥，不链接旧mapper实现或直接原厂EGL前端。完整命令mapper5/build-command-v27.json。

框架回归扩展到108个YUV AHB（含VIDEO_DECODER usage），仍使用原有有界服务及真实PID清理。
真实OMX脚本mapper5/test-omx-v27-inner.sh复用同一私有服务生命周期，只为解码客户端预加载VIDC桥，
用固定6帧HEVC样本组成30帧320→1080→320序列；输入/期望/产物均有hash。
外层test-omx-v27.sh最长85秒，客户端55秒；不替换系统默认服务。
30帧可见像素SHA/PTS、GPU采样、DRC/复用/native fence/EOS/释放已通过，32项主机回归通过。
完整证据、旧模式语法检查与剩余系统Surface限制见HANDOFF；不能把此私有诊断当正式服务。

## 2026-11-08：mapper5/v26 NV12/P010候选

build-quest2-mapper5.py新增quest2-mapper5-yuv-probe，使用固定Stable-C/metadata头、
系统libandroid/EGL/GLESv3；完整参数mapper5/build-command-v26.json。
共享后端新增原厂GetYUVPlaneInfo公共函数ABI，并对aligned几何/容量进行验证与P010布局归一。
仅支持具体NV12/P010格式和even/progressive/linear布局；flexible格式及完整video usage待实现。

framework测试--egl在原RGB GPU/窗口回归成功后运行60秒有界YUV探针；
管理器/allocator计时分别150/140秒，外层v26脚本170秒，退出仍按真实PID与namespace清理。
YUV探针覆盖54 AHB分配、原厂GPU采样、独立exec导入，以及标准P010三分量lockPlanes。
test-framework-v26.sh先跑直接RGB metadata/dump回归，再进入私有Binder/EGL环境，全部退出0。
不能把CPU生成YUV图案的GPU验收当成OMX解码到Surface通过；完整结果/边界见HANDOFF。

## 2026-11-08：mapper5/v24同步与dump候选

最终命令mapper5/build-command-v24.json。mapper增加DMA-BUF flush/reread和metadata快照dump；
共享metadata输入要求regular/tmpfs backing，对应当前原厂ashmem_create_region的memfd实现。
EGL探针mapper5模式额外包含固定Stable-C头及平台native-window头、链接dl，通过sphal加载同一mapper，
独立import用于保持CPU lock期间的真实GPU同步验证；不改原AHB标准调用或旧模式功能。
官方头的C枚举名兼容仅用前向typedef，不复制结构体或改变ABI。

test-framework-v24.sh先运行有界直接mapper探针（27buffer/21metadata、dump重入/回调释放），
再运行私有Binder框架脚本，验证27标准AHB、27原GPU轮次、额外27 held-lock同步轮次及54窗口帧。
v24/BUILD.json包含源码/输入/产物hash，最终全部实机通过，边界及健康记录见HANDOFF。
不能将线性RGB同步验收扩展为YUV/压缩/系统视频Surface或物理显示已完成。

## 2026-11-08：mapper5/v20标准EGL与BufferQueue验证

scripts/build-quest2-mapper5.py从已校验allocator/v12复制原厂EGL/GLES/子驱动及qgl配置，
生成链接系统libandroid/libmediandk/libEGL/libGLESv3的quest2-ahb-egl5-probe。
复现参数见mapper5/build-command-v20.json，产物hash见v20/BUILD.json。
隔离器调用CANDIDATE SCRIPT --mapper5 --egl；原厂gralloc实体置于私有ODM hw目录内，
default别名和后端别名解析同一模块，满足设备libhardware的realpath目录约束。
不替换system libui，不加载旧mapper实现，不启动全局allocator候选。

framework脚本先运行allocator本地边界selftest，再启动独立Binder/default服务；
标准AHB27缓冲CPU/exec通过后运行GPU27轮与窗口54帧验证，实际PID清理规则保持。
allocator允许QTI UBWC hint且原厂判定线性的CPU请求；保持真实usage与压缩/protected拒绝，
不能把此兼容改动作为压缩布局支持。test-framework-v20.sh设备退出0，
原始记录与健康边界见HANDOFF。完整同步/YUV/系统视频与物理显示仍待实现。

## 2026-11-08：mapper5/v15未修改libui的标准框架验收

build-quest2-mapper5.py输出额外的quest2-mapper-hal-namespace、quest2-ahardwarebuffer5-probe、
quest2-mapper5-framework-test.sh与mapper.quest2.xml。固定公开QTI metadata结构头新增到
quest2-mapper5-reference.json，运行时对照原厂copy API校验共享前缀；原厂tail不按公开结构覆盖。

完整命令mapper5/build-command-v15.json。隔离器显式--mapper5后，在私有ODM加入native VINTF，
建立独立binderfs并检查设备号变化；脚本启动独立servicemanager与allocator/default。
服务库搜索顺序candidate/system/vendor确保现代AIDL使用system Binder；测试客户端清除LD变量。
系统libui不绑定补丁，原全局allocator/servicemanager不停止。test-framework-v15.sh执行27缓冲
AHB分配/CPU像素及socket/exec导入，全部通过；metadata的v8三主产物hash与v15相同。

退出按PID文件核对实际服务exe及mount namespace后停止服务，再等待超时monitor；v14旧脚本仅
kill monitor曾遗留私有服务，已清理，不可复制旧清理逻辑。v15最终健康快照证明无额外服务。
候选仍缺完整同步/YUV/动态HDR及GPU/系统显示接入，不可凭此默认部署，详见HANDOFF。

## 2026-11-08：mapper5色彩共享与并发验证候选v6

直接mapper5构建器额外链接原厂libqdMetaData，使用固定color_metadata.h及公开C接口声明。
新增quest2-color-metadata.h实现dataspace与静态HDR单位转换，不访问private_handle固定偏移。
固定引用更新configs/quest2-mapper5-reference.json，完整命令mapper5/build-command-v6.json。
产物仍只输出到新候选目录，builder不自动部署默认HAL。

quest2-allocator5-probe现验证16类metadata、27个远程分配RGB缓冲、27次exec色彩共享、
HDR独立清除/非法值拒绝/原厂单位读回、CPU像素及生命周期；另有基于eventfd的pending fence并发检查。
test-v6.sh以限时/quest2服务执行并清理manifest；v6/BUILD.json含精确头/库/源码hash。
该并发检查依赖root读取本线程wchan确认阻塞poll，属于诊断条件，不是产品运行门槛。
ARM64 -Werror及实机检查通过；真实GPU fence、全部现代接口和系统接入仍需完成，详见HANDOFF。

## 2026-11-08：直接 mapper5 与配套 AIDL allocator 候选

新增scripts/build-quest2-mapper5.py，显式输入已验父库、固定references、AIDL common生成头、
binder/hardware/core头、平台库、clang++及全新输出目录。参考pins为
configs/quest2-mapper5-reference.json；所有实际源码/头文件/链接库hash进入BUILD.json。
可选四个参数allocator-headers/common-headers/binder-platform-headers/allocator-platform必须配套；
启用后生成quest2-allocator5-service与quest2-allocator5-probe，否则只生成mapper.quest2.so和直接探针。

本地完整命令mapper5/build-command-v3.json，AIDL v2头生成命令aidl-generate.json；
固定冻结接口hash9499fec09c544e9de5be3c87125721600f8ade66。builder不自动注册或部署。
quest2-allocator5-service只接受显式default/quest2实例名或--selftest；测试使用临时/quest2，
不覆盖系统/default。test-v3.sh记录临时manifest、限时服务和核对后清理，未重启系统管理器。

实际27缓冲远程allocate2→mapper5独立导入→12类metadata→CPU读写/释放通过。
没有mapper3/4实现调用；原厂链接的旧接口库仍保留。完整mapper5、标准libui选择和GPU/视频/
物理显示尚未验收，未实现metadata和同步方法见HANDOFF，禁止当成正式HAL直接替换默认。

## 2026-11-08：可选 mapper4 导入/元数据验证候选

build-quest2-allocator.py新增四个配套可选参数：
--mapper4-headers（含固定gralloctypes/Gralloc4.h）、--aidl-headers（graphics common生成头）、
--binder-headers（平台NDK C++头）、--mapper4-platform（目标mapper4/libgralloctypes链接库）。
四项必须一起提供，Gralloc4.h按configs/quest2-mapper4-reference.json完整SHA验证；
其余实际输入hash记录于BUILD.json。复现命令见allocator/build-command-v15.json。

产物增加android.hardware.graphics.mapper@4.0-impl-quest2.so、quest2-mapper4-probe。
27个原厂allocator3缓冲的mapper4导入、12类只读metadata解码与按真实布局CPU读写通过。
当前仅导入/metadata实验，分配、flush/reread、可写/HDR metadata等未实现；不加入默认VINTF，
不替换v12的通用AHB/EGL候选。最终现代mapper接口仍须完整实现，边界及证据见HANDOFF。

## 2026-11-08：标准 EGL 与 BufferQueue 实机候选

build-quest2-allocator.py现额外生成quest2-ahb-egl-probe，链接系统libandroid/libmediandk/
libEGL/libGLESv3。复制父候选qgl_config.txt，并从EGL子驱动移除旧私有mapper依赖；
完整命令及产物hash见allocator/v12/BUILD.json，不从系统现有CPU上传包装层取得成功。

隔离器支持最后一个--egl参数；完整参数为候选目录、客户端绝对路径、--libui、
已核验libui兼容候选绝对路径、--egl。只在新MS_PRIVATE命名空间建立EGL目录与别名。
真实原厂allocator、临时VINTF及恢复流程仍见前节；私有测试命令保存于allocator/test-v12.sh。

v12实测27轮标准AHB/GPU三格式采样、GPU写回及native fence，通过同一缓冲读回；
另有54帧真实EGL窗口/ImageReader/BufferQueue，全部像素及显式呈现时间戳匹配。
每个窗口使用匹配EGLConfig的context，不能拿RGBA8 context复用到RGB10窗口。
证据及边界见HANDOFF：未全局部署，未验证SF/HWC/物理屏幕，未接通OMX原生Surface。
10项相关主机回归及-Werror编译通过；原始日志/二进制仍仅在out/private。

## 2026-11-08：标准 mapper HAL 与 libui/AHardwareBuffer 候选

scripts/build-quest2-allocator.py在验证父候选及原厂allocator pins后，额外构建：
android.hardware.graphics.mapper@3.0-impl-quest2.so（标准HIDL_FETCH_IMapper，无getService覆盖）、
quest2-allocator-hal-probe、quest2-ahardwarebuffer-probe与quest2-mapper-hal-namespace。
最后一个必须在Android根/挂载视图以root运行；先unshare/MS_PRIVATE再生成私有ODM。
第三参数--libui屏蔽本进程vendor/lib64/hw以测试正常选择后备；可选第四参数为绝对libui候选路径。
实验仍需真实注册的原厂allocator及临时VINTF声明；hwservicemanager缓存需要刷新并恢复OMX。

Android17目标libui拒绝API>35的mapper3后备。显式生成精确输入的兼容候选：

~~~sh
python3 scripts/prepare-quest2-libui-mapper3.py --libui ORIGINAL_TARGET_LIBUI --output FRESH_LIBUI_CANDIDATE
~~~

该工具核对完整SHA/指令上下文，仅解除mapper5/4均失败后的版本跳转，设备API保持。
不自动部署，不是通用版本补丁。候选仅bind到私有命名空间的/system/lib64/libui.so；
原系统文件、默认HAL和启动包保持。私有运行参数及hash见allocator/build-command-v8.json、
test-v8.sh和libui-compat-v1/BUILD.json。构建器复制的二进制仍只能保存在out/private。

v5标准HAL27缓冲通过；v6未改libui按真实版本限制失败；v7兼容libui的标准AHB27缓冲通过；
v8追加标准AHB socket传输、exec导入及生产者释放后消费者读回，27缓冲全部通过。
本轮10项相关主机测试通过。此处是标准用户态分配路径，尚非这些AHB的GPU/Surface/scanout验收。
持续系统服务、现代metadata与整个显示栈尚未完成，详见HANDOFF。

## 2026-11-08：原厂mapper/GPU独立候选

build-quest2-native-gpu-probe.py入口见[原厂共享内存验证](QUALCOMM-GRALLOC-MEMORY.md)。
真实RGBA8/RGB10A2/FP16共27轮GPU采样/写回/fence/exec导入通过；不是系统HAL部署。
HIDL源码获取现在固定Git内容树，避免将Gitiles重新编码的tar误认内容改变；
旧归档SHA保留作来源记录，非归档仍严格SHA256，固定commit/tree见quest2-gpu-research.json。

## 2026-11-08：整栈输入与独立原厂gralloc

新入口与实际验证见 [QUALCOMM-GRALLOC-MEMORY.md](QUALCOMM-GRALLOC-MEMORY.md)。
inspect-quest2-full-stack.py核验匹配vendor与固定源码参考，不能当部署批准器。
build-quest2-gralloc-probe.py生成新的私有目录，使用原厂gralloc1和共享system ION后端，
只用于独立进程；不得覆盖系统libion/allocator或把MSM GEM WC当成已移植KGSL。
VIDC构建清单现记录共用内存header hash，原持久服务未更新。

用户要求直接清理其他启动项已执行，现机仅保留SQ2QDbg当前包作回退，
菜单ID sq2-qcom-618-fallback，重启已验证。不要依历史章节恢复旧EFI入口；
源码基线/旧私有历史资料仍是移植参考，发布pins未被改写。

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

默认输入为 `private/build-inputs/kernel-6.18/EFI/SQ2Nat`。`package`逐字节复制当前EFI包并核对清单，
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

## 2026-11-08：原生视频共享布局候选

先对配套Venus源码副本运行 scripts/patch-sq2-venus-capture-layout.py，
按既有6.18三模块配套重建和pack-sq2-lts-venus-queue-lock.py打包。
不得热卸载/清sys_error；新候选保留SQ2QDbg唯一已验证回退。
新增S_FMT布局只在SOURCE_CHANGE后的CAPTURE_SETUP、队列未分配时允许；
宽度和格式保持编码输出，height表示分配scanlines，COMPOSE保持真实crop。
对齐约束与容量必须同时匹配固件和原厂gralloc，不能只修改用户态metadata。

    CC=cc python3 -m unittest discover -s tests -p test_sq2_venus_capture_layout.py -v
    python3 tests/test_sq2_v4l2_runner.py

build-quest2-native-gpu-probe.py可加 --capture-vector 已生成的HEVC Main/NV12或Main10/P010六帧目录，
核验vector.json/clip.hevc/ref.nv12或ref.p010，并记录向量头和探针源码hash。
quest2-native-video-probe --native-gralloc /dev/videoN使用SQ2_NATIVE_GRALLOC绝对路径、
原厂mapper的进程私有LD_PRELOAD/LD_LIBRARY_PATH。先核对健康状态，独立限时；
不能将其安装为系统服务或据此去掉OMX的SoftwareRenderer要求。
严格CRC、同dma-buf身份、GPU浮点结果、native fence、EOS/关闭都需通过。

HFI v4平面对齐属性与v6共用封装，实际源码函数验证：
python3 tests/test_sq2_venus_capture_packet.py MATCHED_PATCHED_SOURCE
该脚本编译实际v4/v6函数与配套结构体，ASan/UBSan核对线格式/容量和非法plane数。
当前已部署并通过native-gpu-v15(NV12)/v16(P010)的真实VPU→原生buffer→GPU路径，
候选kernel来源venus-capture-layout-v2；完整边界和未验项目见HANDOFF最新节。

## 2026-11-08：原厂 OMX 原生缓冲与 GPU 帧验证

scripts/build-quest2-native-gpu-probe.py新增可选 --omx-headers PINNED_OMX_INCLUDE，
按configs/quest2-video-omx-headers.json验SHA；同时构建quest2-omx-native-import、
quest2-omx-native-decode和当前libquest2-vidc-bridge.so，记录实际源/header/工具命令。
不替换系统服务，--capture-vector仍为独立V4L2探针的六帧向量输入。

原生解码显式用 --native-buffers --component OMX.qcom.video.decoder.hevc，
其余 --library/--input/--aus/--output FRESH_DIR/--width/--height沿用原客户端。
进程环境QUEST2_VIDC_BRIDGE=1、QUEST2_VIDC_AUX_CAPS=1、QUEST2_VIDC_NATIVE_LAYOUT=1，
SQ2_NATIVE_GRALLOC指向此候选gralloc.kona.so，LD_PRELOAD依次为该候选bridge和native-mapper；
LD_LIBRARY_PATH依次为候选、原厂OMX lib64、vendor/lib64、system/lib64。
必须用匹配capture-layout-v2内核；初次和每次端口重建均按原厂契约设置OutputPortDefinition。
禁止把private helper导出或进程环境直接装成多实例系统服务。

native-gpu-v23/BUILD.json和omx-native/decode-v23.sh保存最终可复现命令；
验证流为原有320×240六帧HEVC Main重复三次，加新1920×1080六帧，再加320×240六帧，
AU由各vector.json的offset拆分，PTS统一递增；逐帧参考来自各自ref.nv12。
1080向量用 scripts/build-sq2-hevc-test-vector.py --size 1920x1080 --out FRESH_DIR生成。
不得把拼接测试当长视频、吞吐或物理屏幕呈现验收；实测边界见HANDOFF最新节。

    CC=cc python3 -m unittest discover -s tests -p test_quest2_omx_decode.py -v
    CC=cc python3 -m unittest discover -s tests -p test_quest2_vidc_bridge.py -v
    CC=cc python3 -m unittest discover -s tests -p test_quest2_video_probe.py -v
    python3 -m unittest discover -s tests -p test_build_quest2_video_probe.py -v
    python3 -m unittest discover -s tests -p test_build_quest2_native_gpu_probe.py -v

五组48项通过，新的buffer读回/布局协商夹具编译实际C并带ASan/UBSan。
默认AllocateBuffer不使用新增native参数/回调；v22真实六帧像素回归通过。

## 2026-11-08：原厂 allocator 服务候选

scripts/fetch-quest2-mapper-sources.py及generate-quest2-mapper-headers.sh新增allocator@3.0，
固定AOSP commit/Git tree校验，29项接口输入。先构建当前native GPU候选，再运行：

    python3 scripts/build-quest2-allocator.py --native VERIFIED_NATIVE_DIR \
      --vendor-root PRIVATE_DONOR_VENDOR --cc ANDROID_NDK_CLANG --hidl PREPARED_HIDL_CACHE \
      --platform ANDROID_PLATFORM_LIB64 --allocator-platform MATCHED_ALLOCATOR_LINK_LIB_DIR \
      --output FRESH_OUTPUT

allocator-platform提供实际目标android.hardware.graphics.allocator@3.0.so，仅用作链接，记录SHA；
不混入本地宿主库。builder验证完整native父产物清单及原厂allocator服务/接口库pins，
构建IPC client并记录命令、源码/头、输入/产物hash。不部署、不自动改默认HAL。
最终复现命令在out/qualcomm-full-stack-20261108/allocator/build-command-v4.json。

configs/quest2-native-allocator.xml是临时HIDL声明，不覆盖现有AIDL allocator声明。
hwservicemanager会缓存VINTF；运行中添加不能当已注册，需真实interfaceChain/isRemote/PID/分配验证。
当前测试以临时root进程运行，未做正式服务SELinux/init和系统mapper选择。恢复后必须删除新增声明、
确认实验服务退出并重新注册原sq2-omx；本轮实际恢复结果见HANDOFF。
不要直接复制donor rc中的onrestart restart surfaceflinger作为实验注册流程。

    python3 -m unittest discover -s tests -p test_quest2_native_rgb_stride.py -v
    python3 -m unittest discover -s tests -p test_build_quest2_allocator.py -v

native GPU builder同时纳入quest2-native-mapper-compat.h/quest2-native-rgb-stride.h的源hash。
兼容层只修正已知RGB行距单位，不能把它当P010 plane metadata、HDR或系统mapper5移植完成。
