# SQ1 Windows EL2 分析样本

2026-10-31 用户明确授权将已提取的四份 Windows 文件加入提交并推送。
这些是从用户的 SQ1 Windows 参考安装只读提取的原始文件，不是项目构建产物。
目录名称按 tcblaunch.exe 的版本 10.0.22621.6199 标记，不表示四份文件均具有相同版本。

文件：tcblaunch.exe、hvloader.dll、hvaa64.exe、winload.efi。
逐文件 SHA256 见 SHA256SUMS.txt；提交前已与原始提取清单核对。
原采集时 Windows 签名检查显示 Valid / Microsoft；本次只核对字节哈希。

这些微软文件仍受各自原有许可约束，不适用本仓库源码许可，也不表示微软授权再分发。
仅作为本次已授权研究的精确样本归档，不是设备部署包。
没有附带凭据、设备私钥、原始设备日志、内存转储或 PDB。

分析结果及现场测试边界见 docs/SQ1-EL2-PREPARATION.md。
文件存在不代表固件接受、EL2 Linux 或 KVM 已实测成功。
