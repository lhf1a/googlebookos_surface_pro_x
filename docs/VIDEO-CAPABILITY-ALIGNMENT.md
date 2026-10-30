# 固件与 Android 解码能力对齐

2026-10-30 最新实机补充：在p010-stride原生后端、e7da7db7… C2和scoped allocator候选组合下，普通UID自动选择HEVC硬件组件，Main10 320x240/30的300帧于9962ms完成EOS/stop/release；同组合Main8 3840x2160/60的600帧于9978ms完成。Main10晚帧截图有测试图案。这不证明4K Main10、HDR或所有声明组合；后续P010原生池候选扩大为单帧32MiB/池384MiB，4K独立C2六帧CRC/EOS/释放已通过；该新版服务尚未部署，普通应用4K10bit/60fps仍未验证。实验后定时恢复原版allocator，正常启动未部署，详见HANDOFF最新记录。

目标：对齐固件实际报告的格式、profile/level、位深、分辨率和联合吞吐约束。不是只让系统显示条目。2026-10-30 用户明确包含 HEVC 4K60，并要求覆盖全部固件格式。

用户随后明确“固件不支持就丢弃”：不为未支持格式补软件冒充硬解或虚构声明。固件声明支持但当前驱动设置失败，仍需区分能力缺失与适配错误；例如MPEG2的UNSUPPORTED_SETTING尚不足以否定整个codec。明确证据证明不支持的格式或组合从实现/广告目标中排除并记录依据。

原始能力包现已从设备恢复并重新严格解析：3112字节、75属性、774/774体字。公开数值快照保存在 `configs/sq2-firmware-decode-capabilities.json`，含原始日志SHA256，不含日志或设备隐私。以下为固件声明，不是任意尺寸/帧率组合的性能证明：

| 格式 | 单轴宽/高范围 | 每帧宏块上限 | 每秒宏块上限 | 帧率上限 | 码率上限 |
|---|---|---|---|---|---|
| H.264 / HEVC / VP9 | 96..8192 | 138240 | 3888000 | 480 | 220 Mbps |
| VP8 | 96..4096 | 36864 | 1036800 | 240 | 100 Mbps |
| MPEG-2 | 96..1920 | 8160 | 244800 | 30 | 40 Mbps |
| MPEG-1 | 本包未单列 | 未单列 | 未单列 | 未单列 | 未单列 |

早期V4L2实读只有MPEG-2、AVC、VP8、VP9、HEVC，全部96..4096；没有MPEG-1，HEVC/MPEG-2 profile control查询EINVAL。固定6.3源码hfi_parser()先调用hfi_platform_parser()，成功即返回，HFI4因而使用静态表。SQ2限定的有界事务式真实能力解析已在boot9e548e25…加载，HEVC六帧CRC及Android 4K60回归通过。但ENUM_FRAMESIZES仍误用当前默认AVC能力，导致各格式均显示8192，不能视为全部格式上限。按请求codec查表的修正已编入v3，等待新启动实读；不修改其它平台静态表。

| 格式 | 固件记录 | Android 实现/当前验证 | 待补齐 |
|---|---|---|---|
| H.264 | 支持；尺寸和吞吐见上表 | Baseline/Main/High；4K30含B帧、像素参考、seek；Bilibili实际1080p | 原生上限、完整level/吞吐、更多边界与持久化 |
| HEVC | 支持Main/Main10；尺寸和吞吐见上表 | Main10普通应用320x240/30通过；4K Main10独立C2六帧CRC/EOS/关闭通过；Main8普通应用4K60/600帧9978ms通过 | 4K Main10普通Surface/60fps、HDR、长播放/seek、B站4K |
| VP8 | 支持 | 原生及Android C2组件六帧CRC；普通应用自动硬解320x240/30及4K30（300帧9962ms），EOS/关闭通过 | 尺寸/吞吐、持久化 |
| VP9 | 支持profile0/2 | v3空EOS修正、原生/C2六帧CRC通过；profile0普通应用自动硬解4K30及4K60（600帧10056ms），EOS/关闭通过 | profile2/位深、更高尺寸/吞吐、持久化 |
| MPEG-1 | 支持 | 未接入；Android MIME映射待核对 | 不应因没有现成MIME映射而遗漏固件声明 |
| MPEG-2 | 支持 | 原生320x240缺裁剪、320x256无输出且关闭未确认；未接入Android硬件组件 | 原生会话/裁剪修复、帧验证、组件、尺寸/吞吐 |

当前差异：实验C2仍限96..4096、XML限320×240..3840×2160。不能把这些人为限制等同固件上限。实验store已接入Main10协商/P010，小尺寸普通应用通过；新P010预算候选已通过4K独立C2六帧，但尚未验证4K普通应用/吞吐。现有内存限制不足以覆盖8K，继续扩尺寸前需要审计stride/scanlines、总内存预算和固件联合约束。HEVC 4K60曾触发capture总预算128MiB的EOVERFLOW，192MiB候选已通过600帧，但不因此无限放大预算。

证据入口：`sq2-v4l2-capabilities.c`只枚举驻留驱动；`parse-sq2-hfi-caps.py`严格解析完整固件包。原始日志和caps-44c0.json现已恢复到忽略的out/video-decode/hfi-session。历史report-sq2-video-capabilities.py固定早期H264证据模型，不能用它的旧状态代表当前实现。

AVC/HEVC采用用户要求的OMX.qcom.video.decoder.*兼容别名，实际创建c2.sq2.*.experimental组件。别名不替换实现，不增加能力。AV1目前不在已记录固件格式中，不能伪造硬件支持。固件单项最大尺寸和帧率不意味着任意组合都能达到；系统声明应表达联合约束，性能另行实测。

正常启动持久化与重启回归尚未完成；所有当前实验只在RAM服务/临时媒体XML下验证。详细哈希、失败记录和现场状态见HANDOFF。
