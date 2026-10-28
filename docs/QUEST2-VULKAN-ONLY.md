# Mesa EGL/GLES + 闭源 Vulkan

## 范围

按用户最新要求，闭源 EGL/GLES 的代码、上传跟踪和失败证据保留在分支；正常桌面和 GLES 继续使用 Mesa。当前只推进系统 Vulkan 后端替换。不要再用 combined 桌面试验覆盖 EGL/GLES。

当前状态：**普通Vulkan APK窗口已修复并完成120帧，Chrome可用但部分管线仍有错误；Wild Life运行失败。未写启动镜像、未提升默认。**

## 构建与私有复现

在已有固定 combined-v5 输入及 NDK/交叉工具链的 WSL 环境中依次执行：

```sh
python3 scripts/prepare-quest2-vulkan-only.py
python3 scripts/prepare-quest2-vulkan-counter-cleanup.py
bash scripts/build-quest2-vulkan-device.sh
```

两次 prepare 均拒绝已有输出，固定输入哈希；第二次还必须通过实际 ARM64 donor 循环的680组 QEMU 所有权检查。九库清单、生成的 device/namespace 探针及其清单同步至设备私有 `sq2-closed` 对应目录，两个运行脚本也放在该私有目录。凭据/网络信息由操作者提供，不进 Git。

从 retained initramfs root 执行：

```sh
SQ2_VK_COUNTER_CLEANUP=1 SQ2_VK_DEVICE_QUEUES=4 SQ2_VK_DEVICE_CYCLES=150   sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-vulkan-only.sh --device
SQ2_VK_COUNTER_CLEANUP=1   sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-vulkan-only.sh --ahb
SQ2_VK_COUNTER_CLEANUP=1   sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-vulkan-only.sh --window
```

GPU 测试必须串行，共用旧图形测试锁。新 helper 在独立 private mount namespace 中保留原 EGL 目录 FD，把原有 Mesa 文件逐字复制至临时目录后才加入 Vulkan 依赖，拒绝覆盖同名文件，核对 Mesa EGL 哈希且不包含 donor GLES。通过原系统 libvulkan 加载私有 HAL。退出核对原 EGL/Vulkan、boot ID、SF/system_server PID；不改变全局挂载。

设备 create 探针支持1–4个实际公布的队列及1–150次创建/销毁，不提交命令。四队列150轮、AHB完整读回和12帧交换链已通过。细节见 [QUEST2-VK-CREATE-AUDIT.md](QUEST2-VK-CREATE-AUDIT.md)。

## 首次两分钟系统试验

`test-quest2-vulkan-only.sh --start` 做临时全局 Vulkan 切换。复制原 Mesa EGL/GLES 并核对后，加入九个 Vulkan 依赖；仅 Vulkan HAL 接入闭源。独立 retained-root watchdog 在120秒后撤回挂载并重启 zygote，以清除应用候选映射。SurfaceFlinger不切换/重启，独立进程分时截图，启动镜像不修改。watchdog不能处理整板/GPU死锁。

本轮已完成自动回退：busy挂载使用lazy unmount，两个原Mesa哈希恢复，boot ID不变，SF PID不变；zygote/system_server因计划内回退重启。不能称为board crash。默认仍是 Mesa Vulkan。

真实应用结果：

- Chrome GPU进程实际映射候选 Vulkan 及两个独立命名的配套 QGL/GLNEXT。未再出现原 `vkCreateDevice` 的重复释放，但出现大量 `vkCreateGraphicsPipelines = -13`。截图可见正常网页和状态栏，不能据此宣称 Vulkan 渲染通过或排除回退路径。
- 普通 `org.surface.sq2.vulkanprobe` 通过系统加载器识别 Adreno 680，`vkCreateSwapchainKHR = -1000072003`，应用内容黑屏，系统状态栏正常。原生 ImageReader 私有窗口通过不覆盖真实应用窗口；接下来应记录 native-buffer 准入失败的具体阶段与实际元数据/应用命名空间。
- 旧图标/状态栏异常属于闭源 GLES 的独立问题，继续使用 Mesa 可避免把它混入这轮 Vulkan 排查。仍需后续实际应用回归，不能据本轮短测宣布所有 UI 问题已解决。

原始截图、maps、logcat 保存在忽略的 `out/quest2-gpu/kmsg/vulkan-only-*`。不要提交用户网页、地址或原始日志。当前不应重复同一全局候选；先用有界诊断定位 pipeline 和 native image import 两处失败，再重试。

## 普通 APK 外部句柄错误修复

新增 `SQ2_VK_APP_DIAGNOSTICS` 编译开关将有界失败阶段写入 logcat（最多96条），未开启时不添加这些日志。实机普通 APK 明确得到 `read_private stage=mem-open errno=13`，之后 BuildID 验证失败、`CreateImage-native-rejected=-1000072003`。root 私有探针成功不能排除这一应用权限边界。

`SQ2_VK_PROCESS_READ` 候选保留地址去tag、溢出/大小上限及 maps 可读范围检查，用 `process_vm_readv(getpid(),...)` 做有界内核拷贝，遇到错误/短读仍拒绝；未改为未经验证的 memcpy，也未放宽 native 元数据/唯一owner校验。该改动目前通过显式候选编译开关启用，未改变基础构建或设备启动镜像。

构建诊断候选：

```sh
bash scripts/build-quest2-vulkan-app-diag.sh                 # v1: 原proc-mem +诊断
bash scripts/build-quest2-vulkan-app-diag.sh --process-read  # v2: self process read +诊断
bash scripts/build-quest2-vulkan-app-diag.sh --pipeline      # v3: v2 +有界管线/着色器捕获
```

构建拒绝已有目录，复制固定 counter-v1 的其它八库，仅重编 wrapper。v2 wrapper SHA256 为 `f167c19ee43f62afad20b686508e449b6605dd9e008c8202474c22b309648c7d`。`SQ2_VK_APP_DIAGNOSTICS=1|2|3` 选择对应全局120秒试验；私有runner支持1/2。正式持久化之前须生成无诊断候选并验证，不能把 v3 捕获功能放入正常启动。

实测 v2：私有窗口12帧 present/fence/读回及最终引用归零；普通 `org.surface.sq2.vulkanprobe` 由系统 Vulkan loader 加载候选，报告 `READY surface=2880x1920 images=4` 和 `SQ2_VULKAN_WSI_OK frames=120`。截图为正常彩色三角形，状态栏正常。真实试验随后恢复 Mesa，boot保持。这个结果修复先前记录的普通APK交换链失败，不代表所有外部图像场景已完成。

## Chrome 结果的准确边界

用户确认 Chrome 正常打开，截图也显示完整网页。v3采集确认 Chrome ANGLE 请求同一族4队列且创建成功；前8次被捕获的图形管线创建均返回成功。随后部分调用仍报 `Failed to link shaders` / `vkCreateGraphicsPipelines=-13`，还有 `AllocateMemory=-3`。**这应记录为“Chrome可用，部分Vulkan路径有待排查”，不能表述为Chrome整体不可用，亦未证明所有错误均触发GLES回退。**

`quest2-vk-pipeline-trace.h` 只在显式诊断构建接入，保留调用参数/返回值，最多捕获每进程前48个shader（各不超过256KiB）和前8次图形管线元数据；日志含句柄关联编号，不输出地址。SPIR-V写入操作者预先创建的私有实验目录 `/data/local/tmp/sq2-vk-capture`，单文件0600、O_EXCL/O_NOFOLLOW；正常构建无此功能。需要给实验进程创建文件的权限，本次专用目录为1777。捕获文件/日志仅保存在out，不能发布。失败发生较晚，前8次均成功，当前样本不能直接用于失败shader的完整重放；下一轮须按失败条件有界捕获并覆盖模块生命周期，而非简单增加无限日志。

## 本轮 Wild Life：失败，按用户要求提交现状

使用 v2（保留计数器修复与self process read）的120秒系统Vulkan试验，Mesa EGL/GLES保持。已进入Wild Life页并点击运行。采样截图明确显示“基准测试运行失败”，用户同时确认测试失败并要求提交当前结果。**没有取得有效分数或完整场景，不能记作完成跑分或性能结论。**

3DMark进程maps中取得候选 Vulkan和配套GLNEXT加载记录；最后采集的logcat窗口已晚于最初失败，尚不足以归因到某条具体驱动错误。本轮没有启用v819专用Direct选择器，也未创建v77的Direct配置；不要把旧v819 Direct成绩挪用到这次v77系统后端。原始错误页/部分截图、maps和logcat留在out，因计划内zygote重启导致一次SCP中断，缺失文件不作已采集证明。

停止后执行回退并核对原Mesa EGL/Vulkan哈希及同一boot ID；未写启动镜像。按用户要求不继续重跑，仅提交修复、诊断工具与诚实的失败记录。系统替换目标仍未完成。
