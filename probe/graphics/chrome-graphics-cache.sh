#!/bin/sh
# Chrome-only metadata discovery and direct graphics-cache deletion. No data backup.
# BusyBox ash; caller supplies actual package/user state and stops Chrome first.
# No cache content is read, hashed, copied, archived or renamed by this helper.
set -eu
umask 077
MODE=${1:-}; [ "$#" -gt 0 ] && shift
ROOT= STATE= FROM= TO= INSTALLED= UNLOCKED= PACKAGE=com.android.chrome QUIESCED=0
usage() { echo 'chrome-graphics-cache.sh dry-run|apply --android-root ROOT --state-dir STATE --from mesa|qualcomm --to mesa|qualcomm --installed-users FILE --ce-unlocked-users FILE [--package com.android.chrome] [--quiesced]' >&2; exit 64; }
while [ "$#" -gt 0 ]; do
    case "$1" in
        --quiesced) QUIESCED=1; shift;;
        --android-root|--state-dir|--from|--to|--installed-users|--ce-unlocked-users|--package)
            [ "$#" -ge 2 ] || usage
            case "$1" in
                --android-root) ROOT=$2;; --state-dir) STATE=$2;; --from) FROM=$2;; --to) TO=$2;;
                --installed-users) INSTALLED=$2;; --ce-unlocked-users) UNLOCKED=$2;; --package) PACKAGE=$2;;
            esac; shift 2;;
        *) usage;;
    esac
done
case "$MODE" in dry-run|apply) ;; *) usage;; esac
[ "$PACKAGE" = com.android.chrome ] || usage
case "$FROM:$TO" in mesa:mesa|qualcomm:qualcomm) echo 'NOOP backend unchanged; Chrome data not inspected'; exit 0;; mesa:qualcomm|qualcomm:mesa) ;; *) usage;; esac
[ "$MODE" != apply ] || [ "$QUIESCED" = 1 ] || { echo 'ERROR Chrome must be stopped; --quiesced required' >&2; exit 64; }
[ -n "$ROOT" ] && [ -n "$STATE" ] && [ -f "$INSTALLED" ] && [ -f "$UNLOCKED" ] || usage
# Only caller-attested numeric user IDs, never arbitrary paths or shell expressions.
for input in "$INSTALLED" "$UNLOCKED"; do
    awk '!/^(0|[1-9][0-9]*)$/ || seen[$0]++ {bad=1} END {exit bad}' "$input" || usage
done
ROOT=$(readlink -f "$ROOT"); [ -d "$ROOT/data" ] || exit 66
PREFIX=${ROOT%/}
canonical() { [ ! -L "$1" ] && [ "$(readlink -f "$1")" = "$1" ]; }
STATE=$(readlink -f "$STATE") || exit 66
case "$STATE" in
    "$PREFIX/data/user"|"$PREFIX/data/user/"*|"$PREFIX/data/user_de"|"$PREFIX/data/user_de/"*|"$PREFIX/data/data"|"$PREFIX/data/data/"*|"$PREFIX/mnt/expand/"*/user|"$PREFIX/mnt/expand/"*/user/*|"$PREFIX/mnt/expand/"*/user_de|"$PREFIX/mnt/expand/"*/user_de/*|"$PREFIX/data/media/"*/Android/data|"$PREFIX/data/media/"*/Android/data/*|"$PREFIX/mnt/expand/"*/media/*/Android/data|"$PREFIX/mnt/expand/"*/media/*/Android/data/*)
        echo 'ERROR state must be outside application data' >&2; exit 64;;
esac
mkdir -p "$STATE"; canonical "$STATE" || exit 66
TXN=$(mktemp -d "$STATE/chrome-switch.XXXXXXXX")
printf '%s\n' chrome-graphics-cache-v1 > "$TXN/schema"
printf '%s\n' "$MODE" > "$TXN/mode"; printf '%s\n' "$FROM" > "$TXN/from"; printf '%s\n' "$TO" > "$TXN/to"
printf '%s\n' "$ROOT" > "$TXN/android-root"; printf '%s\n' "$PACKAGE" > "$TXN/package"
echo none > "$TXN/cache-backup"; echo prepared > "$TXN/status"
printf '%s\n' "$QUIESCED" > "$TXN/quiesced-caller-attested"
sort -n "$INSTALLED" > "$TXN/installed-users"; sort -n "$UNLOCKED" > "$TXN/ce-unlocked-users"
mkdir "$TXN/lock"; trap 'rmdir "$TXN/lock" 2>/dev/null || :' EXIT
: > "$TXN/report.tsv"; : > "$TXN/target-roots"
PENDING=0; COUNT=0
hex() { printf '%s' "$1" | od -An -v -tx1 | tr -d ' \n'; }
record() { printf '%s\t%s\t%s\n' "$1" "$(hex "$2")" "${3:-}" >> "$TXN/report.tsv" || exit 74; }
pending() { PENDING=1; record "$@"; }
echo "TRANSACTION=$TXN"
is_graphics() {
    case "$1" in GPUPersistentCache|GPUCache|DawnGraphiteCache|DawnWebGPUCache|ShaderCache|GrShaderCache) return 0;; *) return 1;; esac
}
is_system_graphics() {
    case "$1" in com.android.opengl.shaders_cache|com.android.opengl.shaders_cache.multifile|com.android.skia.shaders_cache|com.android.skia.shaders_cache.multifile|mesa_shader_cache|mesa_shader_cache_db|mesa_shader_cache_sf) return 0;; *) return 1;; esac
}
is_historical() {
    case "$1" in .graphics-backend-cache.*|*.before-*|*.bak|*.backup) return 0;; *) return 1;; esac
}
mount_safe() {
    # Refuse a cache or descendant that is itself a mount, including same-device
    # bind mounts. Never let recursive deletion cross into another data owner.
    [ -r /proc/self/mountinfo ] || return 1
    awk -v path="$1" '
      function decode(s) {gsub(/\\040/," ",s);gsub(/\\011/,sprintf("%c",9),s);gsub(/\\012/,sprintf("%c",10),s);gsub(/\\134/,sprintf("%c",92),s);return s}
      {m=decode($5);if(m==path || index(m,path"/")==1)bad=1} END{exit bad}' /proc/self/mountinfo
}
candidate() {
    local path=$1 listing item dev other n=0
    [ -e "$path" ] || [ -L "$path" ] || return 0
    if ! canonical "$path"; then pending CACHE_ALIAS "$path"; return; fi
    if ! mount_safe "$path"; then pending CACHE_MOUNT_BOUNDARY "$path"; return; fi
    if [ ! -f "$path" ] && [ ! -d "$path" ]; then pending CACHE_SPECIAL "$path"; return; fi
    listing=$(mktemp "$TXN/.walk.XXXXXXXX") || exit 74
    if ! find "$path" -xdev \( -name '.graphics-backend-cache.*' -o -name '*.before-*' -o -name '*.bak' -o -name '*.backup' \) -prune -print0 -o -print0 > "$listing"; then
        rm -f "$listing"; pending CACHE_UNREADABLE "$path"; return
    fi
    dev=$(stat -c %d "$path") || { rm -f "$listing"; pending CACHE_STAT_FAILED "$path"; return; }
    while IFS= read -r -d '' item; do
        if [ "$item" != "$path" ] && is_historical "${item##*/}"; then
            rm -f "$listing"; pending CACHE_CONTAINS_HISTORY "$path" 'whole candidate retained'; return
        fi
        if [ -L "$item" ] || { [ ! -f "$item" ] && [ ! -d "$item" ]; }; then
            rm -f "$listing"; pending CACHE_UNSAFE_MEMBER "$path"; return
        fi
        other=$(stat -c %d "$item") || { rm -f "$listing"; pending CACHE_STAT_FAILED "$path"; return; }
        if [ "$other" != "$dev" ]; then rm -f "$listing"; pending CACHE_DEVICE_BOUNDARY "$path"; return; fi
        n=$((n+1))
    done < "$listing"
    rm -f "$listing" || exit 74
    if [ "$MODE" = dry-run ]; then record WOULD_DELETE "$path" "objects=$n"; return; fi
    record DELETE_INTENT "$path" "objects=$n; cache-backup=none"
    # Quiescence is an explicit caller obligation; recheck path/mount immediately
    # before the only destructive operation. No rename/copy fallback is allowed.
    if ! canonical "$path" || ! mount_safe "$path"; then pending CACHE_CHANGED "$path"; return; fi
    if ! rm -rf -- "$path" || [ -e "$path" ] || [ -L "$path" ]; then pending DELETE_FAILED "$path"; return; fi
    COUNT=$((COUNT+1)); record DELETED "$path" "objects=$n; cache-backup=none"
    printf 'PROGRESS\tDELETED\t%s\n' "$(hex "$path")" >&2
}
scan_level() {
    local dir=$1 family=$2 item name
    [ -e "$dir" ] || [ -L "$dir" ] || return 0
    if ! canonical "$dir" || [ ! -d "$dir" ] || ! ls -A "$dir" >/dev/null; then pending CONTAINER_UNAVAILABLE "$dir"; return; fi
    for item in "$dir/"* "$dir/".[!.]* "$dir/"..?*; do
        [ -e "$item" ] || [ -L "$item" ] || continue
        name=${item##*/}
        if is_historical "$name"; then record HISTORICAL_PRESERVED "$item"; continue; fi
        if { [ "$family" = browser ] && is_graphics "$name"; } || { [ "$family" = system ] && is_system_graphics "$name"; }; then
            candidate "$item"; continue
        fi
        # Unknown graphically named direct children remain intact and explicit.
        case "$name" in *[Ss][Hh][Aa][Dd][Ee][Rr]*|*[Vv][Uu][Ll][Kk][Aa][Nn]*|*[Pp][Ii][Pp][Ee][Ll][Ii][Nn][Ee]*|*[Gg][Pp][Uu]*[Cc][Aa][Cc][Hh][Ee]*|*[Dd][Aa][Ww][Nn]*|*[Gg][Rr][Aa][Pp][Hh][Ii][Tt][Ee]*|*[Aa][Nn][Gg][Ll][Ee]*[Cc][Aa][Cc][Hh][Ee]*)
            pending UNKNOWN_GRAPHICS "$item";;
        esac
    done
}
scan_chrome() {
    local app=$1 browser child name
    if ! canonical "$app" || [ ! -d "$app" ]; then pending CHROME_ROOT_UNAVAILABLE "$app"; return; fi
    if ! mount_safe "$app"; then pending CHROME_ROOT_MOUNT_BOUNDARY "$app"; return; fi
    if ! ls -A "$app" >/dev/null; then pending CHROME_ROOT_UNAVAILABLE "$app"; return; fi
    printf '%s\n' "$app" >> "$TXN/target-roots" || exit 74
    record CHROME_ROOT "$app"
    scan_level "$app/cache" system; scan_level "$app/code_cache" system
    browser=$app/app_chrome
    [ -e "$browser" ] || [ -L "$browser" ] || return 0
    if ! canonical "$browser" || [ ! -d "$browser" ]; then pending BROWSER_ROOT_UNAVAILABLE "$browser"; return; fi
    scan_level "$browser" browser
    # Actual browser profiles are immediate children with a regular Preferences
    # marker. Never recurse into historical backups, Downloads, Code Cache or
    # arbitrary source directories looking for cache-like names.
    for child in "$browser/"*; do
        [ -e "$child" ] || [ -L "$child" ] || continue
        name=${child##*/}
        is_historical "$name" && continue
        is_graphics "$name" && continue
        case "$name" in Downloads|'Code Cache') continue;; esac
        if [ -L "$child" ]; then
            case "$name" in SingletonLock|SingletonCookie|SingletonSocket) record PROFILE_LOCK_PRESERVED "$child";;
                *) pending PROFILE_ALIAS "$child";; esac
            continue
        fi
        [ -d "$child" ] || continue
        if [ -f "$child/Preferences" ] && [ ! -L "$child/Preferences" ]; then
            record PROFILE "$child"; scan_level "$child" browser
        else
            case "$name" in Default|Profile*) pending PROFILE_UNVERIFIED "$child";;
                *)
                    for cache_name in GPUPersistentCache GPUCache DawnGraphiteCache DawnWebGPUCache ShaderCache GrShaderCache; do
                        if [ -e "$child/$cache_name" ] || [ -L "$child/$cache_name" ]; then pending PROFILE_UNVERIFIED "$child"; break; fi
                    done;;
            esac
        fi
    done
}
while IFS= read -r user; do
    if ! grep -Fxq "$user" "$TXN/ce-unlocked-users"; then pending CE_DEFERRED "$user" 'installed Chrome user is not unlocked'; continue; fi
    ce_seen=0; roots=0
    for store in "$PREFIX/data" "$PREFIX/mnt/expand/"*; do
        [ -d "$store" ] || continue
        if ! canonical "$store"; then pending STORE_ALIAS "$store"; continue; fi
        for area in user user_de; do
            userroot=$store/$area/$user
            [ -e "$userroot" ] || [ -L "$userroot" ] || continue
            if ! canonical "$userroot" || [ ! -d "$userroot" ]; then pending USER_ROOT_UNAVAILABLE "$userroot"; continue; fi
            [ "$area" != user ] || ce_seen=1
            app=$userroot/$PACKAGE
            [ -e "$app" ] || [ -L "$app" ] || continue
            roots=$((roots+1)); scan_chrome "$app"
        done
        # Exact installed-user Chrome external app root only; never enumerate
        # sibling applications or public-volume users. files/Download is ignored.
        app=$store/media/$user/Android/data/$PACKAGE
        if [ -e "$app" ] || [ -L "$app" ]; then roots=$((roots+1)); scan_chrome "$app"; fi
    done
    [ "$ce_seen" = 1 ] || pending CE_STORAGE_UNAVAILABLE "$user"
    [ "$roots" -ne 0 ] || record NO_CHROME_DATA "$user" 'installed package has no data root'
done < "$TXN/installed-users"
printf '%s\n' "$COUNT" > "$TXN/deleted-cache-count"
if [ "$PENDING" = 1 ]; then echo pending > "$TXN/status"; echo 'RESULT pending; unresolved Chrome items retained'; exit 2; fi
if [ "$MODE" = apply ]; then
    sync || exit 74
    printf 'target=%s\ncoverage=chrome-graphics-only\ncache-backup=none\n' "$TO" > "$TXN/ready.next"
    echo complete > "$TXN/status"; mv "$TXN/ready.next" "$TXN/ready"; sync || { rm -f "$TXN/ready"; exit 74; }
else echo complete > "$TXN/status"; fi
echo "RESULT $MODE-complete chrome-graphics-only cache-backup=none"
