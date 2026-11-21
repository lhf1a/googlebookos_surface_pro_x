#!/bin/sh
# One-click ADB upgrade for Surface Pro X Googlebook OS (Linux / macOS).
# Supports legacy v0.7.0 (39-partition) and newer 5-partition devices.
# Accepts:
#   - Full disk image (.img.gz / .img)   -> offline full update (super + ESP)
#   - Manifest OTA archive (.tar.gz)     -> manifest-guided delta or full update
# Supports keeping user data (default) or clean install (--wipe-data).
set -eu

ADB="${ADB:-adb}"
SERIAL=""
PACKAGE=""
WIPE_DATA=0
NO_REBOOT=0
DRY_RUN=0
ASSUME_YES=0
FORCE_LEGACY_DELTA=0
VERSION_OVERRIDE=""

usage() {
    cat <<'EOF'
Usage:
  sh tools/upgrade-googlebook-via-adb.sh --package <path> [options]

Options:
  -p, --package PATH     Update package (.img.gz, .img, or OTA .tar.gz) [required]
  -s, --serial SERIAL    Target ADB device serial (when multiple devices connected)
      --adb PATH         Path to adb executable (default: adb)
      --keep-data        Preserve user data, apps, Wi-Fi & touch calibration (default)
      --wipe-data        Clean install: wipe userdata, metadata & misc after updating
      --version VER      Override display version written to pending metadata
      --no-reboot        Stage update & bootstrap ESP without rebooting automatically
      --dry-run          Inspect package and device compatibility only; make no changes
      --force            Allow ESP-only delta archive even if super baseline cannot be verified
  -y, --yes              Skip interactive confirmation prompts (including --wipe-data)
  -h, --help             Show this help

Examples:
  # Upgrade v0.7.0 -> latest while keeping all user data & touch calibration:
  sh tools/upgrade-googlebook-via-adb.sh -p surface-pro-x-6.18-hybrid-generic.img.gz

  # Clean install (wipe user data):
  sh tools/upgrade-googlebook-via-adb.sh -p surface-pro-x-6.18-hybrid-generic.img.gz --wipe-data

  # Fast incremental update on devices already on 5-partition layout:
  sh tools/upgrade-googlebook-via-adb.sh -p surface-pro-x-6.18-hybrid-ota-esp.tar.gz
EOF
}

log() {
    printf '[spx-upgrade] %s\n' "$*"
}

die() {
    printf '[spx-upgrade] ERROR: %s\n' "$*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -p|--package) [ "$#" -ge 2 ] || die "Missing value for $1"; PACKAGE="$2"; shift 2 ;;
        -s|--serial) [ "$#" -ge 2 ] || die "Missing value for $1"; SERIAL="$2"; shift 2 ;;
        --adb) [ "$#" -ge 2 ] || die "Missing value for $1"; ADB="$2"; shift 2 ;;
        --keep-data) WIPE_DATA=0; shift ;;
        --wipe-data) WIPE_DATA=1; shift ;;
        --version) [ "$#" -ge 2 ] || die "Missing value for $1"; VERSION_OVERRIDE="$2"; shift 2 ;;
        --no-reboot) NO_REBOOT=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        --force) FORCE_LEGACY_DELTA=1; shift ;;
        -y|--yes) ASSUME_YES=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown argument: $1 (use --help)" ;;
    esac
done

[ -n "$PACKAGE" ] || { usage >&2; die "--package is required"; }
[ -f "$PACKAGE" ] || die "Package file not found: $PACKAGE"
command -v "$ADB" >/dev/null 2>&1 || die "adb not found: $ADB"
command -v gzip >/dev/null 2>&1 || die "gzip is required"
command -v tar >/dev/null 2>&1 || die "tar is required"
command -v od >/dev/null 2>&1 || die "od is required"
command -v awk >/dev/null 2>&1 || die "awk is required"

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print tolower($1)}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print tolower($1)}'
    else
        die "sha256sum or shasum is required"
    fi
}

file_size_bytes() {
    if stat -c '%s' "$1" >/dev/null 2>&1; then
        stat -c '%s' "$1"
    else
        stat -f '%z' "$1"
    fi
}

adb_cmd() {
    if [ -n "$SERIAL" ]; then
        "$ADB" -s "$SERIAL" "$@"
    else
        "$ADB" "$@"
    fi
}

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/spx-adb-upgrade.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT

# ---- 1. Local Package Inspection ----

detect_header_kind() {
    # Reads first 1024 bytes from stdin; prints gzip | full-img | tar | unknown
    od -An -tx1 -v -N 1024 | awk '
    {
        for (i = 1; i <= NF; i++) b[n++] = tolower($i)
    }
    END {
        if (n >= 2 && b[0] == "1f" && b[1] == "8b") { print "gzip"; exit }
        if (n >= 520 && b[510] == "55" && b[511] == "aa" &&
            b[512] == "45" && b[513] == "46" && b[514] == "49" && b[515] == "20" &&
            b[516] == "50" && b[517] == "41" && b[518] == "52" && b[519] == "54") {
            print "full-img"; exit
        }
        if (n >= 263 && b[257] == "75" && b[258] == "73" && b[259] == "74" &&
            b[260] == "61" && b[261] == "72") {
            print "tar"; exit
        }
        print "unknown"
    }'
}

parse_gpt_stream() {
    # Reads first 34*512 = 17408 bytes (MBR + GPT header + 128*128 entries) from stdin.
    # Outputs: <name>\t<offset_bytes>\t<size_bytes>
    od -An -tx1 -v -N 17408 | awk '
    BEGIN {
        for (i = 0; i < 16; i++) {
            hex[sprintf("%x", i)] = i
        }
    }
    function u8(idx) { return hex[substr(b[idx],1,1)] * 16 + hex[substr(b[idx],2,1)] }
    function u32(idx) { return u8(idx) + u8(idx+1)*256 + u8(idx+2)*65536 + u8(idx+3)*16777216 }
    function u64(idx) { return u32(idx) + u32(idx+4) * 4294967296 }
    {
        for (i = 1; i <= NF; i++) b[n++] = tolower($i)
    }
    END {
        if (n < 1024) exit 1
        if (b[510] != "55" || b[511] != "aa") exit 1
        if (b[512] != "45" || b[513] != "46" || b[514] != "49" || b[515] != "20" ||
            b[516] != "50" || b[517] != "41" || b[518] != "52" || b[519] != "54") exit 1
        entries_lba = u64(512 + 72)
        num_entries = u32(512 + 80)
        entry_size  = u32(512 + 84)
        table_off = entries_lba * 512
        if (entry_size < 128 || num_entries < 1 || table_off + num_entries * entry_size > n) exit 1
        for (e = 0; e < num_entries; e++) {
            base = table_off + e * entry_size
            nz = 0
            for (k = 0; k < 16; k++) if (b[base + k] != "00") { nz = 1; break }
            if (!nz) continue
            flba = u64(base + 32)
            llba = u64(base + 40)
            if (llba < flba) continue
            name = ""
            for (k = 0; k < 36; k++) {
                lo = u8(base + 56 + k * 2)
                hi = u8(base + 56 + k * 2 + 1)
                if (lo == 0 && hi == 0) break
                if (hi == 0 && lo >= 32 && lo <= 126) name = name sprintf("%c", lo)
                else name = name "_"
            }
            printf "%s\t%.0f\t%.0f\n", name, flba * 512, (llba - flba + 1) * 512
        }
    }'
}

log "Inspecting package: $PACKAGE"
PKG_BYTES="$(file_size_bytes "$PACKAGE")"
OUTER_KIND="$(detect_header_kind < "$PACKAGE")"
PKG_KIND="unknown"
case "$OUTER_KIND" in
    full-img) PKG_KIND="full-img" ;;
    gzip)
        INNER_KIND="$( (gzip -dc "$PACKAGE" 2>/dev/null || true) | head -c 1024 | detect_header_kind )"
        case "$INNER_KIND" in
            tar) PKG_KIND="manifest" ;;
            full-img) PKG_KIND="full-img-gz" ;;
        esac
        ;;
esac
[ "$PKG_KIND" != "unknown" ] || die "Unsupported package format (expected .img.gz, .img, or OTA .tar.gz)"

log "Computing package SHA-256 ($PKG_BYTES bytes)..."
PKG_SHA256="$(sha256_file "$PACKAGE")"
log "Package kind: $PKG_KIND | SHA-256: $PKG_SHA256"

SUPER_INCLUDED=0
SUPER_OFF=0
SUPER_BYTES=0
ESP_OFF=0
ESP_BYTES=0
REQ_SUPER_SHA=""
PKG_VERSION=""
ESP_MODE=""

if [ "$PKG_KIND" = "manifest" ]; then
    mkdir -p "$WORK_DIR/meta"
    tar -xzf "$PACKAGE" -C "$WORK_DIR/meta" ota-manifest.kv ota-files.tsv 2>/dev/null || die "Archive is missing ota-manifest.kv or ota-files.tsv"
    while IFS='=' read -r _k _v; do
        _v="$(printf '%s' "$_v" | tr -d '\r')"
        case "$_k" in
            SCHEMA) [ "$_v" = "1" ] || die "Unsupported OTA manifest schema: $_v" ;;
            VERSION) PKG_VERSION="$_v" ;;
            SUPER_INCLUDED) SUPER_INCLUDED="$_v" ;;
            SUPER_BYTES) SUPER_BYTES="$_v" ;;
            REQUIRED_SUPER_SHA256) REQ_SUPER_SHA="$_v" ;;
            ESP_MODE) ESP_MODE="$_v" ;;
        esac
    done < "$WORK_DIR/meta/ota-manifest.kv"
    log "Manifest parsed: version=${PKG_VERSION:-unknown} super_included=$SUPER_INCLUDED esp_mode=${ESP_MODE:-unknown}"
else
    SUPER_INCLUDED=1
    if [ "$PKG_KIND" = "full-img-gz" ]; then
        GPT_TABLE="$( (gzip -dc "$PACKAGE" 2>/dev/null || true) | head -c 17408 | parse_gpt_stream )" || die "Failed to parse GPT from $PACKAGE"
    else
        GPT_TABLE="$(parse_gpt_stream < "$PACKAGE")" || die "Failed to parse GPT from $PACKAGE"
    fi
    SUPER_OFF="$(printf '%s\n' "$GPT_TABLE" | awk -F '\t' '$1=="super"{print $2; exit}')"
    SUPER_BYTES="$(printf '%s\n' "$GPT_TABLE" | awk -F '\t' '$1=="super"{print $3; exit}')"
    ESP_OFF="$(printf '%s\n' "$GPT_TABLE" | awk -F '\t' '$1=="SQ2PROBE"{print $2; exit}')"
    ESP_BYTES="$(printf '%s\n' "$GPT_TABLE" | awk -F '\t' '$1=="SQ2PROBE"{print $3; exit}')"
    [ -n "$SUPER_OFF" ] && [ -n "$SUPER_BYTES" ] || die "Full image GPT missing super partition"
    [ -n "$ESP_OFF" ] && [ -n "$ESP_BYTES" ] || die "Full image GPT missing SQ2PROBE partition"
    PKG_VERSION="${VERSION_OVERRIDE:-full-image}"
    log "GPT parsed: super=${SUPER_BYTES}B @${SUPER_OFF} | SQ2PROBE=${ESP_BYTES}B @${ESP_OFF}"
fi

if [ -n "$VERSION_OVERRIDE" ]; then
    PKG_VERSION="$VERSION_OVERRIDE"
fi

# ---- 2. ADB Connection & Root Verification ----

log "Checking ADB device connection..."
if [ -z "$SERIAL" ]; then
    DEVS="$(adb_cmd devices | awk 'NR>1 && $2=="device"{print $1}')"
    DEV_CNT="$(printf '%s\n' "$DEVS" | awk 'NF{c++} END{print c+0}')"
    [ "$DEV_CNT" -ge 1 ] || die "No authorized ADB device found. Connect Surface Pro X lower USB-C port and enable USB debugging."
    [ "$DEV_CNT" -le 1 ] || die "Multiple ADB devices connected; pass --serial <SERIAL>."
    SERIAL="$(printf '%s\n' "$DEVS" | awk 'NF{print; exit}')"
fi
log "Target ADB device: $SERIAL"

ROOT_MODE=""
UID_DIRECT="$(adb_cmd shell 'id -u' 2>/dev/null | tr -d '\r\n' || true)"
if [ "$UID_DIRECT" = "0" ]; then
    ROOT_MODE="direct"
else
    adb_cmd root >/dev/null 2>&1 || true
    sleep 1
    adb_cmd wait-for-device >/dev/null 2>&1 || true
    UID_AFTER="$(adb_cmd shell 'id -u' 2>/dev/null | tr -d '\r\n' || true)"
    if [ "$UID_AFTER" = "0" ]; then
        ROOT_MODE="direct"
    else
        UID_SU="$(adb_cmd shell "su -c 'id -u'" 2>/dev/null | tr -d '\r\n' || true)"
        if [ "$UID_SU" = "0" ]; then
            ROOT_MODE="su"
        else
            die "Root ADB access required. Grant root permission to Shell in KernelSU Next Manager (or run adb root) and retry."
        fi
    fi
fi
log "Root mode: $ROOT_MODE"

run_root() {
    if [ "$ROOT_MODE" = "direct" ]; then
        adb_cmd shell "$1"
    else
        adb_cmd shell "su -c '$1'"
    fi
}

# ---- 3. Device-side Helper Script ----

cat > "$WORK_DIR/spx-adb-helper.sh" <<'DEVICE_EOF'
#!/system/bin/sh
set -eu

CMD="$1"
shift

is_preserved_esp() {
    case "$1" in
        EFI/BOOT/grub.cfg|SPX-USERDATA-GROWN.txt|SPX-BOOT-SOURCE.txt|SPX-OTA-*|EFI/SPX/LTE/*) return 0 ;;
        *) return 1 ;;
    esac
}

get_blk_bytes() {
    _gb_d="$1"
    if [ -b "$_gb_d" ]; then
        if command -v blockdev >/dev/null 2>&1; then
            _gb_b="$(blockdev --getsize64 "$_gb_d" 2>/dev/null || true)"
            case "$_gb_b" in ''|*[!0-9]*) ;; *) [ "$_gb_b" != "0" ] && { printf '%s\n' "$_gb_b"; return 0; } ;; esac
        fi
        _gb_r="$(readlink -f "$_gb_d" 2>/dev/null || printf '%s' "$_gb_d")"
        _gb_n="${_gb_r##*/}"
        if [ -r "/sys/class/block/$_gb_n/size" ]; then
            _gb_s="$(cat "/sys/class/block/$_gb_n/size")"
            awk -v s="$_gb_s" 'BEGIN { printf "%.0f\n", s * 512 }'
            return 0
        fi
    fi
    echo 0
}

sha256_dev() {
    sha256sum "$1" | awk '{print tolower($1)}'
}

mount_esp() {
    _me_mp="/data/local/tmp/spx-adb-esp"
    mkdir -p "$_me_mp"
    umount "$_me_mp" 2>/dev/null || true
    mount -t vfat -o sync /dev/block/by-name/SQ2PROBE "$_me_mp"
    printf '%s\n' "$_me_mp"
}

umount_esp() {
    _ue_mp="/data/local/tmp/spx-adb-esp"
    sync 2>/dev/null || true
    umount "$_ue_mp" 2>/dev/null || true
    rmdir "$_ue_mp" 2>/dev/null || true
}

backup_sq2nat_if_space() {
    _bk_esp="$1"
    [ -d "$_bk_esp/EFI/SQ2Nat" ] || return 0
    _bk_free_kb="$(df -k "$_bk_esp" | awk 'NR==2{print $4+0}')"
    # Require >= 350 MiB free on ESP before creating backup copy (v0.7.0 2 GiB ESP has > 1.3 GiB free)
    if [ "$_bk_free_kb" -ge 358400 ]; then
        rm -rf "$_bk_esp/EFI/SQ2Bak" 2>/dev/null || true
        cp -r "$_bk_esp/EFI/SQ2Nat" "$_bk_esp/EFI/SQ2Bak" 2>/dev/null || rm -rf "$_bk_esp/EFI/SQ2Bak" 2>/dev/null || true
        sync 2>/dev/null || true
    fi
}

verify_initramfs_has_ota() {
    _vi_initrd="$1"
    [ -f "$_vi_initrd" ] || { echo "ERR:missing_new_initramfs" >&2; return 1; }
    if ! gzip -dc "$_vi_initrd" 2>/dev/null | tr '\0' '\n' | grep -Fq 'bin/spx-ota-stage'; then
        echo "ERR:new_initramfs_missing_spx_ota_stage" >&2
        return 1
    fi
    if ! gzip -dc "$_vi_initrd" 2>/dev/null | tr '\0' '\n' | grep -Fq 'bin/spx-ota-tool'; then
        echo "ERR:new_initramfs_missing_spx_ota_tool" >&2
        return 1
    fi
    return 0
}

copy_esp_atomic() {
    _ca_src="$1"
    _ca_esp="$2"
    _ca_rel="$3"
    _ca_exp_sha="${4:-}"
    if is_preserved_esp "$_ca_rel"; then
        return 0
    fi
    _ca_dst="$_ca_esp/$_ca_rel"
    mkdir -p "${_ca_dst%/*}"
    _ca_tmp="$_ca_dst.spxtmp"
    rm -f "$_ca_tmp" 2>/dev/null || true
    cp "$_ca_src" "$_ca_tmp"
    sync 2>/dev/null || true
    if [ -n "$_ca_exp_sha" ]; then
        _ca_got="$(sha256_dev "$_ca_tmp")"
        [ "$_ca_got" = "$_ca_exp_sha" ] || { rm -f "$_ca_tmp"; echo "ERR:esp_verify_mismatch:$_ca_rel" >&2; return 1; }
    fi
    mv -f "$_ca_tmp" "$_ca_dst"
    sync 2>/dev/null || true
}

write_standard_grub_cfg() {
    _wg_dst="$1"
    _wg_src="${2:-}"
    if [ -n "$_wg_src" ] && [ -f "$_wg_src" ]; then
        cp "$_wg_src" "$_wg_dst.spxtmp" && mv -f "$_wg_dst.spxtmp" "$_wg_dst"
    else
        cat > "$_wg_dst.spxtmp" <<'GRUBEOF'
set timeout=5
set default=spx-single-user

menuentry 'Surface Pro X Linux 6.18 - single user 0' --id spx-single-user {
 chainloader /EFI/SQ2Nat/start.efi
}
menuentry 'Surface Pro X Linux 6.18 - rescue (SSH / Wi-Fi / USB)' --id spx-rescue {
 chainloader /EFI/SQ2Res/start.efi
}
menuentry 'Power off' { halt }
GRUBEOF
        mv -f "$_wg_dst.spxtmp" "$_wg_dst"
    fi
    sync 2>/dev/null || true
}

ensure_grub_boots_sq2nat() {
    _eg_esp="$1"
    _eg_src_cfg="${2:-}"
    _eg_cfg="$_eg_esp/EFI/BOOT/grub.cfg"
    _eg_env="$_eg_esp/EFI/BOOT/grubenv"

    # Clear stale one-shot grubenv (e.g. trial next_entry) so reboot enters SQ2Nat.
    if [ -f "$_eg_env" ]; then
        cp "$_eg_env" "$_eg_env.ota-bak" 2>/dev/null || true
        rm -f "$_eg_env" 2>/dev/null || true
    fi

    mkdir -p "$_eg_esp/EFI/BOOT" 2>/dev/null || true

    if [ ! -f "$_eg_cfg" ]; then
        write_standard_grub_cfg "$_eg_cfg" "$_eg_src_cfg"
        echo "GRUB_ACTION=installed_missing"
        return 0
    fi

    # Preserve Windows dual-boot menus (bootmgfw.efi); only rewrite legacy non-SQ2Nat chainloader if needed.
    if grep -qi 'bootmgfw\.efi' "$_eg_cfg" 2>/dev/null; then
        if ! grep -q '/EFI/SQ2Nat/start\.efi' "$_eg_cfg" 2>/dev/null; then
            cp "$_eg_cfg" "$_eg_cfg.ota-bak" 2>/dev/null || true
            awk '{
                if ($0 ~ /\/EFI\/SQ2/ && $0 !~ /\/EFI\/SQ2Res\//) {
                    sub(/\/EFI\/SQ2[^\/ \t]*\/[^ \t]+\.efi/, "/EFI/SQ2Nat/start.efi")
                }
                print
            }' "$_eg_cfg" > "$_eg_cfg.spxtmp" && mv -f "$_eg_cfg.spxtmp" "$_eg_cfg"
            sync 2>/dev/null || true
            echo "GRUB_ACTION=repaired_dualboot"
        else
            echo "GRUB_ACTION=preserved_dualboot"
        fi
        return 0
    fi

    # Single-OS grub.cfg: preserve only if it already defaults to SQ2Nat (spx-single-user or 0).
    if grep -q '/EFI/SQ2Nat/start\.efi' "$_eg_cfg" 2>/dev/null && \
       tr -d '\r' < "$_eg_cfg" | grep -qE '^[[:space:]]*set[[:space:]]+default=(spx-single-user|"spx-single-user"|'\''spx-single-user'\''|0)[[:space:]]*$'; then
        echo "GRUB_ACTION=preserved_sq2nat"
        return 0
    fi

    # Legacy/trial grub.cfg (e.g. SQ2N19 / SQ2KSU / SQ2AU10): back up and install standard SQ2Nat menu.
    cp "$_eg_cfg" "$_eg_cfg.ota-bak" 2>/dev/null || true
    write_standard_grub_cfg "$_eg_cfg" "$_eg_src_cfg"
    echo "GRUB_ACTION=replaced_legacy_grub"
}

case "$CMD" in
    probe)
        [ -b /dev/block/by-name/super ] || { echo "ERR:missing_super_block" >&2; exit 1; }
        [ -b /dev/block/by-name/SQ2PROBE ] || { echo "ERR:missing_sq2probe_block" >&2; exit 1; }
        _super_sz="$(get_blk_bytes /dev/block/by-name/super)"
        _esp_sz="$(get_blk_bytes /dev/block/by-name/SQ2PROBE)"
        _data_free_bytes="$(df -k /data | awk 'NR==2{printf "%.0f\n", $4 * 1024}')"
        _has_ota_info=0
        _cur_super_sha=""
        if [ -r /etc/spx-ota-info ]; then
            _has_ota_info=1
            _cur_super_sha="$(awk -F= '$1=="SPX_OTA_SUPER_SHA256"{print $2; exit}' /etc/spx-ota-info 2>/dev/null || true)"
        elif [ -r /data/user_de/0/org.surface.advancedcontrol/files/ota-state ]; then
            _has_ota_info=1
            _cur_super_sha="$(awk -F= '$1=="current_super_sha256"{print $2; exit}' /data/user_de/0/org.surface.advancedcontrol/files/ota-state 2>/dev/null || true)"
        else
            for _p in /proc/[0-9]*/root/etc/spx-ota-info; do
                if [ -r "$_p" ]; then
                    _has_ota_info=1
                    _cur_super_sha="$(awk -F= '$1=="SPX_OTA_SUPER_SHA256"{print $2; exit}' "$_p" 2>/dev/null || true)"
                    break
                fi
            done
        fi
        _krel="$(uname -r 2>/dev/null || true)"
        printf 'SUPER_BYTES=%s\n' "$_super_sz"
        printf 'ESP_BYTES=%s\n' "$_esp_sz"
        printf 'DATA_FREE_BYTES=%s\n' "$_data_free_bytes"
        printf 'HAS_OTA_INFO=%s\n' "$_has_ota_info"
        printf 'CURRENT_SUPER_SHA256=%s\n' "$_cur_super_sha"
        printf 'KERNEL=%s\n' "$_krel"
        ;;

    check-staged-pkg)
        _exp_sha="$1"
        _exp_sz="$2"
        if [ -f /data/ota/pending.pkg ]; then
            _cur_sz="$(stat -c '%s' /data/ota/pending.pkg 2>/dev/null || echo 0)"
            if [ "$_cur_sz" = "$_exp_sz" ]; then
                _got="$(sha256_dev /data/ota/pending.pkg)"
                if [ "$_got" = "$_exp_sha" ]; then
                    echo "HIT=$_got"
                    exit 0
                fi
            fi
        fi
        echo "MISS"
        ;;

    stage-pkg)
        _exp_sha="$1"
        mkdir -p /data/ota
        chmod 700 /data/ota
        [ -f /data/local/tmp/spx-ota-upload.pkg ] || { echo "ERR:upload_missing" >&2; exit 1; }
        mv -f /data/local/tmp/spx-ota-upload.pkg /data/ota/pending.pkg
        chmod 600 /data/ota/pending.pkg
        _got="$(sha256_dev /data/ota/pending.pkg)"
        [ "$_got" = "$_exp_sha" ] || { rm -f /data/ota/pending.pkg; echo "ERR:pkg_sha_mismatch:$_got" >&2; exit 1; }
        echo "OK_STAGED=$_got"
        ;;

    apply-manifest)
        _exp_sha="$1"
        _apply_super="$2"
        _wipe_data="$3"
        _version="$4"
        _stage="/data/ota/adb-stage"
        rm -rf "$_stage"
        mkdir -p "$_stage/meta" "$_stage/esp"
        tar -xzf /data/ota/pending.pkg -C "$_stage/meta" ota-manifest.kv ota-files.tsv
        tar -xzf /data/ota/pending.pkg -C "$_stage/esp" payload/esp

        while IFS='	' read -r _act _dst _blob _sha _sz; do
            [ -n "$_act" ] || continue
            if [ "$_act" = "PUT" ]; then
                _got="$(sha256_dev "$_stage/esp/$_blob")"
                [ "$_got" = "$_sha" ] || { rm -rf "$_stage"; echo "ERR:blob_sha_mismatch:$_dst" >&2; exit 1; }
            fi
        done < "$_stage/meta/ota-files.tsv"

        if [ "$_apply_super" = "1" ] || [ "$_wipe_data" = "1" ]; then
            _initrd_blob="$(awk -F '\t' '$1=="PUT" && $2=="EFI/SQ2Nat/initramfs.cpio.gz"{print $3; exit}' "$_stage/meta/ota-files.tsv")"
            if [ -n "$_initrd_blob" ]; then
                verify_initramfs_has_ota "$_stage/esp/$_initrd_blob" || { rm -rf "$_stage"; exit 1; }
            fi
        fi

        _esp="$(mount_esp)"
        trap 'umount_esp; rm -rf "$_stage"' EXIT
        backup_sq2nat_if_space "$_esp"

        while IFS='	' read -r _act _dst _blob _sha _sz; do
            [ -n "$_act" ] || continue
            case "$_act" in
                PUT) copy_esp_atomic "$_stage/esp/$_blob" "$_esp" "$_dst" "$_sha" ;;
                DEL)
                    if ! is_preserved_esp "$_dst"; then
                        rm -f "$_esp/$_dst" 2>/dev/null || true
                    fi
                    ;;
            esac
        done < "$_stage/meta/ota-files.tsv"

        ensure_grub_boots_sq2nat "$_esp" "$_stage/esp/payload/esp/EFI/BOOT/grub.cfg"

        if [ "$_apply_super" = "1" ] || [ "$_wipe_data" = "1" ]; then
            _ptmp="$_esp/SPX-OTA-PENDING.tmp"
            {
                printf 'PACKAGE_REL=ota/pending.pkg\n'
                printf 'PACKAGE_SHA256=%s\n' "$_exp_sha"
                printf 'PACKAGE_KIND=manifest\n'
                printf 'APPLY_SUPER=%s\n' "$_apply_super"
                printf 'WIPE_DATA=%s\n' "$_wipe_data"
                printf 'VERSION=%s\n' "$_version"
            } > "$_ptmp"
            sync 2>/dev/null || true
            mv -f "$_ptmp" "$_esp/SPX-OTA-PENDING"
            sync 2>/dev/null || true
            echo "OK_OFFLINE_STAGED"
        else
            rm -f /data/ota/pending.pkg 2>/dev/null || true
            _rtmp="$_esp/SPX-OTA-RESULT.txt.tmp"
            {
                printf 'STATUS=APPLIED_PENDING_REBOOT\n'
                printf 'VERSION=%s\n' "$_version"
                printf 'PACKAGE_KIND=manifest\n'
                printf 'APPLY_SUPER=0\n'
                printf 'WIPE_DATA=0\n'
                printf 'ERROR=\n'
            } > "$_rtmp"
            sync 2>/dev/null || true
            mv -f "$_rtmp" "$_esp/SPX-OTA-RESULT.txt"
            sync 2>/dev/null || true
            echo "OK_ONLINE_APPLIED"
        fi
        ;;

    extract-esp-on-device)
        _kind="$1"
        _esp_off="$2"
        _esp_len="$3"
        _esp_start="$(awk -v o="$_esp_off" 'BEGIN { printf "%.0f", o + 1 }')"
        rm -f /data/ota/esp-src.fat 2>/dev/null || true
        if [ "$_kind" = "full-img-gz" ]; then
            gzip -dc /data/ota/pending.pkg | tail -c +"$_esp_start" | head -c "$_esp_len" > /data/ota/esp-src.fat
        else
            tail -c +"$_esp_start" /data/ota/pending.pkg | head -c "$_esp_len" > /data/ota/esp-src.fat
        fi
        _got_sz="$(stat -c '%s' /data/ota/esp-src.fat 2>/dev/null || echo 0)"
        [ "$_got_sz" = "$_esp_len" ] || { rm -f /data/ota/esp-src.fat; echo "ERR:esp_extract_size:$_got_sz" >&2; exit 1; }
        echo "OK_ESP_EXTRACTED=$(sha256_dev /data/ota/esp-src.fat)"
        ;;

    apply-full-esp-and-pending)
        _exp_sha="$1"
        _kind="$2"
        _wipe_data="$3"
        _version="$4"
        _esp_src_fat="$5"
        [ -f "$_esp_src_fat" ] || { echo "ERR:missing_esp_src_fat" >&2; exit 1; }

        _src_mp="/data/local/tmp/spx-adb-esp-src"
        mkdir -p "$_src_mp"
        umount "$_src_mp" 2>/dev/null || true
        _loop="$(losetup -f)"
        losetup -r "$_loop" "$_esp_src_fat"
        mount -t vfat -o ro "$_loop" "$_src_mp"

        _esp="$(mount_esp)"
        trap 'umount_esp; umount "$_src_mp" 2>/dev/null || true; losetup -d "$_loop" 2>/dev/null || true; rmdir "$_src_mp" 2>/dev/null || true; rm -f /data/ota/esp-src.fat /data/local/tmp/spx-esp-src.fat 2>/dev/null || true' EXIT

        [ -f "$_src_mp/SPX-IMAGE.txt" ] || { echo "ERR:new_esp_missing_marker" >&2; exit 1; }
        grep -qx 'SPX_IMAGE_FORMAT=1' "$_src_mp/SPX-IMAGE.txt" || { echo "ERR:new_esp_invalid_marker" >&2; exit 1; }
        [ -f "$_src_mp/EFI/SQ2Nat/Boot" ] || { echo "ERR:new_esp_missing_boot_shim" >&2; exit 1; }
        [ -f "$_src_mp/EFI/SQ2Nat/Image" ] || { echo "ERR:new_esp_missing_kernel" >&2; exit 1; }
        verify_initramfs_has_ota "$_src_mp/EFI/SQ2Nat/initramfs.cpio.gz" || exit 1

        backup_sq2nat_if_space "$_esp"

        ( cd "$_src_mp" && find . -type f | sed 's|^\./||' | sort ) > /data/local/tmp/spx-esp-files.txt
        while IFS= read -r _rel; do
            [ -n "$_rel" ] || continue
            copy_esp_atomic "$_src_mp/$_rel" "$_esp" "$_rel" ""
        done < /data/local/tmp/spx-esp-files.txt
        rm -f /data/local/tmp/spx-esp-files.txt

        # Verify critical boot binaries on target ESP before arming pending OTA.
        _k_src="$(sha256_dev "$_src_mp/EFI/SQ2Nat/Image")"
        _k_dst="$(sha256_dev "$_esp/EFI/SQ2Nat/Image")"
        [ "$_k_src" = "$_k_dst" ] || { echo "ERR:kernel_verify_failed" >&2; exit 1; }
        _i_src="$(sha256_dev "$_src_mp/EFI/SQ2Nat/initramfs.cpio.gz")"
        _i_dst="$(sha256_dev "$_esp/EFI/SQ2Nat/initramfs.cpio.gz")"
        [ "$_i_src" = "$_i_dst" ] || { echo "ERR:initramfs_verify_failed" >&2; exit 1; }

        ensure_grub_boots_sq2nat "$_esp" "$_src_mp/EFI/BOOT/grub.cfg"

        _ptmp="$_esp/SPX-OTA-PENDING.tmp"
        {
            printf 'PACKAGE_REL=ota/pending.pkg\n'
            printf 'PACKAGE_SHA256=%s\n' "$_exp_sha"
            printf 'PACKAGE_KIND=%s\n' "$_kind"
            printf 'APPLY_SUPER=1\n'
            printf 'WIPE_DATA=%s\n' "$_wipe_data"
            printf 'VERSION=%s\n' "$_version"
        } > "$_ptmp"
        sync 2>/dev/null || true
        mv -f "$_ptmp" "$_esp/SPX-OTA-PENDING"
        sync 2>/dev/null || true
        echo "OK_OFFLINE_STAGED"
        ;;

    reboot)
        sync 2>/dev/null || true
        rm -f /data/local/tmp/spx-adb-helper.sh 2>/dev/null || true
        svc power reboot >/dev/null 2>&1 || setprop sys.powerctl reboot >/dev/null 2>&1 || reboot
        ;;

    *)
        echo "ERR:unknown_cmd:$CMD" >&2
        exit 2
        ;;
esac
DEVICE_EOF

adb_cmd push "$WORK_DIR/spx-adb-helper.sh" /data/local/tmp/spx-adb-helper.sh >/dev/null
run_root "chmod 755 /data/local/tmp/spx-adb-helper.sh"

# ---- 4. Probe Device Compatibility ----

PROBE_OUT="$(run_root "sh /data/local/tmp/spx-adb-helper.sh probe" | tr -d '\r')" || die "Device probe failed"
DEV_SUPER_BYTES="$(printf '%s\n' "$PROBE_OUT" | awk -F= '$1=="SUPER_BYTES"{print $2; exit}')"
DEV_ESP_BYTES="$(printf '%s\n' "$PROBE_OUT" | awk -F= '$1=="ESP_BYTES"{print $2; exit}')"
DEV_DATA_FREE="$(printf '%s\n' "$PROBE_OUT" | awk -F= '$1=="DATA_FREE_BYTES"{print $2; exit}')"
DEV_HAS_OTA_INFO="$(printf '%s\n' "$PROBE_OUT" | awk -F= '$1=="HAS_OTA_INFO"{print $2; exit}')"
DEV_SUPER_SHA="$(printf '%s\n' "$PROBE_OUT" | awk -F= '$1=="CURRENT_SUPER_SHA256"{print $2; exit}')"
DEV_KERNEL="$(printf '%s\n' "$PROBE_OUT" | awk -F= '$1=="KERNEL"{print $2; exit}')"

log "Device: kernel=$DEV_KERNEL super=${DEV_SUPER_BYTES}B esp=${DEV_ESP_BYTES}B data_free=${DEV_DATA_FREE}B has_ota_info=$DEV_HAS_OTA_INFO"

if [ "$SUPER_INCLUDED" = "1" ] && [ "$SUPER_BYTES" -gt 0 ]; then
    [ "$DEV_SUPER_BYTES" -ge "$SUPER_BYTES" ] || die "Device super partition ($DEV_SUPER_BYTES B) is smaller than package super ($SUPER_BYTES B)"
fi

if [ "$PKG_KIND" = "manifest" ] && [ "$SUPER_INCLUDED" = "0" ] && [ "$FORCE_LEGACY_DELTA" != "1" ]; then
    if [ "$DEV_HAS_OTA_INFO" != "1" ]; then
        die "Device appears to be on a legacy build (e.g. v0.7.0 without OTA info / 5-partition super). First upgrade from v0.7.0 MUST use a full image (.img.gz/.img) or --mode full OTA archive."
    fi
    if [ -n "$REQ_SUPER_SHA" ] && [ -n "$DEV_SUPER_SHA" ] && [ "$REQ_SUPER_SHA" != "$DEV_SUPER_SHA" ]; then
        die "Delta package requires super SHA-256 $REQ_SUPER_SHA, but device has $DEV_SUPER_SHA. Use a full image package instead."
    fi
fi

NEED_BYTES=$(( PKG_BYTES + ESP_BYTES + 268435456 ))
[ "$DEV_DATA_FREE" -ge "$NEED_BYTES" ] || die "Insufficient free space on /data (need $NEED_BYTES bytes, available $DEV_DATA_FREE bytes)"

# ---- 5. Confirmation ----

log "Upgrade plan:"
log "  Package     : $PACKAGE ($PKG_KIND)"
log "  Version     : ${PKG_VERSION:-unknown}"
log "  Super update: $([ "$SUPER_INCLUDED" = "1" ] && echo "YES (offline in initramfs)" || echo "NO (unchanged)")"
log "  User data   : $([ "$WIPE_DATA" = "1" ] && echo "WIPE ALL DATA (CLEAN INSTALL)" || echo "KEEP USER DATA & TOUCH CALIBRATION")"
log "  Reboot      : $([ "$NO_REBOOT" = "1" ] && echo "NO (--no-reboot)" || echo "AUTOMATIC")"

if [ "$DRY_RUN" = "1" ]; then
    log "Dry run complete; no changes made."
    exit 0
fi

if [ "$ASSUME_YES" != "1" ]; then
    if [ "$WIPE_DATA" = "1" ]; then
        printf '\nWARNING: --wipe-data will ERASE all user apps, accounts, Wi-Fi, files, and touch calibration!\n'
        printf 'Type WIPE to confirm clean install: '
        read -r _ans
        [ "$_ans" = "WIPE" ] || die "Aborted by user."
    else
        printf 'Proceed with upgrade? [y/N]: '
        read -r _ans
        case "$_ans" in y|Y|yes|YES) ;; *) die "Aborted by user." ;; esac
    fi
fi

# ---- 6. Push Package & Verify SHA-256 on Device ----

STAGED_HIT="$(run_root "sh /data/local/tmp/spx-adb-helper.sh check-staged-pkg $PKG_SHA256 $PKG_BYTES" 2>/dev/null | tr -d '\r' || true)"
if [ "$STAGED_HIT" = "HIT=$PKG_SHA256" ]; then
    log "Reusing already-staged verified package on device (/data/ota/pending.pkg)."
else
    log "Pushing package to device (/data/local/tmp/spx-ota-upload.pkg)..."
    adb_cmd push "$PACKAGE" /data/local/tmp/spx-ota-upload.pkg
    log "Verifying package SHA-256 on device..."
    STAGE_OUT="$(run_root "sh /data/local/tmp/spx-adb-helper.sh stage-pkg $PKG_SHA256" | tr -d '\r')" || die "Failed to verify staged package on device"
    log "$STAGE_OUT"
fi

# ---- 7. Apply ESP Bootstrap & Arm Offline OTA ----

if [ "$PKG_KIND" = "manifest" ]; then
    log "Applying manifest ESP update and scheduling offline stage if needed..."
    APPLY_OUT="$(run_root "sh /data/local/tmp/spx-adb-helper.sh apply-manifest $PKG_SHA256 $SUPER_INCLUDED $WIPE_DATA $PKG_VERSION" | tr -d '\r')" || die "Failed to apply manifest update"
    log "$APPLY_OUT"
else
    log "Extracting SQ2PROBE ESP image on device (verifying against host)..."
    ESP_SRC_PATH="/data/ota/esp-src.fat"
    if ! DEV_EXT="$(run_root "sh /data/local/tmp/spx-adb-helper.sh extract-esp-on-device $PKG_KIND $ESP_OFF $ESP_BYTES" 2>&1 | tr -d '\r')"; then
        log "Device-side stream extract unavailable ($DEV_EXT); falling back to host extraction..."
        HOST_ESP="$WORK_DIR/esp-src.fat"
        if [ "$PKG_KIND" = "full-img-gz" ]; then
            gzip -dc "$PACKAGE" | tail -c +"$(( ESP_OFF + 1 ))" | head -c "$ESP_BYTES" > "$HOST_ESP"
        else
            tail -c +"$(( ESP_OFF + 1 ))" "$PACKAGE" | head -c "$ESP_BYTES" > "$HOST_ESP"
        fi
        adb_cmd push "$HOST_ESP" /data/local/tmp/spx-esp-src.fat
        ESP_SRC_PATH="/data/local/tmp/spx-esp-src.fat"
    else
        log "$DEV_EXT"
    fi

    log "Updating SQ2PROBE boot payload & arming offline super installation..."
    APPLY_OUT="$(run_root "sh /data/local/tmp/spx-adb-helper.sh apply-full-esp-and-pending $PKG_SHA256 $PKG_KIND $WIPE_DATA $PKG_VERSION $ESP_SRC_PATH" | tr -d '\r')" || die "Failed to bootstrap ESP & arm offline OTA"
    log "$APPLY_OUT"
fi

# ---- 8. Reboot ----

if [ "$NO_REBOOT" = "1" ]; then
    log "Update staged successfully (--no-reboot). Reboot the device manually to complete installation."
    exit 0
fi

log "Triggering device reboot..."
run_root "sh /data/local/tmp/spx-adb-helper.sh reboot" || true
if [ "$SUPER_INCLUDED" = "1" ] || [ "$WIPE_DATA" = "1" ]; then
    log "DONE! Keep power connected: the device will automatically install the offline update and reboot into the new system."
else
    log "DONE! Device is rebooting into the updated system."
fi
