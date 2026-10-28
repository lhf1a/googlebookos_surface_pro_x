# 实际桌面跟踪：BGRA 缺口与编译器混用

2026-10-28，combined-v3，一次限时对照试验。正常启动仍为Mesa。

## 本次设备证据

本机SurfaceFlinger二进制包含service.sf.prime_shader_cache控制项。本轮仅临时设0，恢复时还原原来的空值；没有把禁用预热作为最终方案。SF成功启动，转储明确为Qualcomm Adreno680、GLES V@0819.0，尚无桌面图层。显式ctl.stop/start不执行init配置的onrestart动作，原system_server当时仍存活，因此手动restart zygote让客户端重新接入。脚本已补该动作，但自动路径尚未再次端到端试验。

应用框架接入后取得两个具体错误：

1. SF在导入36×36、非protected、只读的格式5纹理时abort，重启后复现。格式5为BGRA8888，目前桥接明确拒绝。此次证明BGRA已为实际桌面所需，不能只补FP16就宣称桌面可用。
2. SystemUI和身份登录应用的RenderThread在着色器编译路径SIGSEGV，fault address0x20；栈经libhwui GrGLCompileAndAttachShader → 私有GLES/GLNEXT → **/vendor/lib64/libllvm-qgl.so，BuildID 9d8581ee7cf0ae745ab5fd0ca2a3b5d4**。该BuildID与现有系统vendor库一致；Quest2配套QGL的BuildID为**73d72956e4e42823d8fae35e8347ae70**。这是实际应用命名空间中的版本混用，不能用私有native探针此前的成功排除它。

watchdog记录两次SF PID变化后执行回退，确实遇到/vendor/lib64/egl busy；lazy detach随后恢复了Mesa。boot ID保持，不是整板重启；本次无需用户强制重启。随后再次手动restart zygote清理仍持有旧闭源映射的客户端，两个SystemUI PID在对照间隔中稳定，实际maps均为Mesa EGL，sys.boot_completed=1。仅恢复目录挂载和SF并不足以证明所有客户端已恢复。

75秒kernel采集以timeout正常结束，282条保留记录、5140条过滤记录、约22KB，未触发容量上限。包含DP AUX IRQ错误和调压器DRMS提示；没有本轮Adreno/GMU hangcheck/fault证据。此结论不回溯证明上一轮未知黑屏的原因，也不代表所有GPU负载稳定。

原始证据在忽略目录out/quest2-gpu/kmsg：sf-no-prime-live.txt、sf-no-prime-crash.txt、sf-no-prime-after.txt、no-prime-evidence/*、ui-maps-final.txt、restored-final.txt。原始日志、指针、IP与测试数据不提交。

## 有限独立采集与回退

`build-quest2-kmsg.sh`用AArch64 GNU GCC静态构建collector，SHA256 `16cd33754a793e6d8aa7dbf617f48642ead71b1f3ec42796d0d64d7b8bc25e92`。collector仅从/dev/kmsg独立cursor读取新记录，不清空ring；输出O_EXCL/O_DSYNC、0600，最多75秒/2MiB，带ready/end和超时/容量/overrun状态。保留全部kernel facility及SF/RenderEngine服务记录，过滤其它userspace chatter避免启动噪音过早占满容量。3秒烟雾测试捕获指定标记并按3000ms超时退出；初始未过滤4KiB对照曾提前到limit，不能算作完整时窗。

collector在切换前独立nohup启动；试验必须看到ready和活进程才继续。它不执行GPU命令，不是正常启动日志服务。日志与watchdog进度/maps保存到私有trial/evidence；O_DSYNC尽力缩小丢失窗口，不能保证硬件卡死/断电前最后记录必定落盘。watchdog与collector分离，collector自身I/O问题不应该阻止60秒回退。

脚本`SQ2_SF_PRIME_CACHE=0`只用于本次诊断，记录原值/试验值，回退前恢复；默认不改。当前新增自动zygote restart是依据手工操作实现，语法通过，仍需完整试验确认。watchdog不能修复内核整板死锁。

## 配套编译器隔离候选 combined-v4

`prepare-quest2-private-compiler.py`固定combined-v3全部输入hash，以相同长度字符串替换并重命名：

- libllvm-glnext.so → libq2v77glnext.so；修正GLES/Vulkan的DT_NEEDED以及GLNEXT自己的SONAME/库名字串。
- libllvm-qgl.so → libq2v77qgl.so；修正GLNEXT的动态加载名字和QGL自己的SONAME/库名字串。

仍为14库，不重排ELF、VMA或符号/relocation表，检查所有可执行section和所有非字符串字节保持，保留已有CPU布局、空区域与fence修复。目的为避免按旧同名库解析到系统vendor；不替换系统自身的LLVM文件。

| 修改文件 | 输出SHA256 |
| --- | --- |
| libGLESv2_adreno.so | d7e41532779ba5be94eae207ce3ad6800489a93ba16ccc1fe7b80aa9778995ec |
| libq2v77glnext.so | 17a9cb625c816bf9560489b53e15f20ad356db04ee0f1e83e5fbac03cb0fe3f3 |
| libq2v77qgl.so | 100a97a7c9cfa4b6d554cc1a8960a920de963aeba3b6c3a6bc2ea8d986472652 |
| vulkan-v77-bridge.so | e18b5adf7d213c2058c3ef93028ed6db1af8aa83e398f07681545bff9382dd54 |

v4私有系统加载测试：两种同进程顺序均过，运行时确认两个独立名字的编译器各一个实例；每顺序GLES合成三轮/Vulkan12帧。另FP16三轮、Vulkan compute/image/raster/AHB三轮、414项读回均通过。日志private-compiler-*.txt。尚未在普通HWUI应用命名空间验证修复效果；不能据此宣称SystemUI崩溃已消除。

复现：WSL运行prepare-quest2-private-compiler.py及build-quest2-combined.sh，完整同步combined-v4/探针/清单/runner，私有运行设SQ2_COMBINED=1 SQ2_COMBINED_REVISION=4。此版本仅供私有测试，全局SF脚本尚不接受4。下一步补BGRA/RGBX真实语义，随后在实际应用命名空间确认配套编译器绑定，再做有界桌面试验。完整替换、正常预热、持久化与性能验证仍开放。
