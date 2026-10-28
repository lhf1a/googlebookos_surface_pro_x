# BGRA/RGBX 与两分钟真实桌面试验

2026-10-28。combined-v5，仍不可设为默认系统；正常启动保持 Mesa。

## 已实现及私有验证

真实 SF 曾因 format5 BGRA 导入 abort。mapper3 现在在原 RGBA8/FP16 之外，严格接受 format5 / AR24 / B0,G8,R16,A24 和 format2 / XB24 / R0,G8,B16 三分量。RGBX 的填充字节不解释为 alpha；不伪装原格式，保留线性/单层/非 protected/分量唯一性/步长和边界检查。私有 C++ metadata helper 仍 hidden，不改变旧 AHB adapter 的内部 ABI。

`prepare-quest2-color.py` 固定全部 v4 输入哈希，逐字保留 13 库，仅换 mapper3 为 `dc0f8d0530154501fa849408d115fb22c14732b4e18a38977a01c791bbb3a0ac`。配套编译器独立名字继续保留。mapper resolver GOT 为 0x1a9a0；双 API 探针已按该位置检查实际函数归属。

新增 `quest2-color.cpp`：九组 RGBA/RGBX/BGRA 源/目标组合，非对称 RGB 和变化 alpha，CPU 填充源与输出哨兵，GPU texelFetch，native fence 后 CPU 逐字节 oracle。RGBX 输入必须采样为 alpha=255，RGBX 输出只比较 RGB，不为 X 字节虚构保证。EGLImage 早释放、texture 持有及 FD 稳定性仍检查。不是 HWUI shader/图标 atlas 的模拟实现。

实机结果：

- 75 项 metadata 正/反布局检查通过。
- 36×36 三轮，13×7 共150轮（1350个格式组合），2880×1920 三轮全过，FD稳定10。
- 按真实状态栏2880×54、任务栏2880×253补跑各三轮，九种组合均过。
- FP16 384²三轮、两种同时持有 GLES/Vulkan 对象的加载顺序、414读回及 YV12 三帧回归通过。
- 最终 mapper 地址引用归零，正常 Mesa hash、boot、框架 PID保持。上述不证明真实桌面正确。

构建：WSL `bash scripts/build-quest2-color.sh`、`bash scripts/build-quest2-v77-ahb.sh`；将后者 mapper 复制到 `out/quest2-gpu/color-probe/libqti-mapper3-color.so` 并设置 RUNPATH `/vendor/lib64/egl`，核对固定hash，再运行 prepare-quest2-color.py。仅部署新 mapper，不把重编的 AHB adapter 混入 v5。重新生成 combined 探针和 namespace helper，清单中全部变化文件须同步。runner 使用 `SQ2_COMBINED=1 SQ2_COMBINED_REVISION=5`，模式 `--color`；宽高/轮数为 SQ2_COLOR_WIDTH/HEIGHT/CYCLES。所有 GPU 测试串行。

## 真实系统证据与失败

第一轮60秒及用户随后要求的120秒试验均暂设 prime_shader_cache=0，试验结束还原原来的空值。两轮 SF 在观察期间 PID 保持，未再发生 format5 导入 abort；自动重启 zygote 已实测。实际 SystemUI maps 中配套私有 GLNEXT/QGL 已出现，先前同名系统 QGL 混用崩溃本轮未复现。不是完整 shader-cache 预热验证。

第二轮按用户要求120秒回退，24次5秒采样 SF PID均保持。独立 screenshot worker 五次 screencap 均返回0，SF PID前后相同；另一次抓取应用黑屏。截图和用户反馈一致：

- 用户选择页中央内容可见，顶部状态栏横条损坏。
- 桌面壁纸正常，多数桌面和任务栏图标为黑色方块；文字仍可辨认。
- 打开应用时出现主体全黑、仅顶部留有损坏内容。

SF dumpsys 明确 Qualcomm Adreno680 / GLES V@0819.0，状态栏/任务栏/launcher 是 CLIENT 合成。状态栏和任务栏原 RGBA format1、modifier0、非压缩；它们的尺寸分别2880×54和2880×253。基础相同尺寸测试通过，不能据此排除真实 usage、上传/内部纹理布局、HWUI atlas/shader 或 SF 采样差异。必须继续分离 producer 与 compositor，不能仅用提高阈值/禁用校验遮蔽错误。

第一轮还捕获 Chrome CrGpuMain 在 donor vkCreateDevice（返回栈位置0xd241c，free调用）Scudo invalid chunk state 崩溃，多次复现。双 API native 探针没覆盖 Chrome 的 device feature/queue/pNext 初始化组合；需要采集/复现实际创建参数与调用路径。不能简单忽略 free 或让 Chrome 永久退软件来宣布闭源替换完成。

Binder 1056768字节大回复日志已追到 GMS NullBinder/SafetyNet，也在 Mesa 回退后出现，当前不能归因于 GPU。第一轮回退边界 system_server DEAD_OBJECT 发生在 SF stop/start 附近，不算自主 board reboot。

两轮均回退原 Mesa GL/Vulkan hash，重建 zygote 后两个 SystemUI 实际 maps 恢复 Mesa；boot ID不变、screen_only=Y、boot_completed=1，未写启动镜像。

## 两分钟回退和证据

`test-quest2-surfaceflinger.sh` 现在默认24×5秒观察（仍在连续SF重启时提前退出）。独立截图子进程五次，每次等待12秒后调用 screencap，timeout8秒加kill2秒；单独进程不阻塞回退。记录时间、SF PID前后、命令结果；需要检查PNG本身，exit0不等于图像正确。

独立kmsg collector最大135秒/2MiB，新hash `83dcdf5d31fdf78f1675cd0c379303096559ada6ffd5e9c42581ec635b106887`，需同步retained /tmp。第二轮实际135秒timeout结束，无容量截断；watchdog24次后busy→lazy detach恢复Mesa。它不能恢复内核死锁，也不构成长期稳定性验证。正常boot不加入这些日志或截图。

原始证据仅在忽略目录 `out/quest2-gpu/kmsg/color-*`；截图在 `color-120-evidence/evidence/`，额外 `screen.icons.png` 为黑屏。不得提交用户截图或原始日志。

仍开放：真实图标/状态栏/应用渲染、Chrome Vulkan创建崩溃、正常预热、完整应用/视频/双API长时验证、持久化和正常重启。完整系统替换未完成。
