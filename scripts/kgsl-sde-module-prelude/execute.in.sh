#!/bin/busybox sh
# Generated host-pinned direct-insmod plan. No modprobe retry/cache algorithm.
set -eu
release='@RELEASE@'
lock_owned=0
sticky=0
R=${SQ2_PRELUDE_FIXTURE_ROOT:-}
if [ -n "$R" ]; then
    [ -f "$R/.sq2-module-fixture" ] || exit 2
    case "${SQ2_PRELUDE_COMMAND:-}" in "$R"/*) ;; *) exit 2;; esac
elif [ -n "${SQ2_PRELUDE_COMMAND:-}" ]; then exit 2; fi
run() { if [ -n "$R" ]; then "$SQ2_PRELUDE_COMMAND" "$@"; else "$@"; fi; }
stop() {
    if [ "$lock_owned" = 1 ] && [ "$sticky" = 1 ] && [ -d "${state:-}" ] && [ ! -f "$state/FAILED" ]; then printf '%s\n' "$1 rc=${2:-1}" > "$state/FAILED" || :; fi
    echo "SQ2_MODULE_PRELUDE_STOP reason=$1 rc=${2:-1}" >&2; exit "${2:-1}"
}
check() { value=$(run sha256sum "$2") || stop "hash-read-$2"; [ "${value%% *}" = "$1" ] || stop "hash-$2"; }
plan=$R/etc/module-prelude
tree=$R/lib/modules/$release
state=$R/run/sq2-module-prelude
mode=${1-all}; selection=${2-}
[ "$#" -le 2 ] || stop unexpected-argument 2
case "$mode" in
    all|strict-all|base|tsens|peripherals) [ "$#" -le 1 ] || stop unexpected-argument 2;;
    request)
        [ "$#" = 2 ] && [ -n "$selection" ] || stop request-missing 2
        matched=0
        for valid in @REQUESTS@; do if [ "$valid" = "$selection" ]; then matched=1; fi; done
        [ "$matched" = 1 ] || stop request-not-in-parent 2;;
    *) stop mode 2;;
esac
run mkdir -p "$state" || stop state-directory
if run mkdir "$state/LOCK" 2>/dev/null; then lock_owned=1
else
    if [ -d "$state/LOCK" ]; then echo 'SQ2_MODULE_PRELUDE_BUSY errno=16 no-wait-no-retry' >&2; exit 16; fi
    stop state-lock-create
fi
trap 'run rmdir "$state/LOCK" >/dev/null 2>&1 || :' 0
trap 'exit 143' TERM
trap 'exit 130' INT
[ ! -f "$state/FAILED" ] || stop previous-failure-no-retry
[ "$(run uname -r)" = "$release" ] || stop kernel-release
check '@BUSYBOX_SHA@' "$R/bin/busybox"
check '@NOTES_SHA@' "$R/sys/kernel/notes"
check '@PAYLOAD_SHA@' "$plan/payload.sha256"
check '@PLAN_SHA@' "$plan/PLAN.json"
(cd "$plan" && run sha256sum -c payload.sha256) || stop plan-payload
plan_id='state-v3:@PLAN_SHA@:@PAYLOAD_SHA@'
if [ -f "$state/PLAN_ID" ]; then
    [ "$(run cat "$state/PLAN_ID")" = "$plan_id" ] || stop state-plan-mismatch
else printf '%s\n' "$plan_id" > "$state/PLAN_ID"; fi
for n in modules.dep modules.builtin modules.alias modules.softdep; do
    run cmp "$plan/$n" "$tree/$n" || stop "module-metadata-$n"
done
[ ! -L "$tree" ] || stop module-tree-symlink
[ "$(run readlink -f "$tree")" = "$tree" ] || stop module-tree-path
links=$(cd "$tree" && run find . -type l) || stop module-inventory
links=$(printf '%s\n' "$links" | run sort) || stop module-inventory
observed_links=''
for link in $links; do
    case "$link" in ./build|./source) ;; *) stop module-tree-symlink;; esac
    value=$(run readlink "$tree/${link#./}") || stop module-tree-symlink
    observed_links="$observed_links${link#./}|$value
"
done
expected_links=$(run cat "$plan/non-runtime-links.tsv") || stop non-runtime-links
[ "$(printf %s "$observed_links")" = "$expected_links" ] || stop non-runtime-links
actual=$(cd "$tree" && run find . -type f -name '*.ko') || stop module-inventory
actual=$(printf '%s\n' "$actual" | run sed 's#^./##') || stop module-inventory
actual=$(printf '%s\n' "$actual" | run sort) || stop module-inventory
expected=$(run cut -d ' ' -f 3 "$plan/module-files.sha256") || stop module-inventory
expected=$(printf '%s\n' "$expected" | run sort) || stop module-inventory
[ "$actual" = "$expected" ] || stop module-inventory
(cd "$tree" && run sha256sum -c "$plan/module-files.sha256") || stop module-bytes
if [ "$mode" = strict-all ] && run grep -q '|DISABLED_BY_CONFIG|' "$plan/requests.tsv"; then stop disabled-capability-strict-all; fi
sticky=1
found=0; disabled=''; count=0; request_count=0; loaded_requests=0
while IFS='|' read -r request group requirement kind canonical reason; do
    case "$mode" in request) [ "$request" = "$selection" ] || [ "$canonical" = "$selection" ] || continue;; base|tsens|peripherals) [ "$group" = "$mode" ] || continue;; esac
    found=1
    request_count=$((request_count+1))
    echo "SQ2_MODULE_REQUEST name=$request group=$group requirement=$requirement classification=$kind reason=$reason"
    if [ "$kind" = DISABLED_BY_CONFIG ]; then
        disabled="$disabled $request"
        echo "SQ2_MODULE_DISABLED name=$request reason=$reason capability_available=false"
        continue
    fi
    while IFS='|' read -r owner name type path; do
        [ "$owner" = "$request" ] || continue
        if [ "$type" = BUILTIN ]; then echo "SQ2_MODULE_BUILTIN name=$name"; continue; fi
        file_sha='';note_sha=''
        while IFS='|' read -r identity module_sha module_note; do
            if [ "$identity" = "$name" ]; then file_sha=$module_sha;note_sha=$module_note;break;fi
        done < "$plan/module-identities.tsv"
        [ -n "$file_sha" ] && [ -n "$note_sha" ] || stop "module-identity-missing-$name"
        marker="$plan_id|$file_sha|$note_sha"
        if [ -d "$R/sys/module/$name" ]; then
            [ -f "$state/loaded-$name" ] || stop "unexpected-preloaded-$name"
            [ "$(run cat "$state/loaded-$name")" = "$marker" ] || stop "loaded-marker-mismatch-$name"
            check "$note_sha" "$R/sys/module/$name/notes/.note.gnu.build-id"
            echo "SQ2_MODULE_ALREADY_PRESENT name=$name"; continue
        fi
        if [ -f "$state/attempt-$name" ]; then stop "already-attempted-$name"; fi
        : > "$state/attempt-$name"
        echo "SQ2_MODULE_LOAD name=$name path=$path"
        if run "$R/bin/busybox" insmod "$tree/$path"; then :
        else rc=$?; printf '%s\n' "$name rc=$rc" > "$state/FAILED"; stop "insmod-$name" "$rc"; fi
        [ -d "$R/sys/module/$name" ] || { printf '%s\n' "$name no-sysfs" > "$state/FAILED"; stop "module-not-present-$name"; }
        check "$note_sha" "$R/sys/module/$name/notes/.note.gnu.build-id"
        printf '%s\n' "$marker" > "$state/loaded-$name"
        count=$((count+1))
    done < "$plan/loads.tsv"
    echo "SQ2_MODULE_REQUEST_DONE name=$request state=$kind"
    loaded_requests=$((loaded_requests+1))
done < "$plan/requests.tsv"
[ "$found" = 1 ] || stop request-not-in-parent 2
all=true; [ -z "$disabled" ] || all=false
whole=not_evaluated;case "$mode" in all|strict-all) whole=$all;; esac
echo "SQ2_MODULE_PRELUDE_DONE scope=$mode required_prelude_pass=true optional_disabled=${disabled# } all_requested_capabilities=$whole all_selected_capabilities=$all requested=$request_count loaded_requests=$loaded_requests inserted=$count"
# This is module insertion/presence only, not hardware binding or graphics approval.
