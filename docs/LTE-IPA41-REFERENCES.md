# SQ2 LTE：IPA4.1 同代配置参考

2026-11-02。这里只记录可核对的来源，不把手机移植结果等同于SQ2 LTE可用。

## 平台选择

| 平台 | 公开配置中的IPA版本 | 对SQ2的用途 |
|---|---|---|
| SM8150 / Snapdragon855 | 4.1，hw enum15 | 同代主参考：寄存器、端点、资源表、SRAM布局 |
| SM8250 / Kona / Snapdragon865 | 4.5，hw enum17 | 相邻代际对照，不能直接复制端点/SID/内存 |
| SC7180 | 4.2 | 现6.3.8驱动结构参考，不是SQ2硬件配置 |

已固定并下载的Qualcomm下游源：

- [SM8150 DT](https://android.googlesource.com/kernel/msm/+/c8bbb1e3a59cb5cde2da79a8db638691b70535d7/arch/arm64/boot/dts/qcom/sm8150.dtsi)，SHA256 `a71e5e3fa6108e6fac6df1373c6dce002fba0d2c9cc97f68be2dba4b1a5842d1`。
- [IPA4.1 驱动表](https://android.googlesource.com/kernel/msm/+/c8bbb1e3a59cb5cde2da79a8db638691b70535d7/drivers/platform/msm/ipa/ipa_v3/ipa_utils.c)，SHA256 `27c1d76ef2ea21b2cc69686f1a18642f40a20bc1f622f17e684aeae09c033413`。
- [Kona DT](https://android.googlesource.com/kernel/msm/+/9af99ae982b0c47fb0ff33e475355dd755afd89c/arch/arm64/boot/dts/qcom/kona.dtsi)，SHA256 `d81aa04a4c67380bf0ee05f5ebca853ff27262679cbab697fbf8122656493007`。

网页渲染器有超时/取页失败；实际HTTPS原始文本下载和本地解析成功，不能把渲染错误
当成源文件不存在。记录保存在同一LTE事务的ipa41-ownership-20261102-v1子目录。

## 新取得的可用信息

SM8150 DT明确给出AP/WLAN/uC三种流角色分别为SID `0x520/0x521/0x522`，
AP额外IMEM映射为 `0x146BD000`、大小 `0x2000`。数值与既有SQ1 IORT范围和
Windows OciMemAddr/size参考吻合；该关系是参考交叉印证，不是本机EL2占用许可。
Kona配置则使用 `0x5C0/0x5C1/0x5C2`，说明不能把865 DT当SQ2原样节点。

`ipa_utils.c` 的 `ipa_4_1_mem_part` 有86个纯数字字段，SRAM末端为 `0x2800`。
源资源5类、目的资源2类，分别有4个有效组；QSB DDR reads/writes=12/8，
PCIE=12/4。这些已提取到 `configs/sq2-ipa-v4.1-reference.json`，全部标为
reference_only；本机实际接受的共享内存布局、所有权和初始化顺序尚未验证。
保留原6寄存器/8端点候选硬拒绝分支，未导入这些值到运行驱动。

## 另一套Linux 6.16形状的参考

[Hotdog项目的IPA4.1工作目录](https://github.com/Sr-0w/hotdog-linux-bringup/tree/47087509c4289c2580c54f5433e55366b2e00445/work/ipa-v4.1)
和该版本kernel-checkpoints中的0144–0147补丁，提供平台数据、寄存器、DT和binding。
作者报告本机创建rmnet_ipa0；这不是本项目实测，也不表示上游已接收或蜂窝流量通过。
只下载/检查源码和元数据，没有执行该项目的任何构建、安装或硬件脚本。

实际比较：8组endpoint/channel均与已固定Qualcomm4.1表吻合；23组SRAM offset/size
也逐项吻合，可帮助翻译为mainline结构。但不能原样套用到SQ2：

1. 它使用IMEM `0x146a8000`，与本项目原版/SM8150下游参考 `0x146bd000`不同。
2. 它的ENDP_RSRC_GRP mask为BIT(0)，而固定下游4.1继承值为0x3。
3. 它的平台数据没有backward_compat；直接复制到本项目6.3.8会得到默认0，
   而已固定下游4.1初始化表为0x39，移植时必须显式审计。
4. 手机的self-loader、固件保留区和SMP2P时序，不代表当前SQ2已adopt的固件状态。

结论：优先“SM8150原版下游为准，6.16移植形状辅助翻译，SQ1原版与SQ2实测裁决”。
本机EFI sysfs确有ACPI20指针 `0xffffe018`，但acpi=off没有导出的IORT文件；
下一步可在受限只读入口核实当前固件IORT，再观察GSI AP环状态。无新MMIO/DMA操作，
不重置MPSS，不换NV，不改高通默认/Mesa回退。
