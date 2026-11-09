# Surface Pro X Googlebook OS

唯一支持的版本为 **v21-release / Linux 6.18.60-sq2-android1**，采用 mapper5 + 原厂高通 gralloc + 源码 QTI HWC/SDM。

[构建手册](docs/BUILD.md) · [当前进度](docs/HANDOFF.md) · [整栈方案](docs/QUALCOMM-FULL-STACK-PORT.md)

用户已确认v21内置扬声器和Bilibili页面切换正常。清理版保留SoundWire与DPU EBUSY修复、原音频HAL和原OMX，移除随包开发工具、持久采集、桥接逐缓冲日志和Venus诊断开关；对应源码和构建入口同步清理。

```sh
python3 scripts/build.py verify
python3 scripts/build.py package --output out/v21-release-FRESH
python3 scripts/build.py compile --help
```

默认私有输入为 `private/build-inputs/kernel-6.18/EFI/SQ2Nat`，8个EFI文件由 `configs/kernel-baseline.json` 固定。
initramfs SHA256：`e826df919c358a61ff023f6021ba9dae43d166b1d781a3b107ed8320286456a0`。
`package`核验并逐字节复制；`compile`生成独立未验收候选，不能自动替换发布pins。

旧Mesa/诊断版本不再作为并列发布基线；设备现有回退仅用于应急恢复。私有库、固件、凭据和原始日志不纳入Git，当前私有包不能作为匿名通用镜像发布。

内屏仅SDR，Main10继续过滤。完整显示视频栈、长期休眠稳定性及SQ1跨设备验收仍未完成；本轮功能验收不代表这些目标已完成。
