# 6.18 LTS 私有构建输入

当前唯一版本6.18.60-sq2-android1；入口与说明见[BUILD](BUILD.md)、[通用交付规则](DEVELOPMENT.md)。旧6.3.8模板/模块构建参数已退休，不能再作为当前默认。

## 已保存的当前基线

- private/build-inputs/kernel-6.18/EFI/SQ2L618C：当前验收私有EFI包，hash由configs/kernel-baseline.json核对。
- private/build-inputs/kernel-6.18/linux-6.18.60-current-sources-20261107.tar.gz：当前内核及12组外挂源码，303774784 bytes，SHA256 26f3045793cdf6e44379b75cb0adda0b7b32f2914537fcfb02f83c8d752a3b3b。解包结构与SOURCE_MANIFEST.json/SOURCE_MEMBERS.json以本地归档为准。
- configs/kernel-6.18-baseline.config：实际已编译配置。configs/kernel-external-modules.json：12组/15个当前覆盖模块的构建与安装位置。SPI、SSAM、Venus不能遗漏。
- 工具链需显式指定匹配ARM64 compiler prefix；不从设备身份猜测，不自动下载不固定的latest。

归档已全成员回读，内核Image/modules/dtbs实编通过；恢复到新目录的外挂模块也编译通过。它们是可恢复源码输入，不宣称新编译输出已经再次真机验收。

## 私有资料与产品边界

发布包保留操作者SSH身份，仅用于本地明确授权的设备，**不得直接作为通用交付**或公开上传。Git只存代码、配置、hash、说明，不存密钥、Wi-Fi、密码/shadow、FSG/NV、APK、用户数据或原始日志。保存工作不等于发布私有包。

SQ1/SQ2通用化目标保持。当前基线仍有单机私有输入/用户选择等历史差距，修改入口**不代表这些实现已修正**，不得伪称双机新镜像已验收。Scene用户数据软链接不注入新镜像；Scene GPU读数兼容仍待处理。

旧设备回退和恢复备份不删除。清理范围为仓库中的6.3.8专用构建工具，清单见configs/retired-kernel-tools.json。