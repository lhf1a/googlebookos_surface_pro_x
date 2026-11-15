#!/system/bin/sh
SKIPUNZIP=0

if [ "$ARCH" != "arm64" ]; then
    abort "! Unsupported architecture: $ARCH (arm64 required)"
fi

ui_print "- Installing SQ2 UE/Unity Game Optimizer"

mkdir -p /data/adb/sq2_game_opt
if [ ! -f /data/adb/sq2_game_opt/config.conf ]; then
    cp "$MODPATH/config.conf" /data/adb/sq2_game_opt/config.conf
    ui_print "- Created default /data/adb/sq2_game_opt/config.conf"
else
    ui_print "- Preserved existing /data/adb/sq2_game_opt/config.conf"
fi

chmod 0755 "$MODPATH/bin/sq2-game-optd"
chmod 0755 "$MODPATH/post-fs-data.sh"
chmod 0755 "$MODPATH/service.sh"
chmod 0755 "$MODPATH/uninstall.sh"
chmod 0644 "$MODPATH/zygisk/arm64-v8a.so"
chmod 0644 "$MODPATH/system/vendor/etc/sq2-vulkan-policy.conf"
chmod 0644 /data/adb/sq2_game_opt/config.conf

ui_print "- Installation complete (reboot to activate Zygisk & service)"
