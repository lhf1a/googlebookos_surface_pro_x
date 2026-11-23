# 默认界面密度

默认构建通过 `apply-device-info` 在 Android `init` 读取属性前设置 `ro.sf.lcd_density=320`，覆盖底包中的旧默认密度。标准入口 `scripts/build.py` 和统一机型后端都会刷新实际 initramfs 中的助手，可通过 `--dpi 360` 等参数自定义，分辨率和刷新率保持不变。

已有 `userdata` 中通过显示设置或 `wm density` 保存的密度优先，不在每次启动时覆盖用户选择；可执行 `wm density reset` 恢复构建默认值。
