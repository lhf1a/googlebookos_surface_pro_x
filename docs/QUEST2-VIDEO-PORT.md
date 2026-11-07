# Quest 2 原厂视频工作流移植到 6.18

更新：2026-11-07。用户要求改走提取的 Quest 2 解码器工作流，替代完全自写的解码流程。保留现有 C2 作恢复入口；新增代码只负责标准 OMX 调用、原库装载及必要 ABI 适配，不重写视频解码器。

## 续接：真实 PM 状态、兼容候选与一次 Android 重启

只读单次 kprobe 读取了实际 `hfi_session_create` 的 core 软件字段，随后卸载观察模块：
`sys_error=1 phase=2 pm_error=-110 stops=9 starts=8 priv=0 owner=0 dma_stopped=1`。
因此这次失效发生在第九次恢复，不能把后来 suspend 回调复述的 -110 当成最初 stop 失败。
尚未确定恢复流程中具体哪一步超时；没有热卸载 Venus、写寄存器、清 sys_error 或重复启动 PAS。

已完成三层独立源码修正，最终配套三模块为 candidate3：
- `sq2_system_pm_suspend` 仅把“未停机且 IRQ 已恢复”的 busy token 恢复为 RUNNING；普通失败仍隔离。
  `hfi_session_create` 阻止 STOPPING/STARTING 期间新会话；只读 `sq2_pm_status` 记录实际恢复阶段。
- `MIN_BUFFERS_FOR_OUTPUT` 从已初始化 HFI 会话读取真实需求，未初始化返回 EAGAIN；不填固定数量。
  进程局部桥可在显式 AUX_CAPS 下使用实际格式、独立 REQBUFS 会话查询，不能把请求 count 当结果。
- `vdec_close/vdec_session_release/vdec_stop_streaming` 保持成功 get/put 配对；失败后不发 HFI，
  不确认的 INIT/END scope 在释放实例前关闭新准入，防止 hash32_ptr ID 复用；锁外排空回调后退役缓冲。
  candidate2 在独立审查发现异常路径缺口后中止上传，未激活、未启动；只使用修正后的 candidate3。

最终私有 initramfs：205514370 bytes，SHA256 `525d739f0e1d461aab686296d21262cb3e501e3ff591aec69ae0f0d043052c48`。
core `65c1198a…`、decoder `f1ce7d33…`、encoder `e8bb96d4…`，均为 6.18.60-sq2-android1 ABI。
所有有效模块副本配套更新，depmod 无 stderr，凭据尾层和其他文件保持；Image、DT、固件、GPU 与回退不变。
ESP 空间整理仅移除逐字节回读并保留于主机的旧备份/编译暂存，当前原 5b3a70c3… 包仍留 ESP。
部署完整回读、gzip 与校验清单通过。2026-11-07 03:48:48 UTC 只发送一次 Android `reboot`；
SSH 随后断开。新 boot ID、桌面、实际解码尚待回连读取，不能以命令已发声称启动成功。

原厂客户端 `quest2-omx-decode.c` 实编 ARM64，支持标准 OMX 状态、真实 ETB/FTB、动态输出重配与 EOS；
原 High+B 六帧压缩向量/独立 NV12 参考复用，不造帧。进程局部 VIDC 桥 stage2b 已上传；stage3 已构建，
单平面 offset0 的真实 USERPTR fd→DMABUF pin/归还通过主机检查。七个事件订阅、非阻塞 poll 与并发 fd 关闭
已有模型/真实 fd 测试，但 source-change 完整元数据、活动 flush、多平面/offset 等仍明确不支持，不伪报成功。
35 项 Quest 测试、11 个实际关闭函数抽取场景通过；这些不是硬件帧或应用黑屏修复证明。

稳定四角色继续原路径，ROLLBACK 增 `--quest2-pm-initramfs TARGET_COPY`；独立副本实恢复 5b3a70c3… 原字节，
binary patch 重建最终候选一致。PM busy 与安全关闭函数均保留 BASELINE/MODIFIED/ROLLBACK 原输出。
下一步只续接这次启动和原厂真实调用，不重复部署/重启。6.18 标准发布 pins 仍保持原验收包，未晋升候选。

## 前序用户态阶段已经执行

- 从原固定 v77 范围恢复 vendor：131 个操作及最终 `44f90742c03c04037e378860ad493098a369d2bc752aa0d56d14f63ae91b6722` 哈希通过；906 个文件只读提取，54 个视频文件吻合原清单。第三方 OTA 的 OEM 签名未验证，不刷写该分区。
- 当前设备只有 `arm64-v8a`，没有 32 位 ABI。先移植 64 位 `libOmxCore.so`/`libOmxVdec.so` 原厂 OMX，而非强装其 32 位 C2/OMX 服务。`QC2Codec_GetPlugin` 是私有接口，不按未知 C++ 对象布局调用。
- 私有候选包含 7 个原字节 donor 库，平台依赖沿用当前设备；不复制 Quest VPU 固件、不切换 GPU、不改变全局 gralloc 或 MediaCodec 注册。
- 真机原厂 `OMX_Init=0`，枚举 18 项后 `OMX_ErrorNoMore`，`OMX_Deinit=0`。maps 证实装入了私有目录中的原库。只证明装载/枚举，不是输出帧。
- 原样 `OMX_GetHandle("OMX.qcom.video.decoder.avc")` 返回 `0x80001000`，日志为 fd=-1/errno2。
- 进程局部适配把原厂 `/dev/video32` 指向运行时发现的 `qcom-venus-decoder` 节点；所有 ioctl 原样转发、不模拟成功。实际仍在 open 阶段返回 `-1/errno5(EIO)`，尚未执行 VIDC 私有事件 ioctl。
- 不经过 donor 的系统 shell 直接打开同一个 `/dev/video0`，同样 `I/O error`。因此当前共享 Venus 后端首先阻断了新旧用户态，不能把本次实际失败归因为尚未执行的私有事件，也不能认定只换前端已修复游戏。
- 当前源 `hfi_session_create()` 在 `core->sys_error` 时返回 `-EIO`；这是与观测相符的源码路径，不是已经直接读取该字段或证明此前 PM 超时的全部因果。

《星塔旅人》`com.RoamingStar.StellaSora` 此前确有三次 AVC STARTED 错误（1920×1080、2400×1700，错误码 -2147483648）。HEVC 的另一组记录属于网易 UU 远程，不能混算游戏证据。

## 可复用入口

```sh
python3 scripts/build-quest2-video-probe.py --cc NDK_CLANG \
  --headers AOSP_OMX_HEADERS --output FRESH_BUILD
# 显式编译真实 OMX 客户端与 ABI 桥；不启用或安装任何服务：
python3 scripts/build-quest2-video-probe.py --cc NDK_CLANG \
  --headers AOSP_OMX_HEADERS --output FRESH_FULL_BUILD \
  --with-decode-client --with-vidc-bridge
python3 scripts/prepare-quest2-video-runtime.py --reference EXTRACTED_VENDOR_ROOT \
  --probe FRESH_BUILD/quest2-omx-probe --output FRESH_PRIVATE_RUNTIME
```

头文件来自固定 AOSP `android-12.0.0_r1`，九个哈希在 `configs/quest2-video-omx-headers.json`。`--reference` 目录包含原提取器的 `inventory.json` 和 `vendor/`。准备器检查视频原库与依赖哈希，原始二进制不修改，产物仅限私有本地使用。

运行时 `run.sh --enum` 仅枚举，显式 `--get-handle COMPONENT` 才请求组件；没有默认送帧、注册服务、刷写或重启。可选 `libquest2-io-trace.so` 只观察原始 open/ioctl，`QUEST2_VIDEO_NODE` 是本次动态发现结果，不是产品固定编号。ioctl 返回值及 errno 保留，不映射 `/dev/ion` 或安全内存。

新入口已真实编译四件，原默认两件输出哈希保持；客户端重建 hash=c62f0ad8…、stage3桥=f8bc49d1…，
与先前冻结版本一致。另5项builder测试通过；旧入口同flags退出2→修改件实编四件退出0→独立回滚恢复退出2和原hash。
Android集成仍待原厂帧验证：Quest现成服务是32位，AOSP16参数wire ABI与64位原库存在结构布局差异；
不直接把32位服务改成64位冒充可用。优先研究保留现有64位C2外壳、后端薄接标准OMX，而不是重写原厂解码流程；
完整32位运行时仍是备选，当前未验证。私有只读依据在android-integration-readonly/FINDINGS.json。

## 下一步真实兼容工作

首先恢复并核验共享 Venus 后端为何已拒绝 open；禁止为探测盲目热卸载 provider 或重复启动 PAS。随后沿原厂 OMX 的 Loaded→Idle→Executing、真实帧、时间戳、EOS/释放继续，而不是退回自写解码状态机。

原始二进制静态核验还揭示以下后续 ABI 差异（**本轮尚未动态执行到**）：

- 首次订阅私有事件 `0x08001001`，总共七种；当前 Venus 只支持标准 EOS/SOURCE_CHANGE/CTRL。
- `MIN_BUFFERS_FOR_OUTPUT(0x00980928)` 与下游 conceal 控制。
- USERPTR + `plane.reserved[]` fd/offset 与当前 DMABUF/MMAP 的差异。
- UBWC FourCC/多平面、extradata 和私有 gralloc 元数据；不能只改格式名称。

兼容层必须实现真实事件/队列和缓冲寿命语义，不能靠忽略 ioctl、伪报缓冲数量或把安全分配降级来制造成功。先以非安全 AVC、线性 NV12 完成像素验证，再接 Android Surface/应用；原厂 XML 不是本机能力验收。

## 回滚与证据

仅替换独立目录中的 `selected-backend.sh` 副本。相同 `--names-test`：BASELINE 为现有 C2 的 4 codec/8 name，MODIFIED 为原厂 OMX 18 项，ROLLBACK 恢复原 C2 输出，三次 exit0；恢复文件 SHA 与原件相同。稳定 `ROLLBACK.sh` 新增 `--quest2-video-launcher TARGET_COPY`，已在设备独立副本执行并重新应用候选。现用 service/start.sh 哈希、boot ID、C2/system_server/SurfaceFlinger PID 始终不变。

此前仅用户态阶段没有替换系统默认解码器、没有实际输出帧、没有修复游戏黑屏、没有重启；后续部署与重启状态以上方续接记录为准。6.18 唯一标准编译基线和原验收包保持不变。

私有证据：`out/quest2-video-618-20261107/`（原厂库、命令、stdout/stderr、候选及回滚）；ABI 反汇编索引：`out/quest2-abi-readonly-20261107/READONLY_EVIDENCE.json`。原四角色继续在 `out/kernel-6.18-migration-20261105/`，仅追加记录。

公开接口参考：[AOSP OMX Core](https://android.googlesource.com/platform/hardware/qcom/sm8150/media/+/748f3f05bde7a465ace3eae7d8fbef3087032070/mm-core/src/common/qc_omx_core.c)、[AOSP SM8150 OMX 实现](https://android.googlesource.com/platform/hardware/qcom/sm8150/media/+/748f3f05bde7a465ace3eae7d8fbef3087032070/mm-video-v4l2/vidc/vdec/src/omx_vdec_v4l2.cpp)。以本地固定 Quest 二进制和现场调用为准，不把参考实现所有分支当作已执行事实。
