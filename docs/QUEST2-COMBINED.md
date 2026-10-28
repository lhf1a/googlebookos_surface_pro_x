# 同版 Quest 2 GLES / Vulkan 公共库合并

2026-10-28。`combined-v1`为私有namespace候选，未安装到正常启动。以Vulkan v4九库为公共底座，逐字加入GLES fence-prune的五个专属库：EGL、GLES1/2、eglSubDriver、EGL wrapper。没有重新patchelf或混用其他版本donor。

`prepare-quest2-combined.py`固定14项完整SHA256，拒绝已有输出、链接文件与错误输入，复制后重新校验。所有hash见脚本和生成的SHA256SUMS；GLES仍为`1d3ba33ea006962766f631362556ffa1b1e05835ceb733285bfbe37e580cce77`，Vulkan donor仍为`b087293a0a59ce4d4a3ed25dfc29179a920c2fcab31587de3f8489fb481f3cbb`。

## 同进程验证

`build-quest2-combined.sh`把既有多层合成/窗口交换链探针编译为两个可调用入口，仅在`SQ2_COMBINED_HARNESS`下启用嵌套钩子。egl-first保持两个EGL context存活，内层创建Vulkan device并呈现/读回/销毁，随后外层GLES继续合成；vulkan-first保持Vulkan device/queue存活，内层完整运行GLES后外层继续呈现。两种顺序是独立进程、每种内部同时持有两API对象。不是两独立程序先后运行的替代证据；也不是并行提交压力、跨API共享同一buffer或长期SF测试。

实际同时存活时检查GSL、mapper、AHB adapter、ION、Vulkan donor及平台nativewindow各一个加载实例。小型16K对齐ELF可能多次映射文件page0，统计按全部PT_LOAD共同load bias匹配，不能把三行offset0误报三实例。读取真实GOT并dladdr核对13项：GSL三个ion入口指向私有ION，mapper resolver指向AHB adapter，Vulkan四个SQ2NativeBuffer入口指向同adapter；donor describe和adapter自身四个AHardwareBuffer入口仍绑定平台nativewindow。

最终探针SHA256：`7a03b0bf38d46d069bc11982d5486e2ab2e96403d0cc6615515149eed053af1d`。

| 实测 | 结果 |
| --- | --- |
| 两加载顺序各64×64 GLES150轮 + Vulkan3×12帧 | 全部通过；GPU native-fence与像素oracle保持 |
| 两加载顺序各2880×1920 GLES3轮 + Vulkan3×12帧 | 全部通过 |
| 合并候选compute/image/raster及AHB网格3轮 | 通过 |
| 13项实际绑定、唯一公共库实例 | 两顺序全部通过 |
| QTI回收、native_views/native_bytes/native_failures | 终止归零，sticky failure0 |
| 正常Mesa、boot ID、system_server/SurfaceFlinger PID | 保持 |

原始日志仅在忽略的`out/quest2-gpu/combined-probe/`：`egl-first-stress.txt`、`vulkan-first-stress.txt`、`egl-first-full.txt`、`vulkan-first-full.txt`、`ahb-regression.txt`。早期`*-binding.txt`含修正page0别名统计前的诊断失败，不能替代最终结果。

## 复现

```sh
python3 scripts/prepare-quest2-combined.py
bash scripts/build-quest2-combined.sh
```

同步14库候选至设备`/data/local/tmp/sq2-closed/combined-v1/`；仅同步探针binary/COMBINED_PROBE_SHA256SUMS至`combined-probe/`，不需同步构建object。另同步新版namespace helper/系统清单/runner。retained root运行：

```sh
SQ2_COMBINED=1 SQ2_COMBINED_ORDER=egl-first SQ2_COMPOSITION_CYCLES=150 \
  SQ2_VULKAN_WINDOW_CYCLES=3 \
  sh /proc/1/root/data/local/tmp/sq2-closed/run-quest2-gles-system.sh --combined
```

改为vulkan-first测另一顺序。SQ2_COMBINED模式明确拒绝旧CPU-layout/Direct/readback-policy配置；固定选合并包并同时建立EGL链接和绑定Vulkan HAL。已有RGBA/window/YUV/Vulkan入口也可在该公共库集下回归。保留外部超时、串行测试及screen-only guard。

## 下一步与性能边界

接下来补RGBX/BGRA及DATASPACE等系统格式，并准备可恢复的真实SF切换；正常部署前需关闭逐getter/inc/dec的高频日志。当前未进行同条件性能/功耗基准，不能把旧v819或Direct跑分当作v77合并包成绩。适配增加导入/metadata/引用和零超时依赖查询的CPU工作；shader及命令执行仍为原donor。线性buffer带宽、PBO同步读回回退、原厂与本机调度差异也可能影响吞吐与功耗，不能只按包装函数次数估算百分比。
