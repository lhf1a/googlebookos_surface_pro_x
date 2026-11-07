#!/bin/sh
# BusyBox ash helper: no Python, Android service changes, cache deletion or driver changes.
# Caller must observe the actual backend transition and stop graphics consumers.
# All state/backup files are private local device data, never distributable input.
# Required applets: find -print0, sort -z, readlink -f, stat, sha256sum, od, mktemp,
# mv -nT, mkdir, cat, tr, grep, awk, xxd, chmod, rmdir, touch, sync. Run with `busybox ash`.
set -eu
umask 077
MODE=${1:-}; [ "$#" -gt 0 ] && shift
ROOT= STATE= FROM= TO= TXN= CE= CLASS= CLASS_SNAPSHOT= CLASS_INDEX= QUIESCED=0
usage() {
    echo 'graphics-backend-cache.sh dry-run|apply --android-root ROOT --state-dir STATE --from BACKEND --to BACKEND [--ce-unlocked-users FILE] [--quiesced]' >&2
    echo 'graphics-backend-cache.sh apply|rollback --transaction DIR [--ce-unlocked-users FILE] --quiesced' >&2
    echo 'Optional for dry-run/apply: --classification-file FILE (private evidence, not executable policy)' >&2
    exit 64
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --android-root) ROOT=$2; shift 2;;
        --state-dir) STATE=$2; shift 2;;
        --from) FROM=$2; shift 2;;
        --to) TO=$2; shift 2;;
        --transaction) TXN=$2; shift 2;;
        --ce-unlocked-users) CE=$2; shift 2;;
        --classification-file) CLASS=$2; shift 2;;
        --quiesced) QUIESCED=1; shift;;
        *) usage;;
    esac
done
case "$MODE" in dry-run|apply|rollback) ;; *) usage;; esac
if [ -z "$TXN" ]; then
    case "$FROM:$TO" in mesa:mesa|mesa:qualcomm|qualcomm:mesa|qualcomm:qualcomm) ;; *) usage;; esac
fi
[ "$MODE" != apply ] && [ "$MODE" != rollback ] || [ "$QUIESCED" = 1 ] || {
    echo 'ERROR consumers must be stopped by caller; explicit --quiesced required' >&2; exit 64;
}
if [ -n "$CE" ]; then [ -f "$CE" ] || { echo 'ERROR missing CE state input' >&2; exit 64; }; fi
hex() { printf '%s' "$1" | od -An -v -tx1 | tr -d ' \n'; }
if [ -n "$TXN" ]; then
    TXN=$(readlink -f "$TXN")
    [ -f "$TXN/schema" ] && [ "$(cat "$TXN/schema")" = graphics-cache-v1 ] || usage
    ROOT=$(cat "$TXN/android-root"); FROM=$(cat "$TXN/from"); TO=$(cat "$TXN/to")
    [ "$MODE" != dry-run ] || usage
    [ "$(cat "$TXN/mode")" = apply ] || { echo 'ERROR dry-run is not an applied transaction' >&2; exit 64; }
    [ "$MODE" != apply ] || [ "$(cat "$TXN/status")" != rolled-back ] || { echo 'ERROR rolled-back transaction' >&2; exit 64; }
    if [ "$MODE" = apply ] && [ "$(cat "$TXN/status")" = complete ]; then
        echo "TRANSACTION=$TXN"; echo 'NOOP transition already complete; new backend caches retained'; exit 0
    fi
else
    [ "$MODE" != rollback ] && [ -n "$ROOT" ] && [ -n "$STATE" ] && [ -n "$FROM" ] && [ -n "$TO" ] || usage
    [ "$FROM" != "$TO" ] || { echo 'NOOP backend unchanged; application data not inspected'; exit 0; }
    ROOT=$(readlink -f "$ROOT"); [ -d "$ROOT/data" ] || { echo 'ERROR missing Android data root' >&2; exit 66; }
    mkdir -p "$STATE"; STATE=$(readlink -f "$STATE")
    TXN=$(mktemp -d "$STATE/graphics-switch.XXXXXXXX")
    printf '%s\n' graphics-cache-v1 > "$TXN/schema"
    printf '%s\n' "$ROOT" > "$TXN/android-root"
    printf '%s\n' "$FROM" > "$TXN/from"; printf '%s\n' "$TO" > "$TXN/to"
    printf '%s\n' "$MODE" > "$TXN/mode"; echo prepared > "$TXN/status"
    mkdir "$TXN/entries"
fi
[ "$FROM" != "$TO" ] || usage
case "$FROM:$TO" in mesa:qualcomm|qualcomm:mesa) ;; *) usage;; esac
PREFIX=${ROOT%/}
mkdir "$TXN/lock" 2>/dev/null || { echo 'ERROR transaction is locked; inspect interrupted writer before recovery' >&2; exit 75; }
trap 'rmdir "$TXN/lock" 2>/dev/null || :' EXIT
ID=${TXN##*/}; REPORT=$(mktemp "$TXN/report.XXXXXXXX")
PENDING="$REPORT.pending"
# A resumed transaction must re-earn readiness after scanning current stores.
[ ! -f "$TXN/ready" ] || mv -nT "$TXN/ready" "$REPORT.previous-ready"
echo "TRANSACTION=$TXN"
printf 'MODE\t%s\nFROM_HEX\t%s\nTO_HEX\t%s\nQUIESCED_CALLER_ATTESTED\t%s\n' "$MODE" "$(hex "$FROM")" "$(hex "$TO")" "$QUIESCED" >> "$REPORT"
report() { printf '%s\t%s\t%s\n' "$1" "$(hex "$2")" "${3:-}" >> "$REPORT"; }
pending() { report "$@"; touch "$PENDING"; }
progress() { printf 'PROGRESS\t%s\t%s\n' "$1" "$(hex "$2")" >&2; }
canonical() {
    # Reject symlinked components rather than follow package aliases outside scope.
    [ ! -L "$1" ] && [ "$(readlink -f "$1")" = "$1" ]
}
fingerprint() {
    local object=$1 work=$2 item kind digest
    mkdir -p "$work"
    if [ -f "$object" ] && [ ! -L "$object" ]; then
        printf 'FILE\n' > "$work/tree"
        stat -c '%a %u %g %s' "$object" >> "$work/tree" || return 1
        sha256sum < "$object" >> "$work/tree" || return 1
    elif [ -d "$object" ] && [ ! -L "$object" ]; then
        (cd "$object" && find . -print0) > "$work/unsorted" 2> "$work/errors" || return 1
        sort -z "$work/unsorted" > "$work/list" || return 1
        : > "$work/tree"
        while IFS= read -r -d '' item; do
            if [ -L "$object/$item" ]; then return 1; fi
            if [ -f "$object/$item" ]; then kind=FILE
            elif [ -d "$object/$item" ]; then kind=DIR
            else return 1; fi
            printf '%s\000%s\000' "$kind" "$item" >> "$work/tree"
            stat -c '%a %u %g' "$object/$item" >> "$work/tree" || return 1
            if [ "$kind" = FILE ]; then sha256sum < "$object/$item" >> "$work/tree" || return 1; fi
        done < "$work/list"
    else return 1; fi
    digest=$(sha256sum < "$work/tree") || return 1
    printf '%s\n' "${digest%% *}"
}
classification_digest() {
    local object=$1 kind=$2 work=$3 digest target
    case "$kind" in
        file-sha256)
            [ -f "$object" ] && [ ! -L "$object" ] || return 1
            digest=$(sha256sum < "$object") || return 1
            printf '%s\n' "${digest%% *}";;
        symlink-target-file-sha256)
            [ -L "$object" ] && canonical "${object%/*}" || return 1
            target=$(readlink -f "$object") || return 1
            [ -f "$target" ] && [ ! -L "$target" ] || return 1
            digest=$(sha256sum < "$target") || return 1
            printf '%s\n' "${digest%% *}";;
        tree-fingerprint-v1)
            [ -d "$object" ] && [ ! -L "$object" ] || return 1
            fingerprint "$object" "$work";;
        *) return 1;;
    esac
}
if [ -n "$CLASS" ] && [ "$MODE" != rollback ]; then
    # Snapshot data before scanning. Never source/eval a classification or use its
    # encoded path for filesystem access; only compare it to a discovered path.
    CLASS_SNAPSHOT=$(mktemp "$TXN/classifications.XXXXXXXX")
    if ! canonical "$CLASS" || [ ! -f "$CLASS" ] || ! cat "$CLASS" > "$CLASS_SNAPSHOT"; then
        pending CLASSIFICATION_INPUT_UNREADABLE "$CLASS"; echo pending > "$TXN/status"; exit 64
    fi
    if ! awk -F '\t' '
        NR == 1 { if ($0 != "graphics-cache-classifications-v1") exit 1; next }
        NF != 5 { exit 1 }
        $1 !~ /^(rebuildable-cache|preserve-source-asset|preserve-application-data|preserve-inactive-backup)$/ { exit 1 }
        $2 !~ /^(file-sha256|tree-fingerprint-v1|symlink-target-file-sha256)$/ { exit 1 }
        $2 == "symlink-target-file-sha256" && $1 != "preserve-source-asset" { exit 1 }
        $3 !~ /^2f[0-9a-f]+$/ || length($3) % 2 { exit 1 }
        $4 !~ /^[0-9a-f]+$/ || length($4) != 64 { exit 1 }
        $5 !~ /^[A-Za-z0-9_.:\/-]+$/ { exit 1 }
        seen[$3]++ { exit 1 }
        END { if (NR == 0) exit 1 }
    ' "$CLASS_SNAPSHOT"; then
        pending CLASSIFICATION_INPUT_INVALID "$CLASS" 'exact five-column typed records required; duplicates rejected'
        echo pending > "$TXN/status"; exit 64
    fi
    classhash=$(sha256sum < "$CLASS_SNAPSHOT")
    report CLASSIFICATION_INPUT "$CLASS_SNAPSHOT" "sha256=${classhash%% *}"
    # Decode path bytes only to hash them, never to access a supplied path. Build
    # the exact lookup index once: ordinary leaves do not re-parse the TSV with
    # awk or hex-encode their full path on every lookup.
    CLASS_INDEX=$(mktemp -d "$TXN/classification-index.XXXXXXXX")
    while IFS="$(printf '\t')" read -r purpose kind pathhex expected evidence || [ -n "$purpose" ]; do
        [ "$purpose" != graphics-cache-classifications-v1 ] || continue
        if ! printf '%s' "$pathhex" | xxd -r -p > "$CLASS_INDEX/key"; then
            pending CLASSIFICATION_INDEX_FAILED "$CLASS_SNAPSHOT" 'hex decoder failed'; echo pending > "$TXN/status"; exit 64
        fi
        keyhash=$(sha256sum < "$CLASS_INDEX/key")
        indexfile="$CLASS_INDEX/${keyhash%% *}"
        if [ -e "$indexfile" ]; then
            pending CLASSIFICATION_INDEX_COLLISION "$CLASS_SNAPSHOT"; echo pending > "$TXN/status"; exit 64
        fi
        printf '%s\t%s\t%s\t%s\t%s\n' "$purpose" "$kind" "$pathhex" "$expected" "$evidence" > "$indexfile"
    done < "$CLASS_SNAPSHOT"
fi
if [ "$MODE" = rollback ]; then
    for entry in "$TXN/entries/"*; do
        [ -d "$entry" ] || continue
        if [ ! -f "$entry/sha256" ]; then report NOT_MOVED "$entry" 'preflight did not complete'; continue; fi
        source=$(cat "$entry/source"); backup=$(cat "$entry/backup"); expected=$(cat "$entry/sha256")
        if [ -e "$source" ] || [ -L "$source" ]; then
            if [ ! -e "$backup" ] && [ "$(cat "$entry/status")" = restored ]; then report ALREADY_RESTORED "$source"; continue; fi
            if [ ! -e "$backup" ] && [ ! -L "$backup" ]; then
                if ! canonical "$source"; then pending UNSAFE_RESTORE_PATH "$source"; continue; fi
                actual=$(fingerprint "$source" "$entry/check-unmoved") || { pending ORIGINAL_UNREADABLE "$source"; continue; }
                if [ "$actual" = "$expected" ]; then echo restored > "$entry/status"; report RESTORED_OR_UNMOVED_VERIFIED "$source" "$actual"; continue; fi
            fi
            pending RESTORE_CONFLICT "$source" 'new data retained; no overwrite'; continue
        fi
        if ! canonical "$backup" || ! canonical "${source%/*}"; then pending UNSAFE_RESTORE_PATH "$source"; continue; fi
        actual=$(fingerprint "$backup" "$entry/check-rollback") || { pending BACKUP_UNREADABLE "$backup"; continue; }
        [ "$actual" = "$expected" ] || { pending BACKUP_HASH_MISMATCH "$backup"; continue; }
        if ! mv -nT "$backup" "$source" || [ -e "$backup" ]; then pending RESTORE_FAILED "$source"; continue; fi
        actual=$(fingerprint "$source" "$entry/check-restored") || { pending RESTORED_UNREADABLE "$source"; continue; }
        [ "$actual" = "$expected" ] || { pending RESTORED_HASH_MISMATCH "$source"; continue; }
        echo restored > "$entry/status"; report RESTORED "$source" "$actual"
    done
    if [ -f "$PENDING" ]; then echo rollback-pending > "$TXN/status"; echo 'RESULT rollback-pending'; exit 2; fi
    echo rolled-back > "$TXN/status"; echo 'RESULT rolled-back'; exit 0
fi
recognized() {
    # Exact cache formats/names, not arbitrary *cache* deletion. Sources in adjacent JSON.
    # A matching basename in files/Downloads/documents is not sufficient proof.
    local relative=${1#"$2"/}
    case "$relative" in cache/*|code_cache/*|.cache/*|app_*/*) ;; *) return 1;; esac
    case "${1##*/}" in
        GPUPersistentCache|GPUCache|DawnGraphiteCache|DawnWebGPUCache|GrShaderCache|ShaderCache|\
        com.android.opengl.shaders_cache|com.android.opengl.shaders_cache.multifile|\
        com.android.skia.shaders_cache|mesa_shader_cache|mesa_shader_cache_db|mesa_shader_cache_sf) return 0;;
        *) return 1;;
    esac
}
candidate() {
    local source=$1 guard_kind=${2:-} guard_hash=${3:-} entry backup actual after guard_actual
    progress CACHE_PREFLIGHT "$source"
    if ! canonical "$source"; then pending UNSAFE_CACHE_PATH "$source"; return; fi
    entry=$(mktemp -d "$TXN/entries/cache.XXXXXXXX") || { pending JOURNAL_CREATE_FAILED "$source"; exit 74; }
    if [ "${entry%/*}" != "$TXN/entries" ] || [ ! -d "$entry" ] || ! canonical "$entry"; then
        pending JOURNAL_DIRECTORY_INVALID "$source"; exit 74
    fi
    backup="${source%/*}/.graphics-backend-cache.$ID.${entry##*/}"
    # classify_unknown() is called in an if condition: ash can suppress set -e
    # through that function chain. Recovery metadata needs explicit checks.
    if ! { printf '%s\n' "$source" > "$entry/source" && printf '%s\n' "$backup" > "$entry/backup"; }; then
        pending JOURNAL_PATH_WRITE_FAILED "$source"; exit 74
    fi
    actual=$(fingerprint "$source" "$entry/check-before") || { echo unprocessed > "$entry/status"; pending CACHE_UNREADABLE_OR_SPECIAL "$source"; return; }
    if [ -n "$guard_kind" ]; then
        guard_actual=$(classification_digest "$source" "$guard_kind" "$entry/classification-check") || guard_actual=
        if [ "$guard_actual" != "$guard_hash" ]; then
            echo unprocessed > "$entry/status"; pending CLASSIFICATION_CHANGED_BEFORE_MOVE "$source"; return
        fi
        if ! { printf '%s\n' "$guard_kind" > "$entry/classification-digest-kind" &&
               printf '%s\n' "$guard_hash" > "$entry/classification-sha256" &&
               [ "$(cat "$entry/classification-digest-kind")" = "$guard_kind" ] &&
               [ "$(cat "$entry/classification-sha256")" = "$guard_hash" ]; }; then
            pending JOURNAL_GUARD_WRITE_FAILED "$source"; exit 74
        fi
    fi
    if ! { printf '%s\n' "$actual" > "$entry/sha256" && echo prepared > "$entry/status" &&
           [ "$(cat "$entry/source")" = "$source" ] && [ "$(cat "$entry/backup")" = "$backup" ] &&
           [ "$(cat "$entry/sha256")" = "$actual" ] && [ "$(cat "$entry/status")" = prepared ]; }; then
        pending JOURNAL_PREPARE_WRITE_FAILED "$source"; exit 74
    fi
    if [ "$MODE" = dry-run ]; then report WOULD_MOVE "$source" "$actual"; progress CACHE_WOULD_MOVE "$source"; return; fi
    if [ -e "$backup" ] || [ -L "$backup" ]; then pending BACKUP_COLLISION "$backup"; return; fi
    # Persist the complete recovery journal and source fingerprint BEFORE rename.
    # A process/signal trap cannot repair a board reboot or power interruption.
    if ! sync; then pending JOURNAL_SYNC_FAILED "$source"; exit 74; fi
    progress CACHE_JOURNAL_SYNCED "$source"
    # Same-parent rename preserves ownership, SELinux/xattrs and fscrypt context;
    # no cross-filesystem fallback copy and no recursive delete.
    if ! mv -nT "$source" "$backup" || [ -e "$source" ]; then pending MOVE_FAILED "$source"; return; fi
    if ! sync; then pending RENAMED_BACKUP_SYNC_FAILED "$backup"; exit 74; fi
    progress CACHE_RENAME_SYNCED "$source"
    after=$(fingerprint "$backup" "$entry/check-after") || { pending MOVED_BACKUP_UNREADABLE "$backup"; return; }
    [ "$actual" = "$after" ] || { pending MOVED_HASH_MISMATCH "$backup"; return; }
    # Keep the already durable prepared record intact until the replacement is
    # complete; never truncate it in place while publishing moved status.
    if ! { echo moved > "$entry/status.next" && [ "$(cat "$entry/status.next")" = moved ] &&
           mv -fT "$entry/status.next" "$entry/status" && [ "$(cat "$entry/status")" = moved ] &&
           report MOVED "$source" "$actual"; }; then
        pending MOVED_JOURNAL_WRITE_FAILED "$source"; exit 74
    fi
    if ! sync; then pending MOVED_JOURNAL_SYNC_FAILED "$source"; exit 74; fi
    progress CACHE_MOVED_SYNCED "$source"
}
classify_unknown() {
    local path=$1 purpose kind pathhex expected evidence actual work keyhash indexfile
    CLASS_COVER=0
    [ -n "$CLASS_SNAPSHOT" ] || return 1
    keyhash=$(printf '%s' "$path" | sha256sum)
    indexfile="$CLASS_INDEX/${keyhash%% *}"
    [ -f "$indexfile" ] || return 1
    IFS="$(printf '\t')" read -r purpose kind pathhex expected evidence < "$indexfile"
    if [ "$(hex "$path")" != "$pathhex" ]; then
        pending CLASSIFICATION_INDEX_PATH_MISMATCH "$path"; return 0
    fi
    if [ "$kind" != symlink-target-file-sha256 ] && ! canonical "$path"; then
        pending CLASSIFICATION_UNSAFE_PATH "$path"; return 0
    fi
    work=$(mktemp -d "$TXN/classification-check.XXXXXXXX")
    actual=$(classification_digest "$path" "$kind" "$work") || {
        pending CLASSIFICATION_UNREADABLE_OR_TYPE_MISMATCH "$path" "$evidence"; return 0;
    }
    if [ "$actual" != "$expected" ]; then
        pending CLASSIFICATION_HASH_MISMATCH "$path" "expected=$expected; actual=$actual; evidence=$evidence"; return 0
    fi
    report CLASSIFIED "$path" "purpose=$purpose; $kind=$actual; evidence=$evidence"
    case "$purpose" in
        rebuildable-cache)
            candidate "$path" "$kind" "$expected"
            [ ! -d "$path" ] || CLASS_COVER=1;;
        preserve-inactive-backup)
            # Only this independently proven, whole-tree-pinned inactive backup
            # may exclude descendants. A suffix alone never reaches this branch.
            report PRESERVED_INACTIVE_BACKUP "$path" "$evidence"
            [ ! -d "$path" ] || CLASS_COVER=1;;
        preserve-source-asset)
            # A source directory classification covers its container only.
            # Descendants remain scanned: it cannot conceal unknown/known caches.
            report PRESERVED_SOURCE_ASSET "$path" "$evidence";;
        preserve-application-data)
            # Independently identified settings/databases/downloads are not
            # source code and must not be renamed as compiled graphics caches.
            # As with source containers, all descendants remain scanned.
            report PRESERVED_APPLICATION_DATA "$path" "$evidence";;
    esac
    return 0
}
unknown_graphics() {
    local path=$1 kind work digest
    if [ -d "$path" ]; then kind=tree-fingerprint-v1; else kind=file-sha256; fi
    work=$(mktemp -d "$TXN/unknown-evidence.XXXXXXXX")
    digest=$(classification_digest "$path" "$kind" "$work") || {
        pending UNKNOWN_GRAPHICS_UNREADABLE "$path" 'no classification or readiness'; return;
    }
    # Collecting a digest is not classification. Keep the object pending so a
    # reviewer must identify its purpose from evidence before any later apply.
    pending UNKNOWN_GRAPHICS_CANDIDATE "$path" "$kind=$digest; evidence-dir-hex=$(hex "$work"); no format/provenance classification"
}
scan_app() {
    local app=$1 listing path name covered= resolved relative
    progress APP_BEGIN "$app"
    if ! canonical "$app"; then
        resolved=$(readlink -f "$app") || resolved=
        printf '%s\t%s\n' "$(hex "$app")" "$(hex "$resolved")" >> "$REPORT.app-aliases"
        report APP_ALIAS_NOT_FOLLOWED "$app" 'will require target independently covered by discovered application roots'; return
    fi
    listing=$(mktemp "$TXN/app-list.XXXXXXXX")
    if ! find "$app" -name '.graphics-backend-cache.*' -prune -o -print0 > "$listing" 2> "$listing.stderr"; then
        pending APP_SCAN_INCOMPLETE "$app" 'find stderr saved'; return
    fi
    report APP_DISCOVERED "$app" 'unclassified application data retained'
    hex "$app" >> "$REPORT.apps-scanned"; printf '\n' >> "$REPORT.apps-scanned"
    hex "$app" >> "$REPORT.dirs-scanned"; printf '\n' >> "$REPORT.dirs-scanned"
    while IFS= read -r -d '' path; do
        [ "$path" != "$app" ] || continue
        if [ -n "$covered" ]; then case "$path" in "$covered/"*) continue;; *) covered=;; esac; fi
        [ -e "$path" ] || [ -L "$path" ] || continue
        if [ -L "$path" ]; then
            # An individually inspected source-file link (for example an APK)
            # is not a hidden directory. Only explicit content-pinned evidence
            # may retain it; built-in live cache names cannot be overridden.
            if ! recognized "$path" "$app" && classify_unknown "$path"; then continue; fi
            relative=${path#"$app"/}
            case "$relative" in cache|cache/*|code_cache|code_cache/*|.cache|.cache/*|app_*)
                resolved=$(readlink -f "$path") || resolved=
                printf '%s\t%s\n' "$(hex "$path")" "$(hex "$resolved")" >> "$REPORT.directory-aliases"
                report GRAPHICS_LINK_NOT_FOLLOWED "$path" 'requires independently scanned target';;
                *)
                    name=$(printf '%s' "${path##*/}" | tr '[:upper:]' '[:lower:]')
                    case "$name" in
                        *shader*|*vulkan*|*pipeline*|*gpu*cache*|*dawn*|*graphite*|*angle*cache*)
                            pending UNKNOWN_GRAPHICS_LINK "$path" 'target not read without explicit format evidence';;
                        *) report LINK_PRESERVED "$path" 'outside recognized graphics cache contexts';;
                    esac;;
            esac
            continue
        fi
        if [ -d "$path" ]; then hex "$path" >> "$REPORT.dirs-scanned"; printf '\n' >> "$REPORT.dirs-scanned"; fi
        if recognized "$path" "$app"; then
            [ ! -d "$path" ] || covered=$path
            candidate "$path"; continue
        fi
        # Positive evidence can identify hash-named binary cache leaves too.
        # Match every discovered object, not just the conservative name heuristic;
        # builtin live-cache handling above still cannot be overridden.
        if classify_unknown "$path"; then
            [ "$CLASS_COVER" != 1 ] || covered=$path
            continue
        fi
        name=$(printf '%s' "${path##*/}" | tr '[:upper:]' '[:lower:]')
        case "$name" in
            *shader*|*vulkan*|*pipeline*|*gpu*cache*|*dawn*|*graphite*|*angle*cache*)
                unknown_graphics "$path";;
        esac
    done < "$listing"
    progress APP_END "$app"
}
scan_user() {
    local userroot=$1 kind=$2 user=$3 store=$4 app resolved
    if [ "$kind" = CE ] && { [ -z "$CE" ] || ! grep -Fxq "$user" "$CE"; }; then
        pending CE_DEFERRED "$userroot" "user=$user; unlock evidence absent; block affected app launch until resumed"; return
    fi
    if [ -L "$userroot" ]; then
        resolved=$(readlink -f "$userroot") || resolved=
        case "$resolved" in "$store/"*) report USER_STORAGE_ALIAS "$userroot" "user=$user canonical=$(hex "$resolved")"; userroot=$resolved;;
            *) pending USER_STORAGE_ALIAS_OUTSIDE_STORE "$userroot"; return;; esac
    fi
    if [ ! -d "$userroot" ] || ! canonical "$userroot"; then pending USER_STORAGE_UNAVAILABLE "$userroot" "user=$user"; return; fi
    if ! ls -A "$userroot" > /dev/null 2>&1; then pending USER_STORAGE_UNREADABLE "$userroot" "user=$user"; return; fi
    report USER_DISCOVERED "$userroot" "user=$user storage=$kind"
    touch "$REPORT.users-observed"
    for app in "$userroot/"*; do
        [ -e "$app" ] || [ -L "$app" ] || continue
        if [ -d "$app" ] || [ -L "$app" ]; then scan_app "$app"; fi
    done
}
for store in "$PREFIX/data" "$PREFIX/mnt/expand/"*; do
    [ -d "$store" ] || continue
    if ! canonical "$store"; then pending STORE_ALIAS_NOT_FOLLOWED "$store"; continue; fi
    for area in user user_de; do
        if [ -d "$store/$area" ] && ! ls -A "$store/$area" > /dev/null 2>&1; then
            pending USER_AREA_UNREADABLE "$store/$area"; continue
        fi
        for userroot in "$store/$area/"*; do
            [ -e "$userroot" ] || [ -L "$userroot" ] || continue
            user=${userroot##*/}; case "$user" in ''|*[!0-9]*) pending UNKNOWN_USER_PATH "$userroot"; continue;; esac
            kind=CE; [ "$area" != user_de ] || kind=DE
            if [ "$area" = user_de ] && [ ! -d "$store/user/$user" ]; then
                pending CE_DEFERRED "$store/user/$user" "user=$user; DE exists but CE unavailable"
            fi
            scan_user "$userroot" "$kind" "$user" "$store"
        done
    done
    for userroot in "$store/media/"*; do
        [ -d "$userroot/Android/data" ] || continue
        user=${userroot##*/}; case "$user" in ''|*[!0-9]*) pending UNKNOWN_USER_PATH "$userroot"; continue;; esac
        scan_user "$userroot/Android/data" CE "$user" "$store"
    done
done
# Public removable-volume app stores are not credential-encrypted user stores.
# Scan the actual mounted directory, never /storage symlink mirrors or raw disks.
for store in "$PREFIX/mnt/media_rw/"*; do
    [ -d "$store/Android/data" ] || continue
    if ! canonical "$store"; then pending STORE_ALIAS_NOT_FOLLOWED "$store"; continue; fi
    scan_user "$store/Android/data" PUBLIC "volume-${store##*/}" "$store"
done
# User records identify CE stores that are not yet exposed/unlocked. No fixed IDs.
if [ -d "$PREFIX/data/system/users" ] && ! ls -A "$PREFIX/data/system/users" > /dev/null 2>&1; then
    pending USER_RECORDS_UNREADABLE "$PREFIX/data/system/users"
fi
for userfile in "$PREFIX/data/system/users/"*.xml; do
    [ -f "$userfile" ] || continue
    user=${userfile##*/}; user=${user%.xml}; case "$user" in ''|*[!0-9]*) continue;; esac
    [ -d "$PREFIX/data/user/$user" ] || pending CE_DEFERRED "$PREFIX/data/user/$user" "user=$user; user record exists but CE root absent"
done
# These exact compositor cache formats were observed separately from app data;
# never treat the containing /data/misc or surfaceflinger directory as a cache.
for path in "$PREFIX/data/misc/surfaceflinger/skia_shaders" "$PREFIX/data/misc/surfaceflinger/egl_shaders.multifile"; do
    [ -e "$path" ] || [ -L "$path" ] || continue
    candidate "$path"
done
if [ -f "$REPORT.app-aliases" ]; then
    while IFS="$(printf '\t')" read -r sourcehex targethex; do
        if [ -n "$targethex" ] && [ -f "$REPORT.apps-scanned" ] && grep -Fxq "$targethex" "$REPORT.apps-scanned"; then
            printf 'APP_ALIAS_TARGET_COVERED\t%s\t%s\n' "$sourcehex" "$targethex" >> "$REPORT"
        else
            printf 'APP_ALIAS_UNCOVERED\t%s\t%s\n' "$sourcehex" "$targethex" >> "$REPORT"; touch "$PENDING"
        fi
    done < "$REPORT.app-aliases"
fi
if [ -f "$REPORT.directory-aliases" ]; then
    while IFS="$(printf '\t')" read -r sourcehex targethex; do
        if [ -n "$targethex" ] && [ -f "$REPORT.dirs-scanned" ] && grep -Fxq "$targethex" "$REPORT.dirs-scanned"; then
            printf 'DIRECTORY_ALIAS_TARGET_COVERED\t%s\t%s\n' "$sourcehex" "$targethex" >> "$REPORT"
        else
            printf 'DIRECTORY_ALIAS_UNCOVERED\t%s\t%s\n' "$sourcehex" "$targethex" >> "$REPORT"; touch "$PENDING"
        fi
    done < "$REPORT.directory-aliases"
fi
report COVERAGE "$ROOT" 'all discovered app roots; exact recognized graphics caches only; unknown data retained'
[ -f "$REPORT.users-observed" ] || pending NO_USER_STORAGE_OBSERVED "$PREFIX/data" 'data may not be mounted; cannot certify empty scan'
if [ -f "$PENDING" ]; then echo pending > "$TXN/status"; echo 'RESULT pending; inspect report and resume before affected applications start'; exit 2; fi
echo complete > "$TXN/status"
if [ "$MODE" = apply ]; then
    printf 'target=%s\ncoverage=all-discovered-app-roots/recognized-graphics-only\n' "$TO" > "$TXN/ready"
fi
echo "RESULT $MODE-complete recognized-caches-only"; exit 0
