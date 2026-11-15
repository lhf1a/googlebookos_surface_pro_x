#!/system/bin/sh
MODDIR="${0%/*}"
STATE_DIR="/data/adb/sq2_game_opt"
DAEMON="$MODDIR/bin/sq2-game-optd"
PID_FILE="$STATE_DIR/daemon.pid"

while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 2
done

mkdir -p "$STATE_DIR"
if [ ! -f "$STATE_DIR/config.conf" ] && [ -f "$MODDIR/config.conf" ]; then
    cp "$MODDIR/config.conf" "$STATE_DIR/config.conf"
    chmod 0644 "$STATE_DIR/config.conf"
fi

if [ -f "$PID_FILE" ]; then
    OLD_PID="$(cat "$PID_FILE" 2>/dev/null)"
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
        kill -TERM "$OLD_PID" 2>/dev/null
        sleep 1
    fi
fi

exec "$DAEMON" >/dev/null 2>&1 &
