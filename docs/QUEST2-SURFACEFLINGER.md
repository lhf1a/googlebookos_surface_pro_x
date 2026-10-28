# SurfaceFlinger 实际闭源切换：首轮

2026-10-28。候选combined-v1；这是正常Android init启动的实际SurfaceFlinger及应用框架测试，不是独立producer窗口。

## 结果与恢复

闭源路径在Skia启动shader-cache预热阶段失败，RenderEngine线程SIGABRT：

```text
Failed to create a valid texture. ... [384,384] isProtected:0 isWriteable:0 format:22
```

栈为SkiaRenderEngine::mapExternalTextureBuffer → Cache::primeShaderCache → primeCache → RenderEngineThreaded。首个错误19:59:42，随后约5秒重启一次。格式22为RGBA_FP16；当前native-buffer准入仅接受已验证的RGBA8/YUV，因此拒绝导入。这是明确的格式缺口，不能称为已接管桌面，不能通过禁用cache预热来宣称完整格式支持。

独立60秒watchdog恢复Mesa的两个挂载后启动SurfaceFlinger，SSH重新联通。boot ID保持，SF/system_server PID变化，属于框架重启，不是整板重启。Mesa EGL/Vulkan完整hash均复核一致，未改启动镜像或userdata。后台Trusty/TPM服务原有崩溃不能归因为本次图形失败。

本轮原始证据在忽略目录out/quest2-gpu/combined-probe：sf-trial-start.txt、sf-trial-live.txt（重启期间SSH超时，未取得live maps）、sf-trial-after.txt、sf-trial-diagnostics.txt。不能把缺失的live maps当作完整渲染成功证明。时间限定内12次SF abort后自动恢复已验证；新版watchdog增加两次PID变化提前回退，只完成语法检查，尚未再次设备测试。

## 有界临时切换脚本

`scripts/test-quest2-surfaceflinger.sh --start`从retained initramfs root运行。preflight检查screen-only、正常Mesa哈希、Android启动状态、图形硬件属性、14库清单及文件数量。retained root与Android可共享mount namespace，身份依据各自fs根inode区别，不能要求namespace一定不同。

复制候选到独立带token目录，建立freedreno加载链接，再复制脚本至retained /tmp；先启动nohup watchdog并检查进程存活/命令行，随后停止SF、在PID1根下临时bind EGL目录/Vulkan HAL、重新启动SF。init的SF onrestart会影响zygote，网络可能暂时断开。watchdog不依赖Android框架或SSH会话，最多60秒后回退；这不能恢复内核/GPU整板死锁。

rollback核对同一boot ID和token路径，独占回退锁，只在目标与本trial源dev/inode相同的情况下卸载；覆盖了bind后尚未记录mount ID的异常窗口。核对恢复的两个Mesa哈希再启动SF，保留state/logs供审计。候选仅存在/data的私有实验目录及临时挂载中，正常启动包保持。不要重复未经修改的失败候选。

## 下一步：真实FP16布局

`build-quest2-color-info.sh`生成只分配/读metadata的工具，不提交GPU。正常Mesa环境已实测384×384四种格式，components均来自StandardMetadata/PlaneLayouts；modifier=0，offset=0，hsub/vsub=1，plane width/height均384。

| Android格式 | DRM fourcc | step / stride | 分量(type: bit offset/width) |
| --- | --- | --- | --- |
| RGBA8=1 | AB24=0x34324241 | 32 / 1536 | R1024:0/8、G2048:8/8、B4096:16/8、A1073741824:24/8 |
| RGBX8=2 | XB24=0x34324258 | 32 / 1536 | R1024:0/8、G2048:8/8、B4096:16/8；无alpha分量 |
| BGRA8=5 | AR24=0x34325241 | 32 / 1536 | B4096:0/8、G2048:8/8、R1024:16/8、A1073741824:24/8 |
| RGBA_FP16=22 | AB4H=0x48344241 | 64 / 3072 | R1024:0/16、G2048:16/16、B4096:32/16、A1073741824:48/16 |

FP16的plane/fd均1179648字节；8位格式为589824字节。证据out/quest2-gpu/color-info/device-metadata.txt。旧yuv-info要求2–3平面，不能用于单平面FP16诊断，其YUV_METADATA_FAIL不代表FP16分配失败。

后续需在metadata解析中保留并严格核对components、step/fourcc及真实边界，给mapper准入与getter统一字节/像素步长，再独立验证FP16 GPU采样/写入与RGBX/BGRA语义。当前只完成分配和布局探测，尚未修改适配器或证明这些格式GPU可用。
