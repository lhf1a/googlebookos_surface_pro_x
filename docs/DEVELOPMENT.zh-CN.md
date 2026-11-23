# 通用交付规则（PORT 契约）

适用于构建、启动和镜像交付。来源与优先级见根 `AGENTS.md`；构建入口见 [BUILD](BUILD.zh-CN.md)。

## PORT-01：同一份镜像，任意 SQ1/SQ2

标准镜像应在任意 SQ1 或 SQ2 上直接运行，不允许按机器重编、改代码或替换设备专属启动包。机型差异由 SMBIOS/SoC、硬件能力与运行时资源发现处理。

## PORT-02：禁止单机准入

不得以序列号、NVMe 型号/序列、MAC、IP/SSH 地址、SSH 主机指纹、原盘 UUID/ESP 标记、Android 用户编号或历史采集 SHA 作为通用构建或正常启动的准入条件。

## PORT-03：按运行时硬件事实适配

不得固定一次启动的 phandle、`eventN`、`cardN`、节点编号或调试主机路径。硬件类别/版本 quirk 须有硬件依据，不将单台设备的测量结果推广到所有设备。

## PORT-04：镜像与本机状态分离

通用镜像不得复制调试机的密码/shadow、SSH 私钥/主机身份、Wi-Fi 凭据、IMEI/NV/FSG 校准、用户数据或触摸校准到其他机器。设备唯一数据由目标自身存储或本机首次配置提供；实验室私有包须单独标记。

## PORT-05：保留安全保护

保留模块/固件 ABI、产物完整性、真实硬件兼容性、SMMU/资源边界、`screen_only`/`nohibernate` 与不可重复启动固件保护。写盘前动态识别目标盘/ESP/根源并防止误写。

## PORT-06：跨设备验收

同一产物需覆盖 SQ1/SQ2、默认/回退入口及不同设备身份、容量与枚举资源的回归；旧数据升级不得破坏凭据与用户设置。未测修订或功能在文档中如实标注。

## 验证与环境

- 构建和启动保留真实失败传播与 ABI/格式/路径/磁盘唯一性约束；证据、原始日志与含身份产物留在 ignored 的 `out/`、`private/`，不进 Git。
- 构建主机为 Ubuntu 24.04 WSL，项目位于 `F:\WSL\SPX-Workspace\chromebookOS-workspace.vhdx` 挂载的 `/mnt/spx-workspace/chromebookOS`；Windows 侧只放启动器。
- 源码构建须在 Linux 文件系统执行，避免 NTFS/9p 上的大量小文件；私有输入根默认 `private/build-inputs/`，可用 `--private-root` 重定位。
