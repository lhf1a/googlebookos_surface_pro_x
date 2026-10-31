# 4 KB 页面通用模式通知

用户要求永久关闭 Settings 的“正在使用 4KB 页面通用模式”通知。
只禁用 `com.android.settings/.development.PageAgnosticNotificationService`，
不修改内核页大小、应用16KB兼容模式或Settings其它通知。

`page-mode-notification-setup` 在首次 Android 启动完成后，对当前已存在的
所有用户执行 `pm disable`。全部成功后原子写入
`/data/vendor/sq2-page-mode-notification/done-v1`；后续启动只检查标记，
不再次禁用组件。禁用状态由PackageManager持久保存。失败不写完成标记，
下次启动重试；以后新增用户不自动处理。

助手接入正常init和源码/内部SSD/电源连接打包流程。不会force-stop Settings，
也不会循环取消通知。首次执行前已经发布的通知可能保留至下次启动。

2026-10-31现机已手动禁用用户0和10的组件，并清除了当时通知；
dumpsys确认对应NotificationRecord消失。本仓库一次性启动路径尚未实机重启验证。

恢复时对相应用户执行：

```sh
pm default-state --user 0 com.android.settings/.development.PageAgnosticNotificationService
pm default-state --user 10 com.android.settings/.development.PageAgnosticNotificationService
```

保留完成标记可防止下次启动重新禁用；删除标记则允许再次初始化。
