#!/system/bin/sh
STATE_DIR="/data/adb/sq2_game_opt"
PID_FILE="$STATE_DIR/daemon.pid"

if [ -f "$PID_FILE" ]; then
    OLD_PID="$(cat "$PID_FILE" 2>/dev/null)"
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
        kill -TERM "$OLD_PID" 2>/dev/null
        sleep 1
    fi
fi

rm -rf "$STATE_DIR"
