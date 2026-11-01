# SQ2 内置扬声器：响度与EL2停播爆音修复已验收（2026-11-02）

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
端口配对及transport保持。后续实际状态以HANDOFF为准。

2026-11-02 EL2无声为启动前置判断遗漏：音频DSP已被接管，state为attached，旧
audio-drivers-start只接受running，尚未加载声卡即退出。允许这两种活动状态后
原声卡/HAL恢复，AAudio528000帧、active xruns0，用户确认声音正常。没有修改
下述增益、SoundWire端口或音频transport；启动包持久修正见HANDOFF最新条目。

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
