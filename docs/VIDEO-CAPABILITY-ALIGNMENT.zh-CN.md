# 固件与 Android 解码能力对齐

目标：使 Android 声明与固件实际报告的格式、profile/level、位深、分辨率和联合吞吐约束对齐；固件不支持的格式或组合直接丢弃，不补软件解码冒充硬解。

## 固件声明能力

原始能力包（3112 字节、75 属性、774 体字）的公开数值快照保存在 `configs/sq2-firmware-decode-capabilities.json`：

| 格式 | 单轴宽/高范围 | 每帧宏块上限 | 每秒宏块上限 | 帧率上限 | 码率上限 |
|---|---|---|---|---|---|
| H.264 / HEVC / VP9 | 96..8192 | 138240 | 3888000 | 480 | 220 Mbps |
| VP8 | 96..4096 | 36864 | 1036800 | 240 | 100 Mbps |
| MPEG-2 | 96..1920 | 8160 | 244800 | 30 | 40 Mbps |
| MPEG-1 | 本包未单列 | 未单列 | 未单列 | 未单列 | 未单列 |

固件单项最大尺寸与帧率受联合约束限制，系统声明需表达联合约束。

## 格式实现与待补齐项

| 格式 | 固件支持 | Android 实现 | 待补齐 |
|---|---|---|---|
| H.264 | 支持 | Baseline/Main/High，含 B 帧与 seek | 原生上限、完整 level/吞吐、持久化 |
| HEVC | 支持 Main/Main10 | `p010-stride` 原生后端与 scoped allocator 支持 Main10/P010 及 Main8 4K60 | 4K Main10 普通 Surface/60fps、HDR、持久化 |
| VP8 | 支持 | 原生及 Android C2 组件硬解 | 尺寸/吞吐上限、持久化 |
| VP9 | 支持 profile0/2 | v3 空 EOS 修正，profile0 4K60 硬解 | profile2/位深、更高尺寸/吞吐、持久化 |
| MPEG-1 | 支持 | 未接入 | 核对 Android MIME 映射 |
| MPEG-2 | 支持 | 原生会话待修裁剪；未接入 Android 组件 | 会话/裁剪修复、组件接入、尺寸/吞吐 |

## 驱动与组件约束

- **HFI 解析**：固定 6.3 源码 `hfi_parser()` 优先调用 `hfi_platform_parser()` 并使用静态表；SQ2 启用有界事务式真实能力解析，并修正 `ENUM_FRAMESIZES` 按请求 codec 查表，不修改其他平台静态表。
- **组件与别名**：AVC/HEVC 提供 `OMX.qcom.video.decoder.*` 兼容别名，实际创建 `c2.sq2.*.experimental` 组件；AV1 不在固件支持列表中，不予声明。
- **内存与尺寸上限**：C2 与媒体 XML 需根据 stride/scanlines、总内存预算与固件联合约束配置尺寸上限（P010 原生池候选为单帧 32MiB/池 384MiB，capture 总预算需覆盖 4K60 所需 192MiB，覆盖 8K 前需重新审计预算）。
- **工具入口**：`sq2-v4l2-capabilities.c` 枚举驻留驱动能力，`parse-sq2-hfi-caps.py` 解析完整固件包。
