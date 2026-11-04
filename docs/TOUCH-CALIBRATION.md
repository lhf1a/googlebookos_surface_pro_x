# SQ1/SQ2 统一触摸默认基线

**项目标准整镜像入口：`python3 scripts/build.py`**。默认 `--touch-repair on`，
SQ1/SQ2 Default/Fallback 共用算法；识别到的每设备噪声模型不随镜像分发。
可使用 `--touch-repair off` 或 `--touch-bundle DIR`，默认组件目录为
`private/build-inputs/touch`（标准入口和 profile 后端一致）。

## 从源码准备默认触摸组件

在已具备 ARM64 交叉工具链、Android SDK 和本地签名密钥的 Linux/WSL 环境中：

```sh
python3 scripts/build-unified-touch.py \
  --archive IPTSD_3_1_1_TAR_GZ \
  --dependencies IPTSD_SUBPROJECTS \
  --cross-file ARM64_MESON_CROSS_FILE \
  --output private/build-inputs/touch
python3 scripts/build.py --template TEMPLATE_IMG --gnu-efi GNU_EFI --output NEW_OUTPUT
```

组件输出目录必须是新目录；已有组件先备份另存，避免混合版本。编译器生成 APK、
IPTSd、校准助手和 profile 工具，输出清单记录全部源码/载荷哈希。SDK及签名密钥
可通过 `SQ2_ANDROID_SDK`、`SQ2_CALIBRATION_KEYSTORE` 指定。
该命令是组件构建，不代替项目标准整镜像入口；完整镜像仍需其它私有输入。

## 默认流程

1. 打开应用5秒后自动开始45秒静置检测。
2. 检出静置异常后据已有样本生成候选，不以帧数、时间分窗、异常面积拒绝。
   显示“已生成修复方案”，点击“应用修复并复测”，5秒后开始复测。
3. 静置确认后进行20秒单指滑动，检测伴随触点，生成候选、应用并复测确认。
   不再要求30次移动、固定路程或单条轨迹跨度。
4. 15次单指点击按采到后1秒并抬手再换点；随后进入坐标和拇指校准。

初测在RAM暂时停用旧的每设备噪声/连点策略，复测隔离本轮候选。取消、退出或
保护期限到达恢复；只有用户确认才保存。坐标参数不清除，通用基础状态修正保留。
SPXNOISE1/2兼容已有孤立点/窄行方案，SPXNOISE3按实测电极幅度处理宽区/混合异常，
不固定屏幕屏蔽区。新格式需要协议7助手、工具和IPTSd一起更新。

## 已验证范围

32项离线触摸测试；含间歇15帧/6异常帧、宽区域模型、消息/回滚、原始信号与
真实触点保护夹具。.94运行时APK/native/helper哈希与协议7核对，原校准文件未变。
这是候选生成与部署证据，**不是**实机缓解效果、完整流程或新整盘双机启动验收。
同幅度真实信号与噪声仍可能不可分辨，需复测及操作者确认；没有原始数据时不虚构策略。
