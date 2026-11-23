# 4 KB 页面通用模式通知

本机制用于永久关闭 Settings 的“正在使用 4KB 页面通用模式”通知，仅禁用 `com.android.settings/.development.PageAgnosticNotificationService`，不修改内核页大小、应用 16KB 兼容模式或 Settings 其它通知。

## 实现流程

`page-mode-notification-setup` 接入正常 `init` 及源码/内部 SSD/电源连接打包流程：

1. 首次 Android 启动完成后，对当前已存在的所有用户执行 `pm disable`。
2. 全部成功后原子写入 `/data/vendor/sq2-page-mode-notification/done-v1`，由 PackageManager 持久保存禁用状态；失败则不写标记，下次启动重试。
3. 后续启动仅检查标记文件，不再重复禁用组件。

## 恢复方法

对目标用户执行以下恢复命令：

```sh
pm default-state --user 0 com.android.settings/.development.PageAgnosticNotificationService
pm default-state --user 10 com.android.settings/.development.PageAgnosticNotificationService
```

保留完成标记可防止下次启动重新禁用；删除标记则允许再次执行初始化。
