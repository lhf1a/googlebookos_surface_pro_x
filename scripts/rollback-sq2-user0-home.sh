#!/bin/sh
set -eu
a() { timeout 15 nsenter -t 1 -m -r -- /system/bin/"$@"; }
[ "$(a getprop ro.fw.mu.headless_system_user)" = false ]
[ "$(a am get-current-user)" = 0 ]
a cmd package default-state --user 0 com.google.android.desktop.identity.login/.LoginLauncherActivity
a cmd package set-home-activity --user 0 com.google.android.desktop.identity.login/.LoginLauncherActivity
