# 外部模块源码输入

`external-sources.tar.gz` SHA256：`b080faec4c0229eebbeb0d53c4f1946f6df3c2fbb593b3909015d9caea14fa58`。
由 `configs/hybrid-kernel-source.json` 固定，用于恢复已编译验证的 6.18 外部模块树；不是 OEM 驱动/固件分发包。

归档共 519 个普通文件：C/头文件、构建文件、寄存器 XML、DTS、源码备份和 manifest，以及三个配套 `overlay.dtbo`。
这三个 DT 覆盖都能从相邻 DTS 编译，解码后的树逐字一致；fw-618/hfi-618-v2 的二进制也逐字相同，identify 仅确认解码树一致，不宣称 blob 哈希一致。
它们不是固件、内核模块或整盘镜像。归档不含 ELF、Windows 可执行文件、OEM MBN、私钥或个人配置。

原文件版权/SPDX 保留；KernelSU 随主内核补丁提供，包括 `drivers/kernelsu/LICENSE`，不在此外部归档中。
新增休眠修复在构建副本上由 `full_image_video.ensure_venus_source_hooks` 应用，原归档字节保持不变。
