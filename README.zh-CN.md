# Surface Pro X Googlebook OS

唯一默认整盘构建为 **hybrid**，SQ1/SQ2 共用 **Linux 6.18.60-sq2-android1**：Mesa GLES/Turnip + QTI gralloc/mapper5/HWC + 原生 OMX。

[标准构建](docs/STANDARD-BUILD.md) · [Windows 依赖准备](docs/DEPENDENCIES.md) · [休眠增量与镜像哈希](docs/HYBRID-SLEEP-20261116.md)

默认保留摄像头、触控、自动亮度/旋转，接入 Venus s2idle 修复。通用镜像清除继承的账户/Wi-Fi/SSH 凭据，新建 userdata/metadata，并保留首次设置。

```sh
python3 scripts/build.py --private-root PREPARED_INPUTS --output FRESH_LINUX_OUTPUT
```

在 SQ1 或 SQ2 原 Windows 上运行 `scripts/Prepare-HybridDependencies.ps1`，统一下载固定上游输入、提取共用 OEM 资料并打包；将 tar 与校验文件复制到 Linux 构建主机。
**目前仅靠原始依赖包仍不能从零生成全部 Android/QTI runtime/模板**；需要的已准备输入及剩余缺口在依赖文档中明确列出。

已完成组件可通过 `--reuse-components` 复用；`rebuild-hybrid-venus.py` 核对匹配 KOUT 后只重编 Venus，再重新打包。最新整盘已通过主机编译/回滚/回读检查，尚未进行新镜像 SQ1/SQ2 真机启动验收。

`verify/package` 仅核验/复制历史固定 v21 包，`compile` 仅构建内核组件，不是并列默认产品线。[BUILD.md](docs/BUILD.md) 中保留历史记录，以标准构建文档为准。

私有固件、运行库、凭据、镜像和原始日志不入 Git；明确审核的外部内核源码/DT 输入见[归档说明](source-dependencies/kernel-6.18/README.md)。原组件版权/许可保持；长期休眠、功耗和 SQ1 验收不由构建成功代替。
