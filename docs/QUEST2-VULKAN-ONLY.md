# Mesa EGL/GLES + 闭源 Vulkan

## 范围

按用户最新要求，闭源 EGL/GLES 的代码、上传跟踪和失败证据保留在分支；正常桌面和 GLES 继续使用 Mesa。当前只推进系统 Vulkan 后端替换。不要再用 combined 桌面试验覆盖 EGL/GLES。

当前状态：**有限私有探针通过，真实应用仍有两个阻断；未写启动镜像、未提升默认。**

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
