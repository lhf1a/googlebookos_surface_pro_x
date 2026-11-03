# 项目标准构建入口

项目标准入口是 `python3 scripts/build.py`，输出**单个 SQ1/SQ2 通用原始磁盘镜像**，不是两份独立镜像。

默认基线（不是必须满足的能力校验项）：
- SQ1：GPU 661MHz；SQ2：GPU 700MHz。
- 两机：原生 60Hz、2.7Gbps × 4，KSU、EL2、首次触摸校准。
- 以 main fee67b2 的匹配内核/驱动和完整957b组件为底；默认项加入最新 `0100429` 的已验证 LTE/Radio 专属增量，Mesa 保持原非 LTE 回退。
- LTE 默认 `--lte on`，需精确私有 Radio04 输入；`--lte off` 保留关闭选择。仅原绑定 SQ2 可激活，SQ1/其他设备不移植 FSG，详见 [LTE 默认基线](LTE-BASELINE.md)。
- 校准识别 045E:0996 和 045E:0C21；按设备/屏幕/描述符保存，不复制另一设备的校准结果。

`--profiles CUSTOM.json` 保留自定义频率配置；`--sealed-input` 选择本地固定输入。
更换内核功能、显示/固件等组件时，保留既有组件构建和 `--rebuild-components`、`--legacy-el1` 路径；需要配套输入及清单，不会假称修改配置文本就改变已编译内核。
文件哈希、模块ABI、存储身份和拒绝误写物理盘属于安全/一致性保护，不是强制采用默认能力的门槛。

```sh
python3 scripts/build.py --template TEMPLATE.img --gnu-efi BUILT_GNU_EFI --lte-bundle PRIVATE_LTE_DIRECTORY --output FRESH_OUTPUT
```

同一镜像包含共用内核/固件、两机DT，以及默认/回退各自跨机型共用的initramfs。两个菜单项为高通默认和Mesa回退；EFI根据SMBIOS的SQ1/SQ2身份路由，未知身份拒绝，不猜测机型。保持SQ1/661和SQ2/700的差别。
这不是已完成双机实测的声明：本轮没有硬件、虚拟机或功能测试，没有部署。镜像带私有实验室凭据，不公开分发。
`build-source-boot.py` 是底层启动包/组件兼容入口；**对外标准整镜像入口只有 scripts/build.py**。
