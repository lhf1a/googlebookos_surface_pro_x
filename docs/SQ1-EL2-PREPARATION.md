# SQ1 EL2 接管：离线准备归档（2026-10-31）

## 状态与许可

已完成签名 TCB 回调路径定位和本地主机构建；**尚未运行 EFI、验证固件接受载荷或启动 EL2 Linux**。用户允许准备独立测试介质，测试前由用户确认现场就绪。不修改默认启动、不自动关闭 Secure Boot、不重启、不写未识别的磁盘。本文不是实机成功报告，也不将阶段 1/2 全部标为完成。

Windows 参考设备为 Surface Pro X / Microsoft SQ1，Windows 22631、UEFI 4.234.140。只读采集显示 HypervisorPresent=true、VBS 状态 2、Secure Boot=true。这些不能证明当前 Linux 引导路径取得 EL2；CPU WMI 的虚拟化标志不能单独否定硬件能力。连接资料和凭据保留在 private/，不提交。

## 离线结果

- 参考 tcblaunch.exe 版本 10.0.22621.6199；完整样本 SHA256 见准备清单。
- 微软公共符号：tcblaunch.pdb GUID `72D3686FBF8403002EE3B786C2B314E6`，RSDS age=2、DBI age=2、info age=3。最初要求 info age=2 的检查失败；修正为记录两者、校验 GUID 和 DBI age，并对照实际指令。
- image base=0x400000：BlSlEntryPoint 0x427488 → SlpQcEntry 0x427150 → SlpEntryPoint 0x427028。
- Qualcomm 分支保存 tz_data 到 context+96，设置 kind=1。初始化/启动参数失败路径调用 SlpEnterEL1 后，0x427100 取 tz_data、0x427104 计算参数 tz_data+0x68、0x427108 取回调 tz_data+0x48、0x42710c 调用回调。
- 因此该签名版本保留 slbounce 需要的回调机制。更早的认证、映射和 MDL 检查仍可能失败，不能据静态代码保证固件接受。
- winload 对照：0x1800c0d58 经 0x18003fcc0 执行 Secure Launch 命令 1→2→4；SMC ID=0xc3000001。0x1800c1178 设置 tz+0x48 回调、tz+0x68 起的保存上下文，与上述布局一致。
- REA 只用于本地分析。目标 0x427028 的反编译返回 No matching function；本结论来自公共符号和实际 GNU 指令反汇编，不冒称 REA 已成功反编译该函数。

候选链：UEFI EL1 → slbounce ExitBootServices hook → 签名 TCB Secure Launch → Qualcomm 失败回调 → tb_entry/HVC1 → EL2 → Linux。固件接受、全 CPU EL2、KVM 和外设均待现场验证。

SC8180X 源码检查：PSCI 使用 SMC；apps/GPU SMMUv2 分别在 0x15000000/0x2ca0000，不能套用 SC8280XP SMMUv3 overlay。qcom-smmu 对 BYPASS 写回进行运行时 quirk 检查；SC8180X 使用 arm_mmu500_reset。remoteproc 接管仍需单独解决；所查 qebspil 没有 SC8180X 支持。未取得固件样本，不声称完成固件审计。不要把 SQ2 高频 DTB 用作 SQ1 基线。

## 提交的必要材料与复现

- `configs/sq1-el2-preparation.json`：源码 pin、输入和本次构建产物哈希；不是不同工具链可复现性保证。
- `scripts/verify-sq1-el2-preparation.py`：只读检查本地样本和 EFI，不执行目标、不连接设备、不写盘。
- 原始日志、PDB、微软二进制及 EFI 保留在忽略目录 `out/virtualization-feasibility-20261031-195101/stages12/`，不进入 Git。新机器需从自己的已授权 Windows 安装取得**相同哈希**样本；不下载来源不明的旧 TCB。

WSL 中安装好本机原有交叉编译工具后，在独立忽略目录检出 slbounce 清单指定提交，执行 `git submodule update --init --recursive` 和 `make -j4`。本次默认构建 exit=0，链接器 RWX LOAD 警告仍保留，不将其隐藏。不启用 ALWAYS_SWITCH、不加入自动启动目录。

在仓库根目录用 WSL Python 运行：

```sh
python3 scripts/verify-sq1-el2-preparation.py out/virtualization-feasibility-20261031-195101/stages12
```

该检查同时验证完整输入 SHA、八处指令、三个 ARM64 EFI 的 PE subsystem/大小/SHA。失败立即退出；通过仅表示离线材料匹配。

## 独立介质/现场测试门槛（尚未执行）

1. 用户确认现场可手动恢复；先保存 BitLocker 恢复材料（不入 Git）、确认供电、设备身份与现有启动恢复路径。
2. 枚举并确认独立 USB 的物理盘、容量、ESP 标记/UUID；不得写工作站系统盘或现有 Android userdata。目前没有指定测试盘，因此未制作物理介质。
3. 准备经过来源/架构核验的 ARM64 EFI shell 和手动选择的测试文件。EFI shell 尚未选定；不创建 startup.nsh、默认引导覆盖或自动加载 slbounce 的目录。
4. 当前 Secure Boot=true，未签名测试程序不能假定可运行。测试启动信任方案需要单独确认；不自动关闭 Secure Boot，不把运行拒绝判为 EL2 不支持。
5. 用户再次确认后才手动从测试介质运行 sltest，并传入核验后的 TCB 绝对 EFI 路径。成功会显示绿线并**故意停住**，需要手动重启；无绿线/重启/挂起记录为未通过或不确定，不反复盲试。
6. sltest 通过后才准备 SQ1 专用最小 Linux/DT 候选，保留原 EL1 救援路径，验证全 CPU 启动级别、KVM 初始化、/dev/kvm 及最小虚拟机。不能将绿线等同于 Android 终端可用；VSOCK、AVF/Secretkeeper 和外设还需后续验证。

Windows SQ1 的 WSL2 安装尝试连接重置，后续安装命令受工具策略阻止；没有绕过，也没有确认新发行版安装成功。

## 来源

- [slbounce](https://github.com/TravMurav/slbounce)（精确提交见清单）
- [Qualcomm Secure Launch](https://github.com/TravMurav/Qcom-Secure-Launch)
- [qebspil](https://github.com/stephan-gh/qebspil)
- [匹配的微软公共符号](https://msdl.microsoft.com/download/symbols/tcblaunch.pdb/72D3686FBF8403002EE3B786C2B314E62/tcblaunch.pdb)

本分支由现有 SQ1 suspend WIP 分支创建，继承其历史，但本次提交不重启休眠实验，不更改 screen_only/nohibernate、默认镜像或已有设备修复。
