> 2026-11-04：本文为历史记录。当前构建、自动本机 FSG 与产品清理见 [LTE-PRODUCT](LTE-PRODUCT.md)；旧实验工具已退出产品源码路径。

# LTE 纳入标准基线（2026-11-03）

最新 LTE 分支 `01004299d6b74d78eeb284a6b62b0e4184366ee2` 已合并，保留
main `fee67b2` 的 USB/音频/视频/EL2 修复以及统一 SQ1/SQ2 构建、触摸检测与校准源码。
**项目标准整镜像入口仍且仅为 `scripts/build.py`。**

## 默认设置与覆盖

标准入口及底层 `build-surface-pro-x.py` 默认 `--lte on`，不是只更新功能表。
从已重启验收的 Radio04 提取 LTE 专属文件及三个 init/wifi 钩子，叠加在匹配的
957b 完整运行组件上；不替换整个旧系统，不混入其他设备的存储布局、账号或凭据。
可使用 `--lte off` 明确选择原运行包；机型频率仍可通过 `--profiles` 自定义。

默认与 Mesa 回退各有一份跨机型共用运行包，仍是**一个磁盘镜像、两个菜单项**。
LTE 只加到默认项；Mesa 回退保留原非 LTE 运行包和原 DT，不把未验证的 LTE 改动
扩散到工作回退。原生 60Hz / 2.7Gbps×4、SQ1 GPU661 / SQ2 GPU661、KSU/EL2、
音频、USB、FUSE、视频、首次触摸校准及 screen_only/nohibernate 策略不改变。

## 私有输入与设备边界

把操作者已经拥有的、已验证的两个文件放在 `private/lte-baseline/`，或传
`--lte-bundle PRIVATE_DIRECTORY`：

| 文件 | 固定 SHA256 |
| --- | --- |
| `initramfs.cpio.gz`（Radio04） | `368d4f213d3f5bd6c8d6be159443c569cc82c846da2a88d111b5f416edcc4d05` |
| `android-el2.dtb`（IPA DT） | `49228267f0f2fce8a54b87965f6bf83fa718dbe9bb68ea6da9a227b9e2f1210f` |

构建核对完整哈希、LTE 之外的全部有效 cpio 条目、源码 helper 和已知钩子；
拒绝无关差异或重复叠加，原凭据成员保持最后。缺少输入直接报错，**不静默输出缺 LTE 的默认包**。
文件包含本机 FSG、APN 和登录资料，仅留 private/out；不能提交、公开分发或移植到另一设备。
公开仓库的 IPA DT 可供离线核对，但不是独立可启动包。

`lte-unit-guard` 在写入任何 Radio overlay、声明电话能力或准备 FSG 前，先只读核对
SQ2 型号、原单机 NVMe 序列号、原 ESP 标记、EL2 和关屏保护。
SQ1、其他 SQ2/磁盘或通用模板的不同 ESP 标记会跳过激活；不会使用另一台机器的 FSG。
SQ1 默认 DT 不增加 SQ2 IPA 节点。保留原 seed 的全部身份/RAM 检查以及 `sq2.lte=0` 开关。
**通用镜像带有受保护的 LTE 组件，不等于任意设备写盘后即可 LTE 上网。**
当前仅原 SQ2 配置经过 LTE 实机验证；更换设备/存储需独立取得并审核自己的校准、
布局和 APN，当前流程不会自动放宽绑定。

```sh
python3 scripts/build.py --template TEMPLATE.img --gnu-efi BUILT_GNU_EFI \
  --lte-bundle PRIVATE_LTE_DIRECTORY --output FRESH_OUTPUT
# 显式保留原无 LTE 包：追加 --lte off
```

触摸融合包仍通过 `--touch-bundle BUNDLE` 显式纳入；本轮保留其源码和已有 RAM 验证结果，
没有把既有 IMG/GZ 宣称为包含新 LTE/触摸包的重构建产物。

## 证据与限制

LTE 分支记录：原 SQ2 Radio04 独立启动及普通默认重启均自动联网/HTTPS200，
SIM/信号/设置、数据开关、完整飞行模式和 Wi-Fi 优先通过；音频实际 HAL、声卡、
ADSP attached 和 EL2 保护复核通过。详见 [原生 Radio](LTE-RADIO.md)、[数据链路](LTE-DATA.md)。
仅单 USIM/LTE IPv4；语音、SMS、IMS、eSIM、5G、PIN 管理不支持，IPv6/漫游/长期
移动稳定性未验收，偶发注册拒绝仍有记录。

本次合并只做主机离线回归、真实固定输入的关闭 LTE 构建以及源码副本回滚。
当前工作区没有 Radio04 私有启动包，未重建启用 LTE 的整镜像、未部署或重启设备。
fixture 通过只证明打包边界和默认选择，不代替新的 SQ1/SQ2 功能验收。
