# Surface Pro X 触控构建适配

2026-10-30：新镜像在设备 192.168.5.79 启动后，SPI HID 为
045E:0996。旧 touch-services 仅接受 045E:0C21，退出 2，导致 IPTSd
未启动；不是关闭幽灵触控补丁导致。

已在该设备临时验证：启动匹配增加 0996，输入节点刷新同步接受其虚拟
Touchscreen/Stylus。IPTSd 成功运行；Android EventHub/InputReader 枚举
并启用 event20 触摸屏，用户确认触控正常。未重启、未改启动盘。
原脚本 BASELINE 退出2；恢复原字节的 ROLLBACK 副本退出2；修改版
MODIFIED 退出0，进入 Touchscreen 模式。完整记录留在忽略的 out/touch-0996。

现在源码支持两种设备，且 build-source-boot 显式包含四个触控助手，
不再仅依赖历史底包中已有文件。此适配是基本启动支持，与幽灵触控开关
独立；--ghost-touch-fix off 继续关闭连续性补丁及四格热图补丁。
未扩大热图补丁原有的 045E:0C21/46x68 适用范围。

回归：python3 tests/test_surface_touch_device_ids.py。
本次修改未重建或重新写入设备启动包；当前设备触控仍是临时启动。
重新构建、自然重启及关屏后恢复需要后续验证。
