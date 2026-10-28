# Quest 2 v77 配套图形栈参考

2026-10-28。本页记录同一 Quest 2 OTA 的离线参考材料，供 Surface Pro X 的闭源 GPU 适配使用。提取不等于移植完成，也不包含把 Quest 的显示、音频、相机或其他硬件驱动整套覆盖到 Surface 的操作。

## 来源与核验边界

候选 OTA 为 Quest 2 v77、构建号 `51584880050400150`，来源及分区元数据固定在 `configs/quest2-full-donor-candidate.json`。现有 `out/quest2-gpu/q2-v77-vendor.img` 为 273108992 字节，SHA256 为：

```text
44f90742c03c04037e378860ad493098a369d2bc752aa0d56d14f63ae91b6722
```

vendor 已通过 131 个 REPLACE_XZ 操作的压缩数据哈希、目标 extent 全覆盖及最终分区哈希检查。OTA 来自第三方镜像站；完整 ZIP 哈希、payload 元数据签名和 OEM 信任链尚未验证。准确表述是“从所记录的 Quest 2 v77 OTA 候选中提取并核对分区及文件哈希”，不能称为“已通过 Meta 官方签名认证”。

`configs/quest2-gles-v77-inputs.json` 与 `configs/quest2-v77-graphics-reference.json` 固定的原有 18 个库继续有效。原 `out/quest2-gpu/q2-v77/lib64` 已解出更多配套库，18 是此前审计数量，并非镜像只有 18 个图形文件。新增 `configs/quest2-v77-display-reference.json` 单独固定显示服务、64 位 SDM/HWC 依赖、服务配置与三个 A650 固件的哈希，不改变原 GLES/Vulkan 输入清单。

本次完整提取清单记录 906 个普通文件、269359659 字节；新增显示清单固定其中 70 个文件。70 项均已重新读取提取结果，核对清单中的长度和 SHA256。此项完成的是离线材料核验，不是服务加载或硬件测试。

## 可复现提取

在 WSL Ubuntu 24.04 的仓库根目录执行，需要 Python 3 和 e2fsprogs 的 `debugfs`。目标目录必须不存在；不要覆盖旧证据。

```sh
python3 scripts/extract-quest2-v77-reference.py \
  out/quest2-gpu/q2-v77-vendor.img \
  out/quest2-gpu/vendor-reference-v77
```

脚本先验证固定分区大小和 SHA256，再用只读 debugfs 枚举并提取所有普通文件。文件位于 `out/quest2-gpu/vendor-reference-v77/vendor/`；原始镜像路径 `/lib64/...` 对应提取路径 `vendor/lib64/...`。符号链接及特殊文件只记录，不创建；所有者、权限等原始信息保存在清单，不将该目录视为可直接部署的 vendor 文件系统。只有末尾生成的 `inventory.json` 才代表提取完成，未生成时属于未完成目录。脚本不挂载镜像、不打开设备写入，也不运行提取出的二进制。

如果镜像缺失，可由现有两个下载范围重新生成新的文件：

```sh
python3 scripts/rebuild-quest2-v77-vendor.py \
  downloads/quest2-gpu/q2_51584880050400150.zip.part \
  downloads/quest2-gpu/q2-v77-vendor.compressed.part \
  out/quest2-gpu/q2-v77-vendor-rebuilt.img
```

该脚本只接受新的 `out/*.img`，固定 vendor 身份和操作类型；它不是任意分区提取器。已有正确镜像时无需重新下载整个约 1.17 GB 的 OTA。

固定显示参考文件可按以下方式复核，路径语义由新增清单定义：

```sh
python3 - <<'PY'
import hashlib, json
from pathlib import Path
cfg = json.loads(Path('configs/quest2-v77-display-reference.json').read_text())
root = Path(cfg['reference_root'])
report = json.loads((root.parent / 'inventory.json').read_text())
assert report['build'] == cfg['build']
assert report['image_sha256'] == cfg['image_sha256']
entries = {entry['path']: entry for entry in report['entries']}
for name, expected in cfg['files'].items():
    entry = entries[name]
    path = root / name.lstrip('/')
    assert entry['regular'] and entry['sha256'] == expected, name
    assert path.stat().st_size == entry['bytes'], name
    with path.open('rb') as source:
        assert hashlib.file_digest(source, 'sha256').hexdigest() == expected, name
print('QUEST2_DISPLAY_REFERENCE_OK', len(cfg['files']))
PY
```

## 参考材料范围

| 组成 | 镜像内路径与用途 |
| --- | --- |
| 分配服务 | `/bin/hw/vendor.qti.hardware.display.allocator-service`；配套 gralloc/metadata/mapper 库用于核对原始 handle、堆选择及布局语义 |
| HWC/合成服务 | `/bin/hw/vendor.qti.hardware.display.composer-service`；本镜像未见独立 `hwcomposer.kona.so`，需结合服务和 SDM 库研究 |
| 显示库 | `/lib64/libsdmcore.so`、`libsdmextension.so`、`libsdmutils.so`、`libsdedrm.so`、`libdrmutils.so`、`libdrm.so`、`libqdutils.so`、`libqservice.so`、`libhistogram.so` 及 `libdisplay*.so` |
| 色彩与扩展 | `/lib64/libsdm-color.so`、`libsdm-colormgr-algo.so`、`libsdm-diag.so`、`libsdm-disp-vndapis.so`；存在不代表 Surface 需要全部加载 |
| 服务契约 | `/etc/init/vendor.qti.hardware.display.*.rc`、`/etc/vintf/manifest/*graphics*`、`/etc/vintf/manifest/*display*`、主 manifest 与 compatibility matrix |
| 平台配置 | `/build.prop`、`/etc/init/hw/init.kona.rc`、`init.kona.power.rc`、`init.hollywood.rc`、`/etc/ueventd.rc`、linker/电源/温控/IRQ 配置；记录原厂行为，不直接套用其设备路径和策略 |
| GPU 固件 | `/firmware/a650_gmu.bin`、`a650_sqe.fw`、`a650_zap.elf`；仅参考，不装入 Surface |

全目录提取还包含 `/lib` 的 32 位实现及无关设备材料，但新增固定清单只覆盖当前 64 位图形研究。闭源文件、固件、完整清单和原始分析输出留在已忽略的 `out/`；Git 只保存脚本、哈希和研究说明。

## 已从原始文件确认的接口差异

**Android 服务契约。** VINTF 声明标准 allocator 3.0、QTI allocator 3.0、标准 composer 2.4、QTI composer 3.0、display.config 2.0，以及 32/64 位 passthrough mapper 3.0。当前 Surface 的 mapper5/minigbm 路径不能因名称相似而直接承接这些服务；现有进程内 mapper 桥接也不是完整原厂分配服务。

**分配与元数据。** `libgralloccore.so` 的 DT_NEEDED 包含 `libion.so`，且实际导入 `ion_open`、`ion_close`、`ion_alloc_fd`。本项目当前的 ION 控制 FD 适配仍不能代替原始堆分配、缓存、安全缓冲区和完整多平面语义。保留真实 PlaneLayouts、物理 stride/scanline 与逻辑宽高的区分，不将临时成功的线性路径推广为所有格式兼容。

**显示内核 ABI。** `libsdedrm.so` 使用 DRM atomic，但还识别 Qualcomm SDE 的 `input_fence`、`RETIRE_FENCE`、`output_fence_offset`、`scaler_v1`、`scaler_v2`、`dest_scaler`、`fb_translation_mode` 和大量 DSPP 私有属性。`libdrmutils.so` 含 `DRM_IOCTL_MSM_RMFB2` 路径；`libsdmcore.so` 引用 `/sys/kernel/debug/dri/0/debug/`、HDCP 和 `panel%d-backlight`。这些是静态接口证据，不等于已在 Surface 调用成功。主线 MSM/DPU 与下游 SDE 同样使用 DRM，并不意味着属性、blob 结构、ioctl、fence 和面板控制兼容。

**GPU 内核 ABI。** 原 `init.kona.rc` 写 `/sys/class/kgsl/kgsl-3d0/perfcounter`，`ueventd.rc` 配置 `/dev/kgsl-3d0` 与 `/dev/ion`。当前 Surface 使用 MSM DRM。原版 GSL 中已实测可达的 MSM 路径与 Quest 的 KGSL 启动环境要分别核对，不能把所有版本一概描述成 KGSL-only，也不能认为调频已经实现了 KGSL ABI。GPU VA、提交队列、缓存和 sync_file 仍以实际调用证据为准。

**原厂设置。** `build.prop` 指定 `ro.hardware.egl=adreno`、`ro.hardware.vulkan=adreno`，并包含 Quest 面板方向及显示设置。`init.kona.rc` 在启动阶段删除 `/data/vendor/gpu/esx_config.txt` 和 `qgl_config.txt`，随后允许 `debug.vendor.gpu.*` 属性触发写入。因此不能假定 OTA 自带固定的 Direct/sysmem 配置，也不应照搬 Quest 的屏幕方向、性能计数器和电源策略。

## 尚缺的同版材料

本 vendor 镜像中未找到 `/lib64/libion.so`、`libadreno_app_profiles.so` 或 `libgpu_tonemapper.so`。其中 `libion.so` 是已观察到的真实依赖；后两者的缺失本身不证明该版本需要它们。若依赖分析确实需要额外文件，应先检查同一 OTA 的 system、system_ext、odm 及其中的 VNDK/APEX，不混入其他发布包的同名库。

已读 payload manifest 的后续取证范围如下，均为候选 ZIP 的绝对字节偏移、包含两端：

| 分区 | 字节范围 | 解包镜像字节数 | 操作类型 |
| --- | --- | ---: | --- |
| boot | 405296–12589986 | 83886080 | 1、8 |
| dtbo | 12998519–13029280 | 8388608 | 1、8 |
| odm | 25912339–59726458 | 73109504 | 0、8 |
| system | 203772287–582890968 | 1065078784 | 0、1、8 |
| system_ext | 582890969–1084778713 | 1373446144 | 0、1、8 |

当前 manifest 未见独立 vendor_boot 分区。vendor 的 `/lib/modules` 未见 GPU/SDE 模块，因此 boot 内核及 DT 是后续参考重点；尚不能据此声称已确认所有驱动都内置。当前 vendor 重建器只处理类型8，不能原样用于上表分区。需要按 manifest 实现并校验相应操作、extent、完整目标哈希，才可将重建产物视为完成。

## 不替换 Surface GPU 固件的依据

已固定的 Meta 官方源码提交 `e8c2245c4ca511a8208568e713b14500f252c0ec` 的 GPU 表明确区分 A650 与 A680：A650 使用 `a650_sqe.fw`/`a650_gmu.bin`，GMEM 为1152 KiB；A680 使用 `a630_sqe.fw`/`a640_gmu.bin`，GMEM 为2048 KiB，并有不同初始化表。这已经足以否定“只修改频率即可换用全部固件”的前提。证据见 [Meta GPU core 表](https://github.com/facebookincubator/oculus-linux-kernel/blob/e8c2245c4ca511a8208568e713b14500f252c0ec/drivers/gpu/msm/adreno-gpulist.h)，本地固定哈希见 `configs/quest2-gpu-research.json`。

该公开树为4.19.325，尚未证明它与本次OTA boot逐字对应。Surface SQ2 的实际身份仍是 SC8180X/A680.1 `0x06080001`；继续保留其已验证的 SQE/GMU/ZAP、设备树、温控和频率配置，不使用 Quest A650 固件或 SC8280XP 身份。原厂用户态实现可以作为布局、同步、错误处理和接口的参考，固件提取本身不构成跨平台加载的兼容证明。

运行验证与最新阻断点继续记录在 `docs/QUEST2-GPU-RESEARCH.md`；本文的提取和静态盘点不增加任何设备验证结论。
