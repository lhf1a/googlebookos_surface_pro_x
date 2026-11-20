# SQ2 内置扬声器：响度与EL2停播爆音修复已验收（2026-11-02）

## 当前状态：连续播放短回声修复已验收

连续游戏声音破碎与此前停播爆音是不同问题。NTE 长按喇叭时，
内置输出的 4096 帧块含周期性全零段；完整块在 HAL 入口到
`pcm_write` 之间逐字节不变，静音在应用 AudioTrack 提交时已出现。
同场景 USB 耳机为连续输出，不能据此归咎于实体喇叭或擅改左右路由。

`scripts/prepare-sq2-primary-audio.py` 生成播放适配，不自行安装或选择启动项；
整包入口 `assemble_runtime` 在应用运行库替换后调用它，包含组件复用路径：
PRIMARY 输出名义延迟改为 10 ms（48 kHz 下 480 帧），内置播放代理
至少使用 5760 帧的物理缓冲请求，默认四周期在本次实机成为 4×1440 帧。
单独缩短名义延迟的旧候选曾造成 XRUN 无声，不能用于交付。
麦克风、USB 代理、较大缓冲请求、MMAP、模拟器 pacing 及底层写函数保留。
适配输入为固定 donor 的麦克风修正版，非任意 HAL 的通用二进制补丁；
不新增设备身份准入条件。修复 HAL SHA256：
`65ecd30b575ab81517d86456386842faa65496075216c8c6ae8d2dd7ef8af6e1`。

当前验证边界：单台 SQ2 的热会话中，用户确认 NTE、视频、鸣潮均正常；
400 对 480 帧 HAL 输入/声卡写入块一致，采样窗口无长静音断口。
独立测试启动项已完成完整重启，实际 HAL 与候选相同，3 秒测试播放完成
144000 帧、声卡指针持续前进，AAudio 仍记录 1 次欠载；
用户随后确认完整重启后视频、NTE、鸣潮三类应用均正常。
修复已接入默认构建；本轮未重新构建整个磁盘镜像，也未改实机默认启动项，
设备当前使用独立音频测试项。新镜像构建与存量设备默认入口更新不能混为一谈。
自动回归执行实际 ARM64 指令，验证 PRIMARY、输入、外部设备、
较大请求及异常配置分支，并验证重复应用及私有身份数据不变；
这次单机完整启动验收不代替 SQ1/SQ2 跨设备及长期功耗测试。

## 已验收的停播静音顺序

本日后续：只恢复SDM845静音补丁，WSA881x不改，重启boot059265e0…后用户确认
有声音且无停播爆音；间隔10秒的两遍播放及原应用暂停/恢复均正常。有限跟踪发现
原驱动在HAL停播后prepare重建SoundWire时PA仍为0xfc；修改后两侧先写0x7c静音，
再断流、重建，原prepare再写0xfc开声。只改变停流前顺序，不改增益/端口/HAL。
使用`patch-sq2-audio-sequencing.py --machine-only`；旧组合补丁的PRE_PMD部分
仍不启用。其无声机制尚未单独证明，不为了追究已排除的变体继续实机试错。
以下为定位历史；长期声学、任意应用与所有外部设备不在本轮验收范围。

2026-11-02用户在新高通默认项与Mesa回退项均报告停播一下爆音；不能沿用下方
旧EL1验收声称当前启停正常。检查发现EL2内核源码/模块遗漏先前的
`patch-sq2-audio-sequencing.py`，但三个playback link仍正确设置pmdown_time=0。
尝试恢复sdm845两处SoundWire teardown前静音及WSA881x PRE_PMD静音后，
用户反馈完全无声，尽管两遍PCM均528000帧/零xruns。已恢复原模块并重启，
用户确认“已恢复声音”。故缺失补丁只是差异，不能作为已经证明的完整根因；
单独恢复补丁的候选失败，不进入默认构建。默认构建固定能出声的模块hash，
停播爆音仍开放；下次先核对原驱动全套启动/解除静音行为。增益、HAL、DMIC、
端口配对及transport保持。后续状态见 [BUILD](BUILD.md) 与 [IMAGE](IMAGE.md)。

2026-11-02 EL2无声为启动前置判断遗漏：音频DSP已被接管，state为attached，旧
audio-drivers-start只接受running，尚未加载声卡即退出。允许这两种活动状态后
原声卡/HAL恢复，AAudio528000帧、active xruns0，用户确认声音正常。没有修改
下述增益、SoundWire端口或音频transport；启动包持久修正已并入默认构建。

用户确认 +12 dB 听感很好，选择该档位作为默认，并在重启验证后确认声音问题已解决。
先前 clean 音乐正常、左右顺序正确，短间隔和 10 秒间隔启停均无用户可闻爆音。
此处关闭当前 SQ2 内置扬声器的响度、偏侧与有限启停测试问题，不表示覆盖任意应用或长期声学认证。

## 默认值与边界

- `probe/test2/audio-speaker-route` 的 `enable` 分支：RX7/RX8 Digital Volume 均为 **84**，即 **0 dB**。
- 相比原始 72（−12 dB）提高 12 dB；这不是功放额外增加 12 dB，也不是 Android 系统音量数字。
- +6 dB 仅在 Android MUSIC 15/15 时获用户“够用”确认；后来用户选择 +12 dB，并确认听感很好。
- 保留 PA 原有上电行为、DAC/COMP/BOOST 成对端口、VISENSE、HAL、固件、麦克风、均衡配置及既有回退入口。
- 不复刻 Dolby。Windows 原厂静态配置分析不能证明实际声压、活动路由或调音曲线完全一致；不据此宣称固件完全对齐。

## 验证与部署

原脚本的同输入副本执行结果：BASELINE 发出 RX72/72，MODIFIED 发出 RX84/84，
ROLLBACK 发出 RX72/72，均 exit0。独立恢复副本的原始 hash 一致；补丁可精确重建候选。
这些是 mock mixer I/O 的脚本验证，不是三次硬件启动。

实机同曲片段使用 Android AudioTrack/MUSIC、48 kHz 双声道，活动采样未见效果链。
+8 到 +12 dB 对照两段各完成 864000 帧，脚本 exit0；成功后保留 RX84/84，未最终自动恢复旧值。
用户随后确认 +12 dB 听感很好。默认入口仅追加音频路由增量；完整启动链 last-entry-wins 审计
只改变 `bin/audio-speaker-route`，原凭据 member 最后，Image/DTB/固件/HAL/其他入口不改。
已按磁盘身份、双 UUID、marker、root source、旧菜单和载荷 hash 校验并 sync 原子部署。
重启由用户批准发起；首次 SSH 观察超时，之后由用户明确确认设备正常启动、已播放音频、响度正确，
因此不把该超时写成独立 SSH 启动读回通过。

## 默认构建

`build-source-boot.py` 显式刷新 `audio-speaker-route`，检查打包后的有效脚本与当前源码完全一致，
并在 audit 记录 `speaker_rx_raw=[84,84]`、`speaker_rx_db=0`、`speaker_gain_change_db=12`
和 `speaker_route_sha256`。完整默认启动包构建通过；专项测试可用
`SQ2_AUDIO_BOOT_IMAGE=/absolute/path/to/native-combined-initramfs.cpio.gz`
检查真实归档中的最终脚本。

源码通用构建仍保留原有显示/触控等独立选择，不应将整个通用包无差异覆盖当前设备。
私有固件、Windows 驱动、凭据、原始日志和构建产物不进入 Git。

离线收尾：完整默认启动包 SHA256 为 9bb364313578587be6b32e83777db57745a102edb3e1e99d1c15d77f3e97ec3e；包含当前有效路由脚本。扬声器 5 项（含真实归档）、USB 默认集成 4 项、蓝牙 3 项、显示载荷 5 项，共 17 项回归全部通过、无跳过。

## 硬件备注：SQ2 `ldoe14` XOB 扬声器供电轨审计

- **上游硬件发现（`surface-pro-x-sq2-linux` 参考）**：在 Surface Pro X SQ2 上，RPMh `cmd-db` 将 `ldoe14`（地址 `0x43900`）配置为仅支持使能位的 **XOB** 资源（仅存在 `+0x4` ENABLE 寄存器），而非标准 VRM 稳压器。主线 `drivers/regulator/qcom-rpmh-regulator.c` 的 `pmc8180`/`pm8150` 表将 `ldo14` 映射为 `pmic5_pldo_lv`（VRM 类型）；若驱动向不存在的 `VRM_VOLTAGE`（`+0x0`）或 `VRM_MODE`（`+0x8`）寄存器发起主动写操作，可能导致 RPMh AMC TCS 槽位卡死。
- **本仓库基线 DTS 审计**：`patches/kernel-6.18-surface-baseline.patch` 中的 `arch/arm64/boot/dts/qcom/sc8180x-surface-pro-x.dts` 定义了 `vreg_l14e_1p8: ldo14`（标注 `regulator-boot-on`、`regulator-always-on`、`1800000` uV、`RPMH_REGULATOR_MODE_HPM`），并由 SoundWire 左右功放节点 `left_spkr: speaker@0,3` 与 `right_spkr: speaker@0,4` 的 `vdda-supply`/`vddb-supply` 引用。
- **当前处理与验证边界**：当前 Hybrid Linux 6.18.60 EL2 端口下现有 SQ1/SQ2 扬声器 DTS 已稳定出声，本轮**保持现有 DTS 不变**，不主动删改 `vreg_l14e_1p8` 或引入额外的独立 XOB 驱动模块；记录该硬件约束供后续内核升级、主线同步或排查 SQ2 `rpmh-regulator` TCS 卡死时参考。上游 XOB 现象来源于 Linux 7.3 外部参考，非本仓库 6.18.60 实机复现结论。
