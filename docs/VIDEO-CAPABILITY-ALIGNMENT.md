# Firmware and Android Decode Capability Alignment

Goal: align Android declarations with the formats, profiles/levels, bit depths, resolutions, and combined throughput constraints reported by the firmware; drop unsupported formats or combinations instead of substituting software decode.

## Firmware-Advertised Capabilities

The public numeric snapshot of the raw capability packet (3112 bytes, 75 properties, 774 body words) is saved in `configs/sq2-firmware-decode-capabilities.json`:

| Format | Single-Axis W/H Range | Max Macroblocks/Frame | Max Macroblocks/s | Max Framerate | Max Bitrate |
|---|---|---|---|---|---|
| H.264 / HEVC / VP9 | 96..8192 | 138240 | 3888000 | 480 | 220 Mbps |
| VP8 | 96..4096 | 36864 | 1036800 | 240 | 100 Mbps |
| MPEG-2 | 96..1920 | 8160 | 244800 | 30 | 40 Mbps |
| MPEG-1 | Not listed separately | Not listed | Not listed | Not listed | Not listed |

Per-item maximum dimensions and framerates are subject to combined constraints, which system declarations must reflect.

## Format Implementation and Remaining Gaps

| Format | Firmware Support | Android Implementation | Remaining Gaps |
|---|---|---|---|
| H.264 | Supported | Baseline/Main/High with B-frames and seek | Native ceiling, full level/throughput, persistence |
| HEVC | Supported Main/Main10 | `p010-stride` native backend and scoped allocator support Main10/P010 and Main8 4K60 | 4K Main10 normal Surface/60fps, HDR, persistence |
| VP8 | Supported | Native and Android C2 hardware decode | Size/throughput ceiling, persistence |
| VP9 | Supported profile0/2 | v3 empty EOS fix, profile0 4K60 hardware decode | profile2/bit depth, higher size/throughput, persistence |
| MPEG-1 | Supported | Not integrated | Check Android MIME mapping |
| MPEG-2 | Supported | Native session needs crop fix; Android component not integrated | Session/crop fix, component integration, size/throughput |

## Driver and Component Constraints

- **HFI parsing**: Fixed 6.3 source `hfi_parser()` calls `hfi_platform_parser()` first and uses static tables; SQ2 enables bounded transactional real capability parsing and fixes `ENUM_FRAMESIZES` to look up by requested codec without modifying other platforms' static tables.
- **Components and aliases**: AVC/HEVC expose `OMX.qcom.video.decoder.*` compatibility aliases that instantiate `c2.sq2.*.experimental` components; AV1 is not in the firmware support list and must not be advertised.
- **Memory and dimension limits**: C2 and media XML dimension limits must be configured alongside stride/scanlines, total memory budget, and firmware combined constraints (the P010 native pool candidate uses 32 MiB/frame and 384 MiB total, and capture budget needs 192 MiB for 4K60; expanding to 8K requires auditing budgets first).
- **Tool entry points**: `sq2-v4l2-capabilities.c` enumerates resident driver capabilities; `parse-sq2-hfi-caps.py` parses the full firmware packet.
