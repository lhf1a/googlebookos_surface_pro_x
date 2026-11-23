# SQ2 内置麦克风

## 路由与策略

音频路径：WCD9340 DMIC0/DMIC1 → DEC0/DEC1 → AIF1 CAP SLIM TX0/TX1 → MultiMedia2，ALSA card 0 / device 1。`audio-microphone-route` 使用上游 Surface Pro X UCM v6 的控制映射，DEC 原始增益为 96（相对 84 为 +12 dB），不修改扬声器增益。

Android `primary_audio_policy_sq2.xml` 发布 `Built-In Mic`，原生格式为 48 kHz、S16、双声道；应用请求 16 kHz 单声道时由 AudioFlinger 转换。

## HAL 地址修正

Cuttlefish donor 16373615 原 HAL 的 `XsdcConversion.cpp::createAudioDevice` 会将内置麦克风 XML 地址无条件覆盖为 `bottom`，导致 `StreamPrimary::getCardAndDeviceId` 退回 `card0/dev0` 而无法连接 `card0/dev1`。

`patch-sq2-microphone-hal.py` 仅接受原始 SHA256 `fd219b1058eccc701de685cf59d6578e887c11182d877bb4e6c3a78bfd73e279`，校验 ELF 段与指令上下文后，在 VA/file offset `0xb6120` 将 `cbnz x8, 0xb6260` 改为 `b 0xb6260`。该修改仅跳过 `IN_MICROPHONE` 的地址覆盖，保留配置中的 `CARD_0_DEV_1`，输出 SHA256 为 `774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea`。从源码重建时，应改为仅对未配置地址的麦克风填充默认地址。

参考源码：[XsdcConversion.cpp](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/audio/aidl/default/XsdcConversion.cpp)、[StreamPrimary.cpp](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/audio/aidl/default/primary/StreamPrimary.cpp)。

## 构建与打包

在 WSL root 中运行 `bash scripts/build-sq2-mic-app.sh`（依赖 Android 35 SDK、build-tools、JDK）生成 `out/sensors-mic-camera/app/sq2-mic-test.apk`，签名密钥保留在 `private/`。

`pack-sq2-microphone.py BASE HAL OUTPUT` 将音频策略、音频启动助手、麦克风路由与修补后的 HAL 打包进基础镜像，并将本地 SSH 凭据 addon 保持为最后成员。启动助手会显式标注 HAL 可执行文件的 SELinux 类型、重启服务并核对实际运行的 HAL 进程。
