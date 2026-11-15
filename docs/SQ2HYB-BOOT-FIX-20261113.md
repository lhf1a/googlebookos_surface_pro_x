# SQ2Hyb 启动修复（2026-11-13）

本轮目标是修复既有 SQ2Hyb 启动故障，不恢复其他 KGSL/SDE 开发。
原内核、DTB、EFI、启动参数及默认 Mesa 入口保持；未重建内核、未提交/推送。
私有载荷、身份尾成员及原始设备日志均保留 ignored out，不纳入 Git。

## 三个已证实问题

1. 原 initramfs 尾部 6010 字节 universal-image storage delta 覆盖 native SSD 路径，
   强制要求 SPX-IMAGE.txt，但既有安装使用 SQ2-SSD-ID.txt 与固定分区布局。
   移除此错误叠加层，恢复实际 native SSD 选择与 Android super 挂载；
   不伪造 marker、不改分区。原 SHA256：
   331b626d844f5934f8e5e688325d4d995f05bc6345cbee1e54b6bae0fd9e6624。
   既有 SSD 不应直接套用依赖新 universal-image 分区/marker 的 disk/esp 产物。

2. sq2_init_backend 调用 QTI gralloc HAL 的 open 参数由 gralloc1 改为 gralloc。
   API 版本不是 HAL 打开名称。真实原/修/回滚 NULL-handle probe 为 exit1/0/1；
   修正后返回预期 -EINVAL 而非 backend 初始化失败的 -ENODEV。

3. QTI 对线性 RGB 2880×318 仅分配 3665920 字节，Mesa 实际要求 3686400 字节。
   GetAlignedWidthAndHeight 与两种 GetBufferSizeAndDimensions ABI 的 Hybrid-only
   façade 在 donor 计算/分配前补齐 4 行，保留逻辑 2880×318，实际 scanlines=320；
   不篡改已分配 handle、不绕过 Mesa 大小检查、不伪装 GPU 型号。
   仅已知单层线性 RGB 扩展；UBWC/YUV/未知格式仍委托原 donor。
   原 donor 以新 SONAME 留在依赖库，其余导出照常提供；builder 严格验证原库 SHA。
   不应向已有活跃旧缓冲区的全局命名空间热换布局库。

可复建源码为 probe/graphics/quest2-mesa-layout.c、
scripts/build_mesa_qti_layout.py；scripts/full_image.py 的 hybrid_runtime
已集成构建及安装清单/哈希更新。原 adapter mock 错误亦已修正。

## 验证与边界

- 存储 helper 同输入 BASELINE/MODIFIED/ROLLBACK：
  exit1 FAIL:image-disk-not-found / exit0 SELECTED:nvme0n1, ALIASES:39 /
  exit1 FAIL:image-disk-not-found。此为实际 shell helper 加捕获设备元数据的主机模型。
- 真实 QTI 分配 → AHardwareBuffer → Mesa EGL → GPU 绘制/末端像素回读：
  五种 RGB 格式×四种尺寸，原版/恢复原版各14通过、6失败，修改版20/20通过。
  最终剥离库再次20/20通过。最初 FP16 探针错用 UNSIGNED_BYTE，已改 FLOAT；
  原失败留档，不作为产品缺陷。另45项 host 分支场景、8项 unittest通过。
- 包副本真正执行 ROLLBACK.sh 后与原 SHA 相同，JSON patch 重应用逐字相等；
  独立 gzip/newc 解包确认仅11有效路径改变，原主成员及身份尾成员逐字不变。
  六文件源码 patch 亦实际重建→回滚→重应用逐字核对。
- 前两次测试未完成 Hybrid 启动：第二次 gralloc-open-only 候选 rescueparty 后
  回退默认 Mesa，用户最初“进入桌面”不能作为 Hybrid 验收；该反证保持。
- 最终实际 boot_id 为 abd5c84f-0b1f-444f-b344-e2ebad3aefc0，
  cmdline 指向 EFI/SQ2Hyb，模式 hybrid-mesa-qti，boot_completed=1。
  SurfaceFlinger PID3478、QTI allocator/composer running；
  GLES 为 freedreno FD680 / Mesa26.2.4，实际映射新 adapter、layout façade及 vendor依赖。
  2880×1920 桌面截图已检查，用户明确确认“桌面及操作正常”。
- gscd 缺 TPM、mmd_setup 等其他诊断仍保留；不宣称所有服务、音视频、PM、
  长时稳定性及 full_* 全栈验收完成。

## 产物与恢复

四角色主链绝对目录：
/home/kanoyami/spx_googlebookos/chromebookOS-mesa-a6xx/out/sq2hyb-image-disk-fix-20261113/

- MODIFIED_FILE.cpio.gz：207110677 字节，
  36d80d54c7359616f3cf1d224f628880a89d45e819f76c800f21485849691e24
- DIFF_FILE.json：原归档到修复归档的精确 byte-splice patch。
- VERIFICATION.txt：原始命令、输出、状态码与逐阶段证据。
- ROLLBACK.sh：接受目标副本，以 sibling BASELINE.cpio.gz 恢复原始字节并校验。

旧 storage-only、gralloc-open-only 四角色分别冻结保留。
最终包持久写入设备 EFI/SQ2Hyb/initramfs.cpio.gz 并逐 SHA 校验。
临时 one-shot 已消费；grub.cfg 和默认 Full Mesa 未改。
再次进入 Hybrid 需在菜单选择 Hybrid；不自动提升为默认或发布 pins。
设备 userdata 保留原始及两次中间包备份，避免 ESP 空间不足阻碍回滚。
