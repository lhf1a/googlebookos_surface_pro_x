# DRM/MSM 通用提交效率：v5 暂停检查点

本开发分支保存当前优化进度，不晋升发布 pins、不恢复暂停目标、不操作设备。
通用机制不识别应用包名，不关闭隐式同步，不依据某台设备的身份允许或拒绝启动。
当前内核路线仍是 Linux 6.18.60-sq2-android1 的 DRM/MSM；KGSL 仅作设计参考，
没有移植 KGSL，也没有官方闭源驱动同场景 A/B 结论。

## 保存的源码

`patches/drm-msm-submit-efficiency-v5.patch` 是五个文件的完整累计补丁：

| 文件/符号 | 当前机制 |
| --- | --- |
| msm_gem.c / update_lru_active、msm_gem_pin_obj_locked | 已在 pinned LRU 的 BO 不重复重挂；每次 pin 引用仍保留 |
| msm_gem.c / lookup_vma | 先选择 GPUVM 分组，再取该组 VMA；不引入跨生命周期指针缓存 |
| msm_gem.c / msm_gem_pin_vma_locked | mapped、pages 和 WILLNEED 均有效才走已有映射快路；地址检查及原错误路径保留 |
| msm_gem_submit.c / submit_lookup_objects | 每批最多 32 个描述符；批量读取失败回到标量检查，保留错误顺序 |
| sched_main.c / drm_sched_job_add_dependency_ref | 在 reservation 锁下借用已有 fence，只在新增/替换依赖时取 job 引用；公开 owned API 不变 |
| drm_exec.c、drm_exec.h / DRM_EXEC_OBJECTS_REF_HELD | 仅 legacy submit 显式启用借用 tracked-object 引用；contended/prelocked 引用仍由 drm_exec 持有 |
| msm_gem_submit.c / submit_lock_objects | v5 预留 VM reservation + BO 数量，UINT_MAX 饱和，不更改分配失败处理 |
| msm_gem_submit.c / submit_bo_lookahead、submit_prefetch_resv、submit_prefetch_object | v5 前看 8 个对象，只读预取；不读 fence-list 内容，不改变遍历、锁定和同步顺序 |

借用引用的必要条件：lookup 持有全部 BO，错误路径先 exec_fini 再 retire；异步 retire
必须先锁定 BO 才释放其引用，queue/context 保活 VM reservation。VM_BIND 不启用该选项。
不把共享缓冲的字节比例当对象数或 NO_IMPLICIT 资格；不删外部依赖、回调、错误或超时。

## 已观察结果与未完成项

### v4：已有性能证据

同一通用提交客户端、CPU6、1996800 kHz、15000 BO、150 次计时、10 次 warmup，
每个 depth 各三轮，implicit sync 开启、commands=0。每轮 160 个 fence 完成，faults=0；
频率策略最终恢复到 schedutil / 825600–3148800 kHz。

| depth | v3 CPU 平均 us | v4 CPU 平均 us | 降低 |
| --- | ---: | ---: | ---: |
| 1 | 10498.106 | 9014.640 | 14.13% |
| 4 | 10278.703 | 8923.049 | 13.19% |

这测量的是通用提交 CPU 开销，不是 GPU 渲染吞吐或 FPS。
v4 游戏采样 8.019613 秒：462 个完整 ioctl、386 个非空提交，平均 15995.8 BO，
每非空提交 scheduled CPU 16.077 ms。阶段约为 lookup 1.469、lock 2.772、fini 1.507、
dependency 2.771、VMA/pin 3.034、LRU 1.886、attach 2.575 ms；push 约 0.006 ms。
24 项 trace loss、16 项 probe miss 均为 0，收尾已验证。
另一个无探针窗口 RHI 占单核 96.69%（kernel 83.20%），GPU client 执行占比
26.4%–26.9%，频率驻留均值约 415 MHz。CPU 提交瓶颈仍存在，不能称已解决。
不同跑图窗口不能直接当严格同场景帧率对照。

### v5：已构建并启动，性能尚未采样

原主机实际源码 ASan/UBSan：每个版本 660 个遍历场景、9 个 submit 错误场景、
58 个 exec 引用场景；容量溢出、尾部边界、NO_IMPLICIT、错误顺序、pin/unpin、
重试、重复对象、分配恢复、unlock 时 retire 均通过定向检查。
这些是实际函数配合模拟内核原语，不是完整硬件生命周期或并发压力证明。

| 相同输入与优化门槛 | BASELINE 原版 | MODIFIED v5 | ROLLBACK 原版副本 |
| --- | ---: | ---: | ---: |
| 15000 BO 额外 tracking growth 调用 | 1 | 0 | 1 |
| reservation 预取提示计数 | 0 | 29984 | 0 |
| 优化门槛 exit | 1 | 0 | 1 |
| 660 场景的语义检查 | PASS | PASS | PASS |

旧版 exit 1 是未达到新增优化门槛，不是语义检查失败。v4 parent 的容量/预取门槛同样
为 1/0、exit 1。避免一次增长调用不等于证明原来每次都实际复制内存。
既有 v4 引用检查为 tracked get/put 15000/15000 → 0/0 → 15000/15000，
映射重复检查 15000 → 0 → 15000；回滚副本与原件 hash 相等，完整补丁重应用逐字节一致。

严格 MODPOST 构建成功、stderr 为空。v5 包相对 v4 **仅 MSM 模块记录变化**，
其余有效记录和 metadata 不变，drm_exec/gpu-sched 沿用 v4，私有尾成员原样保持。
既有 SQ2Nat 已正常重启一次，原 SQ2QDbg 未变；无额外启动项、无热卸 provider。

| 产物/实际载入 note | SHA256 |
| --- | --- |
| v5 MSM 模块 | 7ee05cae59584d2f4b0fc7f8f042c359c27dcebc480a631b915fae24f5578ef4 |
| v5 MSM resident note | 90adca013cc7f6d99babad904fa62ec26480a4264c7b8a3c666d81768b443f9a |
| v5 initramfs | 94b3f6bf3e2630c5d7045d82f2420d7d62ab2c4399e9ba39520fdaf954b302ce |
| drm_exec 模块 | 2749c8ed942088e2a6c270994b4423a40d6292209d8e708c8a8d867be5e1a72e |
| gpu-sched 模块 | de48b3802cb8515c8383f154f5e4c13c16b0efb5e2f71221e8ac3503a1a3d4bd |

最后确认 boot 为 f029ca42-df94-4ede-8ad2-96a3891afc4f，Android boot_completed=1，
SF/HWC running，**audioserver 当时为 restarting，尚未复核**。首次三次 SSH 超时后第四次
成功，不把轮询时长当启动耗时，也不称所有服务健康。用户随后暂停目标。
v5 固定频率对照及游戏采样均未执行，不能把 v4 的 13%–14% 收益记在 v5 上。
历史双 context 所有权压力测试曾卡死设备，原因未闭环；禁止无新设计直接重跑。
full_fence_lifecycle_verified / full_hwc_verified / full_stack_complete 均不因此变为 true。

## 可重跑的离线源码检查

保存的 checker 从显式传入的原始源码读五个文件，向**全新输出目录**重建 v5 与 v4
副本；从实际代码提取函数运行主机测试并执行副本回滚/重应用。原源码只读。
fixture hash 仅标识本次历史测试输入，不接入通用构建、设备身份白名单或启动准入。

在 Linux/WSL、安装 Python 3 / patch / clang 及 ASan/UBSan 的环境运行：

```sh
python3 scripts/check-gpu-submit-efficiency.py \
  --kernel-source "$KERNEL_SOURCE" --output "$NEW_TEST_OUTPUT"
python3 -m unittest discover -s tests -p test_gpu_submit_efficiency.py
```

`--prepare-only` 仅重建，不声称运行测试。输出 RESULT.json、VERIFICATION.txt、
源文件及可执行 ROLLBACK.sh。测试原语为 mock；没有设备连接、驱动安装或重启逻辑。
v4→v5 增量另存 tests/fixtures/gpu-submit-efficiency-v5/v4-to-v5.patch。
所有原始角色文件及先前日志继续保存在 ignored 本地目录，没有用新测试记录替代旧角色。

保存本分支时已从原始源码重新运行该入口，返回 0：VMA 220053 次检查、描述符 3328
场景、映射 3456 场景、exec 58 场景、v5 遍历 660 场景及错误/引用组均按各版本预期
通过，ASan/UBSan 无报告；五文件重建与回滚/重应用逐字节匹配，原始源码未变。
检查入口自身的 8 项 unittest 也通过。本轮没有新增任何设备测试或性能结论。

### 构建边界

补丁未自动加入标准 compile/full-image/pins。保存分支不等于把未测 v5 升为默认发布版。
重新构建应从**保留已验收 DPU/eDP 修改的匹配源码副本**开始，而不是以 stock MSM 覆盖它们。
在内核根目录对副本应用完整补丁；完整内核构建可直接使用匹配修改后的 drm_exec.h。
原外部模块构建使用 include/drm/drm_exec.h overlay，仅对 msm_gem_submit.o 和 drm_exec
编译单元 preinclude；不能把它全局 preinclude 导致 pr_fmt 重定义。
沿用配套配置、工具链、Module.symvers 和严格 MODPOST，不绕过模块 ABI/依赖错误。
累计部署必须包括配套 msm/drm_exec/gpu-sched；从已运行 v4 到 v5 才可以只换 MSM。
热卸 GPU/display provider 禁止。当前请求只保存代码，未重新构建、打包或部署内核。

## 续接（仅在用户明确恢复后）

1. 先复核音频服务是否仍 restarting，不把它自动归因于 GPU，也不盲目重启服务。
2. 复用旧同一 submit-bench 二进制和 controlled-benchmark-input.sh，直接测 v5；
   对照已记录的 v4 六轮，不再重复旧版部署；临时频率必须用既有 trap 恢复。
3. 进入跑图后用已按 v5 二进制绑定的 submit-cost-v6-input.sh；不要复用旧 probe 偏移。
   再做无探针 CPU/GPU 窗口；确认 loss/miss、清理及服务健康，才能更新性能结论。

本地证据根为 ignored `out/ww-audio-gpu-20261110/`。四角色位于其
`buffer-mechanism-v1/` 子目录：MODIFIED_FILE.c（及 submit/scheduler/exec/header 补充）、
DIFF_FILE.patch、VERIFICATION.txt、ROLLBACK.sh。该旧 rollback 的 initramfs 模式恢复的是
**优化前原包，不是 v4**；不能因恢复工作就调用。索引 COMBINED_RESULT.json 已根据实有
启动记录修正为 v5/paused，COMBINED_V5_RESULT.json 保留未测性能与音频健康未确认边界。
原始 trace、私有库/包、凭据和失败证据只留本地，不提交。
