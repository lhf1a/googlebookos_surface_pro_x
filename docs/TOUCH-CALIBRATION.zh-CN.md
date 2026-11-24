# SQ1/SQ2 统一触摸默认基线

**标准整镜像入口：`python3 scripts/build.py`**。默认 `--touch-repair on`，SQ1/SQ2 Default/Fallback 共用算法，每设备噪声模型不随镜像分发。可通过 `--touch-repair off` 或 `--touch-bundle DIR` 调整，默认组件目录为 `private/build-inputs/touch`。

## 从源码准备默认触摸组件

在具备 ARM64 交叉工具链、Android SDK 和本地签名密钥的 Linux/WSL 环境中运行：

```sh
python3 scripts/build-unified-touch.py \
  --archive IPTSD_3_1_1_TAR_GZ \
  --dependencies IPTSD_SUBPROJECTS \
  --cross-file ARM64_MESON_CROSS_FILE \
  --output private/build-inputs/touch
python3 scripts/build.py --template TEMPLATE_IMG --gnu-efi GNU_EFI --output NEW_OUTPUT
```

组件输出目录须使用新目录以避免混用版本。编译生成 APK、IPTSd、校准助手和 profile 工具，并在输出清单记录源码与载荷哈希。可通过 `SQ2_ANDROID_SDK` 和 `SQ2_CALIBRATION_KEYSTORE` 指定 SDK 与签名密钥。

## 默认校准流程

1. 打开应用 5 秒后自动开始 45 秒静置检测。若屏幕完好且无幽灵触摸问题，可点击“屏幕完好，跳过校准”，经二次确认（提示日后可在「高级设置 → 触摸设置」重新打开或校准）后一键跳过、自动写入 `.noise.disabled` 关闭幽灵触控补丁，并写入 `.prompted` 使后续开机不再弹出校准。
2. 检出静置异常后根据已采样本生成候选，显示“已生成修复方案”，点击“应用修复并复测”后 5 秒开始复测。
3. 静置确认后进行 20 秒单指滑动以检测伴随触点，生成候选、应用并复测确认。
4. 完成 15 次单指点击后，进入坐标和拇指校准。

初测会在 RAM 中暂时停用旧的每设备噪声/连点策略，复测隔离本轮候选；取消、退出或保护超时会恢复旧策略，仅在用户确认后保存。`SPXNOISE1/2` 兼容孤立点/窄行方案，`SPXNOISE3` 按实测电极幅度处理宽区/混合异常而不固定屏幕屏蔽区。格式变更需同步更新协议 7 助手、工具和 IPTSd。
