#requires -Version 5.1
<#
.SYNOPSIS
    One-click ADB upgrade for Surface Pro X Googlebook OS (Windows PowerShell).
.DESCRIPTION
    Upgrades Surface Pro X devices (including legacy v0.7.0 39-partition and newer
    5-partition layouts) over root ADB without manually entering Rescue Mode or
    removing the SSD.
    Accepts:
      - Full disk images (.img.gz / .img)   -> offline full update (super + ESP)
      - Manifest OTA archives (.tar.gz)     -> manifest-guided delta or full update
    Preserves user data, apps, Wi-Fi, touch calibration, and dual-boot grub.cfg by
    default; pass -WipeData for a clean install.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$PackagePath,

    [string]$Serial,

    [string]$AdbPath = 'adb',

    [switch]$WipeData,

    [string]$VersionOverride,

    [switch]$NoReboot,

    [switch]$DryRun,

    [switch]$Force,

    [switch]$Yes
)

$ErrorActionPreference = 'Stop'

$nativeCode = @'
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

public sealed class SpxAdbPackageInfo
{
    public string Kind = "unknown"; // manifest | full-img | full-img-gz
    public string Sha256 = "";
    public long PackageBytes = 0;
    public bool SuperIncluded = false;
    public long SuperOffset = 0;
    public long SuperBytes = 0;
    public long EspOffset = 0;
    public long EspBytes = 0;
    public string Version = "";
    public string EspMode = "";
    public string RequiredSuperSha256 = "";
    public string Error = "";
}

public static class SpxAdbOtaNative
{
    private static readonly uint[] Crc32Table = BuildCrc32();

    private static uint[] BuildCrc32()
    {
        uint[] t = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            uint c = i;
            for (int k = 0; k < 8; k++)
                c = ((c & 1) != 0) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }

    private static uint Crc32(byte[] data, int off, int len)
    {
        uint c = 0xffffffffu;
        for (int i = 0; i < len; i++)
            c = Crc32Table[(c ^ data[off + i]) & 0xff] ^ (c >> 8);
        return ~c;
    }

    private static int ReadExact(Stream s, byte[] buf, int off, int count)
    {
        int got = 0;
        while (got < count)
        {
            int n = s.Read(buf, off + got, count - got);
            if (n <= 0) break;
            got += n;
        }
        return got;
    }

    private static void SkipExact(Stream s, long count)
    {
        if (count <= 0) return;
        if (s.CanSeek)
        {
            s.Seek(count, SeekOrigin.Current);
            return;
        }
        byte[] buf = new byte[1024 * 1024];
        long left = count;
        while (left > 0)
        {
            int n = s.Read(buf, 0, (int)Math.Min(left, buf.Length));
            if (n <= 0) throw new EndOfStreamException("Unexpected EOF while skipping stream.");
            left -= n;
        }
    }

    private static string ComputeSha256(string path)
    {
        using (FileStream fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 4 * 1024 * 1024))
        using (SHA256 sha = SHA256.Create())
        {
            byte[] hash = sha.ComputeHash(fs);
            StringBuilder sb = new StringBuilder(64);
            foreach (byte b in hash) sb.Append(b.ToString("x2"));
            return sb.ToString();
        }
    }

    private static bool IsGptHeader(byte[] hdr, int n)
    {
        if (n < 520) return false;
        if (hdr[510] != 0x55 || hdr[511] != 0xaa) return false;
        return Encoding.ASCII.GetString(hdr, 512, 8) == "EFI PART";
    }

    private static bool IsTarHeader(byte[] hdr, int n)
    {
        if (n < 263) return false;
        return Encoding.ASCII.GetString(hdr, 257, 5) == "ustar";
    }

    private static long ParseOctal(byte[] buf, int off, int len)
    {
        string s = Encoding.ASCII.GetString(buf, off, len).Trim('\0', ' ');
        if (string.IsNullOrEmpty(s)) return 0;
        return Convert.ToInt64(s, 8);
    }

    private static void ParseGpt(Stream stream, byte[] first1024, SpxAdbPackageInfo info)
    {
        uint hdrSize = BitConverter.ToUInt32(first1024, 512 + 12);
        uint hdrCrc = BitConverter.ToUInt32(first1024, 512 + 16);
        if (hdrSize < 92 || hdrSize > 512)
            throw new InvalidDataException("Invalid GPT header size.");
        byte[] copy = new byte[hdrSize];
        Array.Copy(first1024, 512, copy, 0, (int)hdrSize);
        copy[16] = copy[17] = copy[18] = copy[19] = 0;
        if (Crc32(copy, 0, copy.Length) != hdrCrc)
            throw new InvalidDataException("GPT header CRC32 mismatch.");

        ulong entriesLba = BitConverter.ToUInt64(first1024, 512 + 72);
        uint numEntries = BitConverter.ToUInt32(first1024, 512 + 80);
        uint entrySize = BitConverter.ToUInt32(first1024, 512 + 84);
        uint entriesCrc = BitConverter.ToUInt32(first1024, 512 + 88);
        if (entriesLba < 2 || entrySize < 128 || numEntries == 0 || numEntries > 4096)
            throw new InvalidDataException("Unsupported GPT table layout.");

        long skip = (long)(entriesLba * 512UL) - 1024L;
        SkipExact(stream, skip);
        byte[] table = new byte[numEntries * entrySize];
        if (ReadExact(stream, table, 0, table.Length) != table.Length)
            throw new EndOfStreamException("Truncated GPT entry table.");
        if (Crc32(table, 0, table.Length) != entriesCrc)
            throw new InvalidDataException("GPT entry table CRC32 mismatch.");

        for (uint i = 0; i < numEntries; i++)
        {
            int pos = (int)(i * entrySize);
            bool zero = true;
            for (int b = 0; b < 16; b++) { if (table[pos + b] != 0) { zero = false; break; } }
            if (zero) continue;
            ulong flba = BitConverter.ToUInt64(table, pos + 32);
            ulong llba = BitConverter.ToUInt64(table, pos + 40);
            if (llba < flba) continue;
            string name = Encoding.Unicode.GetString(table, pos + 56, 72);
            int nul = name.IndexOf('\0');
            if (nul >= 0) name = name.Substring(0, nul);
            long off = (long)(flba * 512UL);
            long len = (long)((llba - flba + 1UL) * 512UL);
            if (name == "super") { info.SuperOffset = off; info.SuperBytes = len; }
            else if (name == "SQ2PROBE") { info.EspOffset = off; info.EspBytes = len; }
        }
        if (info.SuperBytes <= 0) throw new InvalidDataException("Image GPT missing super partition.");
        if (info.EspBytes <= 0) throw new InvalidDataException("Image GPT missing SQ2PROBE partition.");
        info.SuperIncluded = true;
    }

    private static void ParseTarManifest(Stream stream, byte[] first1024, int firstLen, SpxAdbPackageInfo info)
    {
        // Reassemble a stream starting with the already-read bytes.
        int memPos = 0;
        Func<byte[], int, int, int> readFn = (buf, off, count) =>
        {
            int total = 0;
            if (memPos < firstLen)
            {
                int avail = Math.Min(count, firstLen - memPos);
                Array.Copy(first1024, memPos, buf, off, avail);
                memPos += avail;
                total += avail;
            }
            if (total < count)
            {
                total += ReadExact(stream, buf, off + total, count - total);
            }
            return total;
        };

        byte[] hdr = new byte[512];
        bool foundKv = false;
        bool foundTsv = false;
        for (int iter = 0; iter < 64; iter++)
        {
            if (readFn(hdr, 0, 512) != 512) break;
            bool allZero = true;
            for (int i = 0; i < 512; i++) { if (hdr[i] != 0) { allZero = false; break; } }
            if (allZero) break;

            string name = Encoding.ASCII.GetString(hdr, 0, 100).Trim('\0');
            string prefix = Encoding.ASCII.GetString(hdr, 345, 155).Trim('\0');
            string fullName = string.IsNullOrEmpty(prefix) ? name : (prefix + "/" + name);
            long size = ParseOctal(hdr, 124, 12);
            long padded = ((size + 511L) / 512L) * 512L;

            if (fullName == "ota-manifest.kv" && size > 0 && size <= 65536)
            {
                byte[] body = new byte[padded];
                if (readFn(body, 0, (int)padded) != (int)padded)
                    throw new EndOfStreamException("Truncated ota-manifest.kv.");
                string text = Encoding.UTF8.GetString(body, 0, (int)size);
                foreach (string rawLine in text.Split('\n'))
                {
                    string line = rawLine.Trim('\r');
                    int eq = line.IndexOf('=');
                    if (eq <= 0) continue;
                    string k = line.Substring(0, eq).Trim();
                    string v = line.Substring(eq + 1).Trim();
                    if (k == "SCHEMA" && v != "1") throw new InvalidDataException("Unsupported OTA manifest schema: " + v);
                    else if (k == "VERSION") info.Version = v;
                    else if (k == "SUPER_INCLUDED") info.SuperIncluded = (v == "1");
                    else if (k == "SUPER_BYTES") long.TryParse(v, out info.SuperBytes);
                    else if (k == "REQUIRED_SUPER_SHA256") info.RequiredSuperSha256 = v;
                    else if (k == "ESP_MODE") info.EspMode = v;
                }
                foundKv = true;
            }
            else
            {
                if (fullName == "ota-files.tsv") foundTsv = true;
                long left = padded;
                byte[] scratch = new byte[65536];
                while (left > 0)
                {
                    int want = (int)Math.Min(left, scratch.Length);
                    if (readFn(scratch, 0, want) != want)
                        throw new EndOfStreamException("Truncated tar entry: " + fullName);
                    left -= want;
                }
            }
            if (foundKv && foundTsv) break;
        }
        if (!foundKv || !foundTsv)
            throw new InvalidDataException("Archive is missing ota-manifest.kv or ota-files.tsv.");
    }

    public static SpxAdbPackageInfo InspectPackage(string path)
    {
        SpxAdbPackageInfo info = new SpxAdbPackageInfo();
        FileInfo fi = new FileInfo(path);
        info.PackageBytes = fi.Length;
        info.Sha256 = ComputeSha256(path);

        using (FileStream fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            byte[] head = new byte[1024];
            int n = ReadExact(fs, head, 0, head.Length);
            if (n >= 2 && head[0] == 0x1f && head[1] == 0x8b)
            {
                fs.Seek(0, SeekOrigin.Begin);
                using (GZipStream gz = new GZipStream(fs, CompressionMode.Decompress))
                {
                    byte[] inner = new byte[1024];
                    int ni = ReadExact(gz, inner, 0, inner.Length);
                    if (IsGptHeader(inner, ni))
                    {
                        info.Kind = "full-img-gz";
                        ParseGpt(gz, inner, info);
                    }
                    else if (IsTarHeader(inner, ni))
                    {
                        info.Kind = "manifest";
                        ParseTarManifest(gz, inner, ni, info);
                    }
                    else
                    {
                        throw new InvalidDataException("Unsupported gzip package contents (expected GPT .img.gz or OTA .tar.gz).");
                    }
                }
            }
            else if (IsGptHeader(head, n))
            {
                info.Kind = "full-img";
                ParseGpt(fs, head, info);
            }
            else
            {
                throw new InvalidDataException("Unsupported package format (expected .img.gz, .img, or OTA .tar.gz).");
            }
        }
        return info;
    }

    public static void ExtractRegion(string path, bool isGzip, long offset, long length, string outPath)
    {
        using (FileStream fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 4 * 1024 * 1024))
        using (Stream src = isGzip ? (Stream)new GZipStream(fs, CompressionMode.Decompress) : fs)
        using (FileStream dst = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.None, 4 * 1024 * 1024))
        {
            SkipExact(src, offset);
            byte[] buf = new byte[4 * 1024 * 1024];
            long left = length;
            while (left > 0)
            {
                int want = (int)Math.Min(left, buf.Length);
                int n = src.Read(buf, 0, want);
                if (n <= 0) throw new EndOfStreamException("Unexpected EOF while extracting region.");
                dst.Write(buf, 0, n);
                left -= n;
            }
            dst.Flush(true);
        }
    }
}
'@

if (-not ('SpxAdbOtaNative' -as [type])) {
    Add-Type -TypeDefinition $nativeCode -Language CSharp
}

function Write-Log([string]$Msg) {
    Write-Host "[spx-upgrade] $Msg"
}

function Resolve-AdbExecutable([string]$Requested) {
    $cmd = Get-Command $Requested -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $localCand = Join-Path $PSScriptRoot 'adb.exe'
    if (Test-Path -LiteralPath $localCand) { return $localCand }
    throw "adb executable not found ('$Requested'). Install Android Platform-Tools and ensure adb.exe is in PATH or pass -AdbPath."
}

function Invoke-Adb {
    param([string[]]$Arguments, [switch]$IgnoreExitCode, [switch]$StreamOutput)
    $fullArgs = @()
    if ($script:TargetSerial) {
        $fullArgs += @('-s', $script:TargetSerial)
    }
    $fullArgs += $Arguments
    $prevPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($StreamOutput) {
            & $script:AdbExe @fullArgs
            $code = $LASTEXITCODE
            $text = ''
        } else {
            $output = & $script:AdbExe @fullArgs 2>&1
            $code = $LASTEXITCODE
            $text = (($output | ForEach-Object {
                if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { $_.ToString() }
            }) -join "`n")
        }
    } finally {
        $ErrorActionPreference = $prevPref
    }
    if (-not $IgnoreExitCode -and $code -ne 0) {
        throw "adb $($Arguments -join ' ') failed (exit $code): $text"
    }
    return [pscustomobject]@{
        ExitCode = $code
        Text     = $text
    }
}

function Invoke-DeviceRoot([string]$Command, [switch]$IgnoreExitCode) {
    if ($script:RootMode -eq 'direct') {
        return Invoke-Adb -Arguments @('shell', $Command) -IgnoreExitCode:$IgnoreExitCode
    }
    $escaped = $Command.Replace("'", "'`"'`"'")
    return Invoke-Adb -Arguments @('shell', "su -c '$escaped'") -IgnoreExitCode:$IgnoreExitCode
}

function Parse-KeyValues([string]$Text) {
    $map = @{}
    foreach ($rawLine in ($Text -split "`n")) {
        $line = $rawLine.Trim("`r")
        $idx = $line.IndexOf('=')
        if ($idx -gt 0) {
            $map[$line.Substring(0, $idx).Trim()] = $line.Substring($idx + 1).Trim()
        }
    }
    return $map
}

function Pick-PackageInteractively {
    try {
        Add-Type -AssemblyName System.Windows.Forms | Out-Null
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = 'Select Googlebook OS Update Package (.img.gz / .img / .tar.gz)'
        $dlg.Filter = 'Update Packages (*.img.gz;*.img;*.tar.gz)|*.img.gz;*.img;*.tar.gz|All Files (*.*)|*.*'
        if ($dlg.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            return $dlg.FileName
        }
    } catch {}
    $entered = Read-Host 'Enter path to update package (.img.gz / .img / .tar.gz)'
    return $entered.Trim('"')
}

# ---- Interactive prompt when double-clicked without arguments ----
if (-not $PackagePath) {
    $PackagePath = Pick-PackageInteractively
    if (-not $PackagePath) {
        throw 'No update package selected.'
    }
    if (-not $WipeData -and -not $Yes) {
        Write-Host ''
        Write-Host 'Data retention mode:' -ForegroundColor Cyan
        Write-Host '  [1] Keep user data (apps, Wi-Fi, touch calibration & settings) [Default]'
        Write-Host '  [2] Clean install (WIPE all user data, metadata & misc)'
        $choice = Read-Host 'Select [1/2] (default 1)'
        if ($choice -eq '2') { $WipeData = $true }
    }
}

if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
    throw "Package file not found: $PackagePath"
}
$PackagePath = (Resolve-Path -LiteralPath $PackagePath).Path
$script:AdbExe = Resolve-AdbExecutable $AdbPath

# ---- 1. Inspect Package Locally ----
Write-Log "Inspecting package: $PackagePath"
$pkg = [SpxAdbOtaNative]::InspectPackage($PackagePath)
if ($VersionOverride) {
    $pkg.Version = $VersionOverride
} elseif (-not $pkg.Version) {
    $pkg.Version = 'full-image'
}
Write-Log "Package kind: $($pkg.Kind) | Size: $($pkg.PackageBytes) B | SHA-256: $($pkg.Sha256)"
if ($pkg.Kind -eq 'manifest') {
    Write-Log "Manifest: version=$($pkg.Version) super_included=$($pkg.SuperIncluded) esp_mode=$($pkg.EspMode)"
} else {
    Write-Log "GPT: super=$($pkg.SuperBytes)B @$($pkg.SuperOffset) | SQ2PROBE=$($pkg.EspBytes)B @$($pkg.EspOffset)"
}

# ---- 2. ADB Device & Root Check ----
Write-Log 'Checking ADB device connection...'
$script:TargetSerial = $Serial
if (-not $script:TargetSerial) {
    $devOut = (Invoke-Adb -Arguments @('devices')).Text
    $devs = @()
    foreach ($line in ($devOut -split "`n")) {
        $t = $line.Trim("`r").Trim()
        if ($t -match '^(\S+)\s+device$') {
            $devs += $Matches[1]
        }
    }
    if ($devs.Count -eq 0) {
        throw 'No authorized ADB device found. Connect Surface Pro X lower USB-C port and enable USB debugging.'
    }
    if ($devs.Count -gt 1) {
        throw "Multiple ADB devices found ($($devs -join ', ')). Pass -Serial <SERIAL>."
    }
    $script:TargetSerial = $devs[0]
}
Write-Log "Target ADB device: $script:TargetSerial"

$script:RootMode = ''
$uidDirect = (Invoke-Adb -Arguments @('shell', 'id -u') -IgnoreExitCode).Text.Trim()
if ($uidDirect -eq '0') {
    $script:RootMode = 'direct'
} else {
    $null = Invoke-Adb -Arguments @('root') -IgnoreExitCode
    Start-Sleep -Seconds 1
    $null = Invoke-Adb -Arguments @('wait-for-device') -IgnoreExitCode
    $uidAfter = (Invoke-Adb -Arguments @('shell', 'id -u') -IgnoreExitCode).Text.Trim()
    if ($uidAfter -eq '0') {
        $script:RootMode = 'direct'
    } else {
        $uidSu = (Invoke-Adb -Arguments @('shell', "su -c 'id -u'") -IgnoreExitCode).Text.Trim()
        if ($uidSu -eq '0') {
            $script:RootMode = 'su'
        } else {
            throw 'Root ADB access required. Grant root permission to Shell in KernelSU Next Manager (or enable adb root) and retry.'
        }
    }
}
Write-Log "Root mode: $script:RootMode"

# ---- 3. Push Device-Side Helper Script (LF, UTF-8 without BOM) ----
$deviceHelper = @'
#!/system/bin/sh
set -eu

CMD="$1"
shift

is_preserved_esp() {
    case "$1" in
        EFI/BOOT/grub.cfg|SPX-USERDATA-GROWN.txt|SPX-BOOT-SOURCE.txt|SPX-OTA-*) return 0 ;;
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
'@

$tmpHelper = Join-Path ([IO.Path]::GetTempPath()) ("spx-adb-helper-" + [Guid]::NewGuid().ToString('N') + '.sh')
$tmpHostEsp = Join-Path ([IO.Path]::GetTempPath()) ("spx-esp-src-" + [Guid]::NewGuid().ToString('N') + '.fat')
try {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($tmpHelper, ($deviceHelper -replace "`r`n", "`n"), $utf8NoBom)
    $null = Invoke-Adb -Arguments @('push', $tmpHelper, '/data/local/tmp/spx-adb-helper.sh')
    $null = Invoke-DeviceRoot 'chmod 755 /data/local/tmp/spx-adb-helper.sh'

    # ---- 4. Probe Device ----
    $probeMap = Parse-KeyValues (Invoke-DeviceRoot 'sh /data/local/tmp/spx-adb-helper.sh probe').Text
    $devSuperBytes = [long]$probeMap['SUPER_BYTES']
    $devEspBytes   = [long]$probeMap['ESP_BYTES']
    $devDataFree   = [long]$probeMap['DATA_FREE_BYTES']
    $devHasOtaInfo = $probeMap['HAS_OTA_INFO']
    $devSuperSha   = $probeMap['CURRENT_SUPER_SHA256']
    $devKernel     = $probeMap['KERNEL']

    Write-Log "Device: kernel=$devKernel super=${devSuperBytes}B esp=${devEspBytes}B data_free=${devDataFree}B has_ota_info=$devHasOtaInfo"

    if ($pkg.SuperIncluded -and $pkg.SuperBytes -gt 0 -and $devSuperBytes -lt $pkg.SuperBytes) {
        throw "Device super partition ($devSuperBytes B) is smaller than package super ($($pkg.SuperBytes) B)."
    }

    if ($pkg.Kind -eq 'manifest' -and -not $pkg.SuperIncluded -and -not $Force) {
        if ($devHasOtaInfo -ne '1') {
            throw 'Device appears to be on a legacy build (e.g. v0.7.0 without 5-partition super / OTA info). First upgrade from v0.7.0 MUST use a full image (.img.gz/.img) or --mode full OTA archive.'
        }
        if ($pkg.RequiredSuperSha256 -and $devSuperSha -and ($pkg.RequiredSuperSha256 -ne $devSuperSha)) {
            throw "Delta package requires super SHA-256 $($pkg.RequiredSuperSha256), but device has $devSuperSha. Use a full image package instead."
        }
    }

    $needBytes = $pkg.PackageBytes + $pkg.EspBytes + (256L * 1024L * 1024L)
    if ($devDataFree -lt $needBytes) {
        throw "Insufficient free space on /data (need $needBytes B, available $devDataFree B)."
    }

    # ---- 5. Plan & Confirmation ----
    $wipeFlag = if ($WipeData) { '1' } else { '0' }
    $superFlag = if ($pkg.SuperIncluded) { '1' } else { '0' }
    Write-Host ''
    Write-Log 'Upgrade plan:'
    Write-Log "  Package     : $PackagePath ($($pkg.Kind))"
    Write-Log "  Version     : $($pkg.Version)"
    Write-Log ("  Super update: " + $(if ($pkg.SuperIncluded) { 'YES (offline in initramfs)' } else { 'NO (unchanged)' }))
    Write-Log ("  User data   : " + $(if ($WipeData) { 'WIPE ALL DATA (CLEAN INSTALL)' } else { 'KEEP USER DATA & TOUCH CALIBRATION' }))
    Write-Log ("  Reboot      : " + $(if ($NoReboot) { 'NO (-NoReboot)' } else { 'AUTOMATIC' }))

    if ($DryRun) {
        Write-Log 'Dry run complete; no changes made.'
        return
    }

    if (-not $Yes) {
        if ($WipeData) {
            Write-Host ''
            Write-Host 'WARNING: -WipeData will ERASE all user apps, accounts, Wi-Fi, files, and touch calibration!' -ForegroundColor Red
            $ans = Read-Host 'Type WIPE to confirm clean install'
            if ($ans -ne 'WIPE') { throw 'Aborted by user.' }
        } else {
            $ans = Read-Host 'Proceed with upgrade? [y/N]'
            if ($ans -notmatch '^(y|yes)$') { throw 'Aborted by user.' }
        }
    }

    # ---- 6. Push & Verify Package ----
    $stagedHit = (Invoke-DeviceRoot "sh /data/local/tmp/spx-adb-helper.sh check-staged-pkg $($pkg.Sha256) $($pkg.PackageBytes)" -IgnoreExitCode).Text.Trim()
    if ($stagedHit -eq ("HIT=" + $pkg.Sha256)) {
        Write-Log "Reusing already-staged verified package on device (/data/ota/pending.pkg)."
    } else {
        Write-Log 'Pushing package to device (/data/local/tmp/spx-ota-upload.pkg)...'
        $null = Invoke-Adb -Arguments @('push', $PackagePath, '/data/local/tmp/spx-ota-upload.pkg') -StreamOutput
        Write-Log 'Verifying package SHA-256 on device...'
        $stageOut = (Invoke-DeviceRoot "sh /data/local/tmp/spx-adb-helper.sh stage-pkg $($pkg.Sha256)").Text.Trim()
        Write-Log $stageOut
    }

    # ---- 7. Apply ESP Bootstrap & Arm Offline OTA ----
    if ($pkg.Kind -eq 'manifest') {
        Write-Log 'Applying manifest ESP update and scheduling offline stage if needed...'
        $applyOut = (Invoke-DeviceRoot "sh /data/local/tmp/spx-adb-helper.sh apply-manifest $($pkg.Sha256) $superFlag $wipeFlag $($pkg.Version)").Text.Trim()
        Write-Log $applyOut
    } else {
        Write-Log 'Extracting SQ2PROBE ESP image on device...'
        $espSrcPath = '/data/ota/esp-src.fat'
        $extRes = Invoke-DeviceRoot "sh /data/local/tmp/spx-adb-helper.sh extract-esp-on-device $($pkg.Kind) $($pkg.EspOffset) $($pkg.EspBytes)" -IgnoreExitCode
        if ($extRes.ExitCode -ne 0) {
            Write-Log "Device-side stream extract unavailable ($($extRes.Text.Trim())); falling back to host extraction..."
            [SpxAdbOtaNative]::ExtractRegion($PackagePath, ($pkg.Kind -eq 'full-img-gz'), $pkg.EspOffset, $pkg.EspBytes, $tmpHostEsp)
            $null = Invoke-Adb -Arguments @('push', $tmpHostEsp, '/data/local/tmp/spx-esp-src.fat') -StreamOutput
            $espSrcPath = '/data/local/tmp/spx-esp-src.fat'
        } else {
            Write-Log $extRes.Text.Trim()
        }

        Write-Log 'Updating SQ2PROBE boot payload & arming offline super installation...'
        $applyOut = (Invoke-DeviceRoot "sh /data/local/tmp/spx-adb-helper.sh apply-full-esp-and-pending $($pkg.Sha256) $($pkg.Kind) $wipeFlag $($pkg.Version) $espSrcPath").Text.Trim()
        Write-Log $applyOut
    }

    # ---- 8. Reboot ----
    if ($NoReboot) {
        Write-Log 'Update staged successfully (-NoReboot). Reboot the device manually to complete installation.'
        return
    }

    Write-Log 'Triggering device reboot...'
    $null = Invoke-DeviceRoot 'sh /data/local/tmp/spx-adb-helper.sh reboot' -IgnoreExitCode
    if ($pkg.SuperIncluded -or $WipeData) {
        Write-Host ''
        Write-Log 'DONE! Keep power connected: the device will automatically install the offline update and reboot into the new system.'
    } else {
        Write-Host ''
        Write-Log 'DONE! Device is rebooting into the updated system.'
    }
} finally {
    if (Test-Path -LiteralPath $tmpHelper) { Remove-Item -LiteralPath $tmpHelper -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $tmpHostEsp) { Remove-Item -LiteralPath $tmpHostEsp -Force -ErrorAction SilentlyContinue }
}
