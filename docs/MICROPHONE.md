# SQ2 内置麦克风

2026-10-28：实机在线录音已验证；启动持久性另行记录在 deployment-status.json。

## 路由与策略

WCD9340 DMIC0/DMIC1 → DEC0/DEC1 → AIF1 CAP SLIM TX0/TX1 → MultiMedia2，ALSA card 0 / device 1。`audio-microphone-route` 使用上游 Surface Pro X UCM v6 的控制映射，DEC 原始增益 96（相对 84 为 +12 dB），不修改扬声器增益。路由配置本身不启动录音。

Android `primary_audio_policy_sq2.xml` 发布 `Built-In Mic`，原生格式为 48 kHz、S16、双声道；普通应用请求 16 kHz 单声道时由 AudioFlinger 转换。没有发布尚未实现的 offload、MMAP、电话或 FM 路径。

## HAL 地址修正

Cuttlefish donor 16373615 原 HAL 的 `XsdcConversion.cpp::createAudioDevice` 把内置麦克风 XML 地址无条件覆盖为 `bottom`。`StreamPrimary::getCardAndDeviceId` 无法解析这个地址，退回 card0/dev0，无法连接本机 card0/dev1 的捕获通道。

`patch-sq2-microphone-hal.py` 只接受原始 SHA256 `fd219b1058eccc701de685cf59d6578e887c11182d877bb4e6c3a78bfd73e279`，校验 ELF 段和指令上下文，在 VA/file offset `0xb6120` 将 `cbnz x8, 0xb6260` 改为 `b 0xb6260`。仅跳过 IN_MICROPHONE 的地址覆盖，保留配置中的 `CARD_0_DEV_1`。结果 SHA256 为 `774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea`。

这是限定二进制和 SQ2 策略的临时适配，不是通用补丁；从源重建时应改为只对没有配置地址的麦克风填默认地址。播放/捕获的传输、模拟器 pacing 和 MonoPipe 指令均保持原版，不叠加先前未验证的播放二进制实验。

参考源码：[XsdcConversion.cpp](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/audio/aidl/default/XsdcConversion.cpp)、[StreamPrimary.cpp](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/audio/aidl/default/primary/StreamPrimary.cpp)。原始驱动/固件不在仓库重新分发。

## 构建与验证

在 WSL root 中运行 `bash scripts/build-sq2-mic-app.sh`，依赖现有 Android 35 SDK、build-tools、JDK，生成 `out/sensors-mic-camera/app/sq2-mic-test.apk`。签名密钥留在忽略的 `private/`。

应用通过标准 AudioRecord 录制 10 秒，用户点击后才采集、离开页面终止；录音保留在应用私有目录，无网络权限。支持 48 kHz 双声道和 16 kHz 单声道，回放不额外放大。初版逐样本落盘很慢，已改为缓冲写入，并将采集计时与落盘计时分开。

- 底层 ALSA：48 kHz 双声道 483328 帧 / 10.069 秒，两通道有非零信号，无削波。
- Android：HAL 实际进程哈希匹配，设备地址正确为 CARD_0_DEV_1；48 kHz 录制取得 480000 帧，峰值 2016，无削波。
- 用户确认两种格式录音、回放以及原诊断应用均正常。
- 这些是有限录音测试，不代表回声消除、波束成形、长时间通话、蓝牙麦克风或所有隐私设置的认证。

`pack-sq2-microphone.py BASE HAL OUTPUT` 只接受资源监控基线 `d519b958…` 和上述 HAL，修改音频策略、音频启动助手、麦克风路由及 HAL 四项。逐项审计其他文件不变，保留本地 SSH 凭据 addon 为最后成员。启动助手显式标注执行文件 SELinux 类型、重启并核对实际 HAL 进程，消除旧版“挂载文件与实际运行 inode 不同”的竞态。

采用一次性 GRUB 测试项验证后才提为默认。备份基线、带凭据镜像、原始录音和日志均保留在忽略的本地目录；不要提交 Git。
