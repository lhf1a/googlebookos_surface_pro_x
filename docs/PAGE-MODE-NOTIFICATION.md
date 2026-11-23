# 4 KB Page-Agnostic Mode Notification

This mechanism permanently disables Settings' "Using 4KB page-agnostic mode" notification by disabling only `com.android.settings/.development.PageAgnosticNotificationService`, without altering the kernel page size, app 16KB compatibility mode, or other Settings notifications.

## Implementation Flow

`page-mode-notification-setup` is wired into normal `init` and the source/internal-SSD/power-connected packaging flows:

1. After the first Android boot completes, it runs `pm disable` for all currently existing users.
2. Once all succeed, it atomically writes `/data/vendor/sq2-page-mode-notification/done-v1`, with the disabled state persisted by PackageManager; on failure, the marker is omitted so the next boot retries.
3. Subsequent boots only check the marker file and do not re-disable the component.

## Restore

Run the following commands for the target users:

```sh
pm default-state --user 0 com.android.settings/.development.PageAgnosticNotificationService
pm default-state --user 10 com.android.settings/.development.PageAgnosticNotificationService
```

Keeping the completion marker prevents re-disabling on the next boot; deleting the marker allows initialization to run again.
