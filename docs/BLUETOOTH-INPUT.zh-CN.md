# SQ2 蓝牙 HID 输入

蓝牙 UHID bus 0005 的 event 节点需要 `root:input 0660` 权限和 `input_device` SELinux 上下文，EventHub 才能正常打开。否则 EventHub 会因 `EACCES` 失败，UHID 就绪超时到期，Android 会禁用 HID 连接策略。

`probe/test2/bluetooth-input-watch` 以 1 秒间隔轮询，仅将蓝牙 UHID bus 0005 的 event 节点以 `root:input 0660` 权限和 `input_device` SELinux 上下文重新发布。它在发布前会再次校验 sysfs 身份标识，按设备身份而非 event 编号跟踪；若所有权或上下文设置失败，则保持节点不动。现有的 Surface 保护套和 IPTSd 路径不受影响。

`bluetooth-framework-boot` 在发布传输之前启动该 watcher，包括在已有桥接的提前返回路径上，并通过 PID/cmdline 检查避免重复启动。该过程不修改传输状态、配对数据库或输入事件。
