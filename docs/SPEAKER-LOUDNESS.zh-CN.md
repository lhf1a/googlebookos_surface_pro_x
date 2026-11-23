# SQ2 内置扬声器：响度与播放修复

## 播放适配

`scripts/prepare-sq2-primary-audio.py` 由 `assemble_runtime` 在麦克风修正版 donor HAL 上调用，用于适配内置播放：
- 将 PRIMARY 输出名义延迟改为 10 ms（48 kHz 下 480 帧）。
- 将内置播放代理的物理缓冲请求设为至少 5760 帧（默认四周期为 4×1440 帧）。
- 保留麦克风、USB 代理、MMAP、模拟器 pacing 与底层写函数。
- 输出修补 HAL SHA256：`65ecd30b575ab81517d86456386842faa65496075216c8c6ae8d2dd7ef8af6e1`。

## 停播静音顺序与 DSP 启动检查

- **停播静音顺序**：`patch-sq2-audio-sequencing.py --machine-only` 仅启用 SDM845 停流前静音补丁（不启用 WSA881x `PRE_PMD`），在断流与重建 SoundWire 前向两侧写 `0x7c` 静音，随后由原 `prepare` 写 `0xfc` 开声，三个 playback link 保持 `pmdown_time=0`。后续状态见 [BUILD](BUILD.zh-CN.md) 与 [IMAGE](IMAGE.zh-CN.md)。
- **EL2 DSP 启动检查**：`audio-drivers-start` 在加载声卡前同时接受音频 DSP 的 `attached` 与 `running` 状态。

## 扬声器增益默认值

- `probe/test2/audio-speaker-route` 的 `enable` 分支将 RX7/RX8 Digital Volume 设为 **84**（**0 dB**），相比原始 **72**（−12 dB）提高 12 dB。
- PA 上电行为、DAC/COMP/BOOST 成对端口、VISENSE、HAL、固件、麦克风与均衡配置保持不变。

## 构建集成

`build-source-boot.py` 刷新 `audio-speaker-route`，校验打包脚本与源码一致，并在审计中记录 `speaker_rx_raw=[84,84]`、`speaker_rx_db=0`、`speaker_gain_change_db=12` 及 `speaker_route_sha256`。可通过 `SQ2_AUDIO_BOOT_IMAGE=/absolute/path/to/native-combined-initramfs.cpio.gz` 检查真实归档中的最终脚本。

## 硬件备注：SQ2 `ldoe14` XOB 供电轨

- 在 SQ2 上，RPMh `cmd-db` 将 `ldoe14`（`0x43900`）配置为仅支持使能位的 **XOB** 资源（仅存在 `+0x4` ENABLE），而主线 `qcom-rpmh-regulator.c` 将 `ldo14` 映射为 `pmic5_pldo_lv`（VRM 类型）；向不存在的 `VRM_VOLTAGE`（`+0x0`）或 `VRM_MODE`（`+0x8`）写入可能导致 RPMh AMC TCS 槽位卡死。
- `patches/kernel-6.18-surface-baseline.patch`（`sc8180x-surface-pro-x.dts`）定义了 `vreg_l14e_1p8: ldo14`（`1800000` uV、`RPMH_REGULATOR_MODE_HPM`、`regulator-boot-on`、`regulator-always-on`），由 `left_spkr: speaker@0,3` 与 `right_spkr: speaker@0,4` 引用；当前 Hybrid 6.18.60 EL2 端口保持该 DTS 不变。
