# SQ2 Wild Life 与 Turnip GMEM 状态

## 当前配置：硬件 GMEM 与双 Vulkan 分流

- **内核 GMEM 修复**：Linux 6.18 已合入 `0003 Fix GMEM range on a680`（见 `patches/drm-618-a6xx-upstream/`）。
- **Turnip 26.2.4**：`scripts/patch-sq2-turnip-gmem.py` 与 `scripts/build-sq2-turnip.sh` 默认构建纯硬件 GMEM 的 Mesa Turnip 26.2.4，已移除历史 3DMark sysmem 绕过补丁（保留 `--revert` 与 `--verify-clean` 校验上游原生 GMEM 源码哈希）。
- **双 Vulkan 分流（`--graphics hybrid` / `SQ2Hyb`）**：默认构建在保留硬件 GMEM Turnip 1.3 的同时接入高通 Quest 2 v77 原生 Vulkan 分流路由器。

## 参考

- 上游 [A680 UBWC/macrotile 修正](https://www.mail-archive.com/dri-devel@lists.freedesktop.org/msg504111.html) 要求配套内核 UBWC 查询接口，不能单独写 macrotile 寄存器。
