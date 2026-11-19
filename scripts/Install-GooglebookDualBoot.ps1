#requires -Version 5.1
<#
.SYNOPSIS
    Windows and Googlebook OS (Surface Pro X 6.18 Hybrid) dual-boot installer & boot manager.
.DESCRIPTION
    Runs directly on Windows 10/11 (PowerShell 5.1+ / 7+, x64 or ARM64).
    Supports:
      - Same-disk dual boot alongside Windows (uses unallocated space or shrinks OS volume)
      - Full-disk flashing to an external USB / secondary disk with dual-boot GRUB menu
      - UEFI NVRAM registration via bcdedit ({fwbootmgr}) without overwriting Windows Boot Manager
      - Repairing UEFI boot order / switching default OS / uninstalling cleanly
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [ValidateSet('Menu', 'Status', 'Install', 'ConfigureBoot', 'SwitchDefault', 'Uninstall')]
    [string]$Action = 'Menu',

    [string]$ImagePath,

    [int]$TargetDiskNumber = -1,

    [ValidateSet('Auto', 'SameDisk', 'FullDisk')]
    [string]$InstallMode = 'Auto',

    [int]$DataSizeGB = 0,

    [int]$ShrinkOSVolumeGB = 0,

    [ValidateSet('GooglebookOS', 'Windows')]
    [string]$DefaultOS = 'GooglebookOS',

    [ValidateRange(0, 60)]
    [int]$TimeoutSeconds = 5,

    [ValidateSet('Auto', 'WindowsEspShim', 'Sq2ProbeDirect')]
    [string]$EspMode = 'Auto',

    [string]$ConfirmErase,

    [switch]$SkipSecureBootCheck,

    [switch]$KeepPartitionsOnUninstall
)

$ErrorActionPreference = 'Stop'
$script:RepoRoot = Split-Path -Parent $PSScriptRoot
$script:GptEspGuid = [Guid]'c12a7328-f81f-11d2-ba4b-00a0c93ec93b'
$script:GptBasicDataGuid = [Guid]'ebd0a0a2-b9e5-4433-87c0-68b6b72699c7'
$script:RequiredImagePartitions = @('SQ2PROBE', 'super', 'userdata', 'metadata', 'misc')
$script:KnownGooglebookPartNames = @(
    'SQ2PROBE', 'super', 'userdata', 'metadata', 'misc', 'ota_recovery_b',
    'persist', 'frp_persist', 'rawdump', 'desktop_security_persist', 'desktop_security_storage'
)
$script:BootEntryTitle = 'Googlebook OS'
$script:ShimRelativeDir = 'EFI\GooglebookOS'
$script:ShimRelativeEfi = '\EFI\GooglebookOS\BOOTAA64.EFI'
$script:DirectRelativeEfi = '\EFI\BOOT\BOOTAA64.EFI'
$script:CopyProgressState = $null

$nativeCode = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

public sealed class SpxGptPartitionEntry
{
    public int Index;
    public string Name;
    public Guid TypeGuid;
    public Guid PartitionGuid;
    public ulong FirstLba;
    public ulong LastLba;
    public long OffsetBytes;
    public long SizeBytes;
    public ulong Attributes;
}

public sealed class SpxDriveLayout
{
    public uint PartitionStyle;
    public Guid DiskId;
    public long StartingUsableOffset;
    public long UsableLength;
    public uint MaxPartitionCount;
    public List<SpxDiskPartitionEntry> Partitions = new List<SpxDiskPartitionEntry>();
}

public sealed class SpxDiskPartitionEntry
{
    public long StartingOffset;
    public long PartitionLength;
    public uint PartitionNumber;
    public bool RewritePartition;
    public bool IsServicePartition;
    public Guid PartitionType;
    public Guid PartitionId;
    public ulong Attributes;
    public string Name;
}

public sealed class SpxImageInfo
{
    public string Path;
    public bool IsGzip;
    public long LogicalImageBytes;
    public List<SpxGptPartitionEntry> Partitions = new List<SpxGptPartitionEntry>();
}

public sealed class SpxPartitionCopyTarget
{
    public string Name;
    public long SourceOffsetBytes;
    public long CopyBytes;
    public string TargetDevicePath;
}

public static class SpxDualBootNative
{
    private const uint GENERIC_READ = 0x80000000;
    private const uint GENERIC_WRITE = 0x40000000;
    private const uint FILE_SHARE_READ = 0x00000001;
    private const uint FILE_SHARE_WRITE = 0x00000002;
    private const uint OPEN_EXISTING = 3;

    private const uint FSCTL_LOCK_VOLUME = 0x00090018;
    private const uint FSCTL_DISMOUNT_VOLUME = 0x00090020;
    private const uint FSCTL_ALLOW_EXTENDED_DASD_IO = 0x00090083;
    private const uint IOCTL_DISK_GET_DRIVE_LAYOUT_EX = 0x00070050;
    private const uint IOCTL_DISK_SET_DRIVE_LAYOUT_EX = 0x0007C054;
    private const uint IOCTL_DISK_UPDATE_PROPERTIES = 0x00070140;

    private const uint PARTITION_STYLE_GPT = 1;
    private const int LAYOUT_HEADER_BYTES = 48;
    private const int PARTITION_ENTRY_BYTES = 144;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(
        string lpFileName,
        uint dwDesiredAccess,
        uint dwShareMode,
        IntPtr lpSecurityAttributes,
        uint dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool DeviceIoControl(
        SafeFileHandle hDevice,
        uint dwIoControlCode,
        byte[] lpInBuffer,
        int nInBufferSize,
        byte[] lpOutBuffer,
        int nOutBufferSize,
        out int lpBytesReturned,
        IntPtr lpOverlapped);

    private static readonly uint[] Crc32Table = BuildCrc32Table();

    private static uint[] BuildCrc32Table()
    {
        uint[] table = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            uint c = i;
            for (int k = 0; k < 8; k++)
            {
                if ((c & 1) != 0)
                    c = 0xedb88320u ^ (c >> 1);
                else
                    c >>= 1;
            }
            table[i] = c;
        }
        return table;
    }

    public static uint ComputeCrc32(byte[] data, int offset, int count)
    {
        uint crc = 0xffffffffu;
        for (int i = 0; i < count; i++)
        {
            crc = Crc32Table[(crc ^ data[offset + i]) & 0xff] ^ (crc >> 8);
        }
        return ~crc;
    }

    private static int ReadExact(Stream stream, byte[] buffer, int offset, int count)
    {
        int total = 0;
        while (total < count)
        {
            int n = stream.Read(buffer, offset + total, count - total);
            if (n <= 0) break;
            total += n;
        }
        return total;
    }

    private static void SkipExact(Stream stream, long bytesToSkip)
    {
        if (bytesToSkip < 0) throw new ArgumentOutOfRangeException("bytesToSkip");
        if (bytesToSkip == 0) return;
        if (stream.CanSeek)
        {
            stream.Seek(bytesToSkip, SeekOrigin.Current);
            return;
        }
        byte[] scratch = new byte[1024 * 1024];
        long left = bytesToSkip;
        while (left > 0)
        {
            int chunk = (int)Math.Min(left, scratch.Length);
            int n = stream.Read(scratch, 0, chunk);
            if (n <= 0) throw new EndOfStreamException("Unexpected end of compressed image stream while skipping.");
            left -= n;
        }
    }

    public static bool IsGzipFile(string path)
    {
        using (FileStream fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            int b0 = fs.ReadByte();
            int b1 = fs.ReadByte();
            return b0 == 0x1f && b1 == 0x8b;
        }
    }

    public static SpxImageInfo ParseImageGpt(string imagePath)
    {
        bool isGz = IsGzipFile(imagePath);
        SpxImageInfo info = new SpxImageInfo();
        info.Path = imagePath;
        info.IsGzip = isGz;

        using (FileStream fs = new FileStream(imagePath, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (Stream stream = isGz ? (Stream)new GZipStream(fs, CompressionMode.Decompress) : fs)
        {
            byte[] headerBuf = new byte[1024];
            if (ReadExact(stream, headerBuf, 0, headerBuf.Length) != headerBuf.Length)
                throw new InvalidDataException("Image is too small to contain MBR and primary GPT header.");

            if (headerBuf[510] != 0x55 || headerBuf[511] != 0xaa)
                throw new InvalidDataException("Missing protective MBR 0x55AA signature.");

            string sig = Encoding.ASCII.GetString(headerBuf, 512, 8);
            if (sig != "EFI PART")
                throw new InvalidDataException("Missing primary GPT 'EFI PART' signature.");

            int headerSize = BitConverter.ToInt32(headerBuf, 512 + 12);
            if (headerSize < 92 || headerSize > 512)
                throw new InvalidDataException("Invalid GPT header size: " + headerSize);

            uint expectedHeaderCrc = BitConverter.ToUInt32(headerBuf, 512 + 16);
            byte[] crcCheck = new byte[headerSize];
            Buffer.BlockCopy(headerBuf, 512, crcCheck, 0, headerSize);
            Array.Clear(crcCheck, 16, 4);
            if (ComputeCrc32(crcCheck, 0, headerSize) != expectedHeaderCrc)
                throw new InvalidDataException("Primary GPT header CRC32 mismatch.");

            ulong myLba = BitConverter.ToUInt64(headerBuf, 512 + 24);
            ulong altLba = BitConverter.ToUInt64(headerBuf, 512 + 32);
            if (myLba != 1 || altLba < 34)
                throw new InvalidDataException("Unexpected GPT LBA geometry.");

            ulong entryLba = BitConverter.ToUInt64(headerBuf, 512 + 72);
            uint entryCount = BitConverter.ToUInt32(headerBuf, 512 + 80);
            uint entrySize = BitConverter.ToUInt32(headerBuf, 512 + 84);
            uint expectedTableCrc = BitConverter.ToUInt32(headerBuf, 512 + 88);

            if (entryLba < 2 || entrySize < 128 || (entrySize % 128) != 0 || entryCount == 0 || entryCount > 16384)
                throw new InvalidDataException("Unsupported GPT partition table geometry.");

            long currentOffset = 1024;
            long tableOffset = (long)(entryLba * 512UL);
            if (tableOffset > currentOffset)
                SkipExact(stream, tableOffset - currentOffset);

            int tableBytes = checked((int)(entryCount * entrySize));
            byte[] table = new byte[tableBytes];
            if (ReadExact(stream, table, 0, tableBytes) != tableBytes)
                throw new EndOfStreamException("Truncated GPT partition entry array.");

            if (ComputeCrc32(table, 0, tableBytes) != expectedTableCrc)
                throw new InvalidDataException("GPT partition entry array CRC32 mismatch.");

            info.LogicalImageBytes = checked((long)((altLba + 1UL) * 512UL));

            for (int i = 0; i < (int)entryCount; i++)
            {
                int pos = i * (int)entrySize;
                byte[] typeBytes = new byte[16];
                Buffer.BlockCopy(table, pos, typeBytes, 0, 16);
                Guid typeGuid = new Guid(typeBytes);
                if (typeGuid == Guid.Empty) continue;

                byte[] partBytes = new byte[16];
                Buffer.BlockCopy(table, pos + 16, partBytes, 0, 16);
                Guid partGuid = new Guid(partBytes);

                ulong firstLba = BitConverter.ToUInt64(table, pos + 32);
                ulong lastLba = BitConverter.ToUInt64(table, pos + 40);
                ulong attrs = BitConverter.ToUInt64(table, pos + 48);
                string name = Encoding.Unicode.GetString(table, pos + 56, 72).TrimEnd('\0');

                SpxGptPartitionEntry entry = new SpxGptPartitionEntry();
                entry.Index = i + 1;
                entry.Name = name;
                entry.TypeGuid = typeGuid;
                entry.PartitionGuid = partGuid;
                entry.FirstLba = firstLba;
                entry.LastLba = lastLba;
                entry.OffsetBytes = checked((long)(firstLba * 512UL));
                entry.SizeBytes = checked((long)((lastLba - firstLba + 1UL) * 512UL));
                entry.Attributes = attrs;
                info.Partitions.Add(entry);
            }
        }
        return info;
    }

    private static SafeFileHandle OpenDevice(string path, bool write)
    {
        uint access = write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
        SafeFileHandle h = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (h.IsInvalid)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Failed to open device: " + path);
        return h;
    }

    private static void TryControl(SafeFileHandle handle, uint code, bool throwOnError, string name)
    {
        int bytesReturned;
        bool ok = DeviceIoControl(handle, code, null, 0, null, 0, out bytesReturned, IntPtr.Zero);
        if (!ok && throwOnError)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "DeviceIoControl failed (" + name + ")");
    }

    private static void LockAndDismount(SafeFileHandle handle)
    {
        bool locked = false;
        for (int attempt = 0; attempt < 8; attempt++)
        {
            int br;
            if (DeviceIoControl(handle, FSCTL_LOCK_VOLUME, null, 0, null, 0, out br, IntPtr.Zero))
            {
                locked = true;
                break;
            }
            System.Threading.Thread.Sleep(200);
        }
        if (!locked)
        {
            int err = Marshal.GetLastWin32Error();
            throw new Win32Exception(err, "Failed to lock partition volume before raw write.");
        }
        TryControl(handle, FSCTL_DISMOUNT_VOLUME, false, "FSCTL_DISMOUNT_VOLUME");
        TryControl(handle, FSCTL_ALLOW_EXTENDED_DASD_IO, false, "FSCTL_ALLOW_EXTENDED_DASD_IO");
    }

    public static SpxDriveLayout GetDriveLayout(int diskNumber)
    {
        string path = "\\\\.\\PhysicalDrive" + diskNumber;
        using (SafeFileHandle h = OpenDevice(path, false))
        {
            int size = LAYOUT_HEADER_BYTES + 128 * PARTITION_ENTRY_BYTES;
            byte[] buf = new byte[size];
            int returned;
            if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, null, 0, buf, buf.Length, out returned, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed on disk " + diskNumber);

            uint style = BitConverter.ToUInt32(buf, 0);
            if (style != PARTITION_STYLE_GPT)
                throw new InvalidOperationException("Target disk " + diskNumber + " is not GPT (PartitionStyle=" + style + ").");

            uint count = BitConverter.ToUInt32(buf, 4);
            SpxDriveLayout layout = new SpxDriveLayout();
            layout.PartitionStyle = style;
            byte[] diskGuid = new byte[16];
            Buffer.BlockCopy(buf, 8, diskGuid, 0, 16);
            layout.DiskId = new Guid(diskGuid);
            layout.StartingUsableOffset = BitConverter.ToInt64(buf, 24);
            layout.UsableLength = BitConverter.ToInt64(buf, 32);
            layout.MaxPartitionCount = BitConverter.ToUInt32(buf, 40);

            for (int i = 0; i < (int)count; i++)
            {
                int pos = LAYOUT_HEADER_BYTES + i * PARTITION_ENTRY_BYTES;
                if (pos + PARTITION_ENTRY_BYTES > returned) break;

                long start = BitConverter.ToInt64(buf, pos + 8);
                long length = BitConverter.ToInt64(buf, pos + 16);
                uint partNum = BitConverter.ToUInt32(buf, pos + 24);
                byte[] pType = new byte[16];
                Buffer.BlockCopy(buf, pos + 32, pType, 0, 16);
                Guid typeGuid = new Guid(pType);
                if (typeGuid == Guid.Empty || length <= 0) continue;

                byte[] pId = new byte[16];
                Buffer.BlockCopy(buf, pos + 48, pId, 0, 16);

                SpxDiskPartitionEntry p = new SpxDiskPartitionEntry();
                p.StartingOffset = start;
                p.PartitionLength = length;
                p.PartitionNumber = partNum;
                p.RewritePartition = false;
                p.IsServicePartition = buf[pos + 29] != 0;
                p.PartitionType = typeGuid;
                p.PartitionId = new Guid(pId);
                p.Attributes = BitConverter.ToUInt64(buf, pos + 64);
                p.Name = Encoding.Unicode.GetString(buf, pos + 72, 72).TrimEnd('\0');
                layout.Partitions.Add(p);
            }
            return layout;
        }
    }

    public static void SetDriveLayout(int diskNumber, SpxDriveLayout layout)
    {
        if (layout.PartitionStyle != PARTITION_STYLE_GPT)
            throw new InvalidOperationException("Only GPT layout is supported.");

        int count = layout.Partitions.Count;
        if (count > (int)layout.MaxPartitionCount)
            throw new InvalidOperationException("Partition count exceeds GPT MaxPartitionCount (" + layout.MaxPartitionCount + ").");

        byte[] buf = new byte[LAYOUT_HEADER_BYTES + count * PARTITION_ENTRY_BYTES];
        Buffer.BlockCopy(BitConverter.GetBytes((uint)PARTITION_STYLE_GPT), 0, buf, 0, 4);
        Buffer.BlockCopy(BitConverter.GetBytes((uint)count), 0, buf, 4, 4);
        Buffer.BlockCopy(layout.DiskId.ToByteArray(), 0, buf, 8, 16);
        Buffer.BlockCopy(BitConverter.GetBytes(layout.StartingUsableOffset), 0, buf, 24, 8);
        Buffer.BlockCopy(BitConverter.GetBytes(layout.UsableLength), 0, buf, 32, 8);
        Buffer.BlockCopy(BitConverter.GetBytes(layout.MaxPartitionCount), 0, buf, 40, 4);

        for (int i = 0; i < count; i++)
        {
            SpxDiskPartitionEntry p = layout.Partitions[i];
            int pos = LAYOUT_HEADER_BYTES + i * PARTITION_ENTRY_BYTES;
            Buffer.BlockCopy(BitConverter.GetBytes((uint)PARTITION_STYLE_GPT), 0, buf, pos + 0, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(p.StartingOffset), 0, buf, pos + 8, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(p.PartitionLength), 0, buf, pos + 16, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(p.PartitionNumber), 0, buf, pos + 24, 4);
            buf[pos + 28] = p.RewritePartition ? (byte)1 : (byte)0;
            buf[pos + 29] = p.IsServicePartition ? (byte)1 : (byte)0;
            Buffer.BlockCopy(p.PartitionType.ToByteArray(), 0, buf, pos + 32, 16);
            Buffer.BlockCopy(p.PartitionId.ToByteArray(), 0, buf, pos + 48, 16);
            Buffer.BlockCopy(BitConverter.GetBytes(p.Attributes), 0, buf, pos + 64, 8);

            if (!string.IsNullOrEmpty(p.Name))
            {
                byte[] nameBytes = Encoding.Unicode.GetBytes(p.Name);
                int copyLen = Math.Min(nameBytes.Length, 72);
                Buffer.BlockCopy(nameBytes, 0, buf, pos + 72, copyLen);
            }
        }

        string path = "\\\\.\\PhysicalDrive" + diskNumber;
        using (SafeFileHandle h = OpenDevice(path, true))
        {
            int returned;
            if (!DeviceIoControl(h, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, buf, buf.Length, null, 0, out returned, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "IOCTL_DISK_SET_DRIVE_LAYOUT_EX failed on disk " + diskNumber);
            TryControl(h, IOCTL_DISK_UPDATE_PROPERTIES, false, "IOCTL_DISK_UPDATE_PROPERTIES");
        }
    }

    public static void CopyPartitionsFromImage(string imagePath, SpxPartitionCopyTarget[] targets, Action<string, long, long> onProgress)
    {
        if (targets == null || targets.Length == 0) return;
        Array.Sort(targets, delegate(SpxPartitionCopyTarget a, SpxPartitionCopyTarget b)
        {
            return a.SourceOffsetBytes.CompareTo(b.SourceOffsetBytes);
        });

        bool isGz = IsGzipFile(imagePath);
        byte[] buffer = new byte[4 * 1024 * 1024];

        using (FileStream fs = new FileStream(imagePath, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (Stream stream = isGz ? (Stream)new GZipStream(fs, CompressionMode.Decompress) : fs)
        {
            long streamPos = 0;
            for (int i = 0; i < targets.Length; i++)
            {
                SpxPartitionCopyTarget t = targets[i];
                if (t.SourceOffsetBytes < streamPos)
                    throw new InvalidOperationException("Overlapping or unsorted source offset for " + t.Name);

                if (isGz)
                {
                    if (t.SourceOffsetBytes > streamPos)
                    {
                        SkipExact(stream, t.SourceOffsetBytes - streamPos);
                        streamPos = t.SourceOffsetBytes;
                    }
                }
                else
                {
                    stream.Seek(t.SourceOffsetBytes, SeekOrigin.Begin);
                    streamPos = t.SourceOffsetBytes;
                }

                using (SafeFileHandle dev = OpenDevice(t.TargetDevicePath, true))
                {
                    LockAndDismount(dev);
                    using (FileStream dst = new FileStream(dev, FileAccess.Write, buffer.Length, false))
                    {
                        long left = t.CopyBytes;
                        long written = 0;
                        while (left > 0)
                        {
                            int want = (int)Math.Min(left, buffer.Length);
                            int got = ReadExact(stream, buffer, 0, want);
                            if (got != want)
                                throw new EndOfStreamException("Short read from image while copying partition " + t.Name);
                            dst.Write(buffer, 0, got);
                            left -= got;
                            written += got;
                            streamPos += got;
                            if (onProgress != null) onProgress(t.Name, written, t.CopyBytes);
                        }
                        dst.Flush(true);
                    }
                }
            }
        }
    }

    public static void WriteFullImageToDisk(string imagePath, int diskNumber, long totalBytes, Action<long, long> onProgress)
    {
        bool isGz = IsGzipFile(imagePath);
        byte[] buffer = new byte[4 * 1024 * 1024];
        string targetPath = "\\\\.\\PhysicalDrive" + diskNumber;

        using (FileStream fs = new FileStream(imagePath, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (Stream stream = isGz ? (Stream)new GZipStream(fs, CompressionMode.Decompress) : fs)
        using (SafeFileHandle dev = OpenDevice(targetPath, true))
        {
            TryControl(dev, FSCTL_ALLOW_EXTENDED_DASD_IO, false, "FSCTL_ALLOW_EXTENDED_DASD_IO");
            using (FileStream dst = new FileStream(dev, FileAccess.Write, buffer.Length, false))
            {
                long done = 0;
                while (done < totalBytes)
                {
                    int want = (int)Math.Min(totalBytes - done, buffer.Length);
                    int got = ReadExact(stream, buffer, 0, want);
                    if (got != want)
                        throw new EndOfStreamException("Unexpected end of image stream at byte " + done + " of " + totalBytes);
                    dst.Write(buffer, 0, got);
                    done += got;
                    if (onProgress != null) onProgress(done, totalBytes);
                }
                dst.Flush(true);
            }
            TryControl(dev, IOCTL_DISK_UPDATE_PROPERTIES, false, "IOCTL_DISK_UPDATE_PROPERTIES");
        }
    }

    public static string ReadFat32VolumeUuid(string deviceOrVolumePath)
    {
        using (SafeFileHandle h = OpenDevice(deviceOrVolumePath, false))
        using (FileStream fs = new FileStream(h, FileAccess.Read))
        {
            byte[] vbr = new byte[512];
            if (ReadExact(fs, vbr, 0, 512) != 512)
                throw new EndOfStreamException("Unable to read VBR from " + deviceOrVolumePath);
            string fsType = Encoding.ASCII.GetString(vbr, 0x52, 8).Trim();
            if (fsType != "FAT32")
                throw new InvalidDataException("Partition is not FAT32 (found '" + fsType + "').");
            uint volId = BitConverter.ToUInt32(vbr, 0x43);
            ushort hi = (ushort)((volId >> 16) & 0xffff);
            ushort lo = (ushort)(volId & 0xffff);
            return string.Format("{0:X4}-{1:X4}", hi, lo);
        }
    }
}
'@

if (-not ('SpxDualBootNative' -as [type])) {
    Add-Type -TypeDefinition $nativeCode -Language CSharp
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-Administrator {
    if (-not (Test-IsAdministrator)) {
        throw 'This operation requires Administrator privileges. Run via Install-GooglebookDualBoot.cmd or an elevated PowerShell prompt.'
    }
}

function Format-ByteSize([long]$Bytes) {
    if ($Bytes -ge 1TB) { return ('{0:N2} TB' -f ($Bytes / 1TB)) }
    if ($Bytes -ge 1GB) { return ('{0:N2} GB' -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ('{0:N1} MB' -f ($Bytes / 1MB)) }
    return "$Bytes B"
}

function Align-Up([long]$Value, [long]$Alignment) {
    $rem = $Value % $Alignment
    if ($rem -eq 0) { return $Value }
    return $Value + ($Alignment - $rem)
}

function Align-Down([long]$Value, [long]$Alignment) {
    return $Value - ($Value % $Alignment)
}

function Get-SecureBootState {
    try {
        if (Confirm-SecureBootUEFI) { return 'Enabled' }
        return 'Disabled'
    } catch {
        return 'Unknown'
    }
}

function Get-BitLockerState([string]$DriveLetter) {
    try {
        if (Get-Command Get-BitLockerVolume -ErrorAction SilentlyContinue) {
            $v = Get-BitLockerVolume -MountPoint "${DriveLetter}:" -ErrorAction Stop
            return $v.ProtectionStatus.ToString()
        }
    } catch {}
    return 'Unknown'
}

function Find-FreeDriveLetter {
    $used = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::OrdinalIgnoreCase)
    foreach ($d in (Get-PSDrive -PSProvider FileSystem)) {
        $null = $used.Add($d.Name)
    }
    try {
        foreach ($p in (Get-Partition | Where-Object { $_.DriveLetter })) {
            $null = $used.Add([string]$p.DriveLetter)
        }
    } catch {}
    foreach ($code in 90..77) {
        $letter = [string][char]$code
        if (-not $used.Contains($letter)) { return $letter }
    }
    throw 'No free drive letter available between M: and Z:.'
}

function Mount-WindowsEsp {
    $letter = Find-FreeDriveLetter
    $out = & mountvol.exe "${letter}:" /S 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ("Failed to mount Windows EFI System Partition: " + ($out -join ' '))
    }
    return [pscustomobject]@{
        Letter = $letter
        Root   = "${letter}:\"
    }
}

function Dismount-WindowsEsp([string]$Letter) {
    if ($Letter) {
        & mountvol.exe "${Letter}:" /D 2>$null | Out-Null
    }
}

function Resolve-DefaultImagePath {
    $candidates = @(
        (Join-Path (Get-Location) 'surface-pro-x-6.18-hybrid-generic.img'),
        (Join-Path (Get-Location) 'surface-pro-x-6.18-hybrid-generic.img.gz'),
        (Join-Path $PSScriptRoot 'surface-pro-x-6.18-hybrid-generic.img'),
        (Join-Path $PSScriptRoot 'surface-pro-x-6.18-hybrid-generic.img.gz'),
        (Join-Path $script:RepoRoot 'disk\surface-pro-x-6.18-hybrid-generic.img'),
        (Join-Path $script:RepoRoot 'disk\surface-pro-x-6.18-hybrid-generic.img.gz')
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { return (Resolve-Path -LiteralPath $c).Path }
    }
    return $null
}

function Get-ValidatedImageInfo([string]$Path) {
    if (-not $Path) { throw 'ImagePath is required.' }
    $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    $info = [SpxDualBootNative]::ParseImageGpt($resolved)
    $byName = @{}
    foreach ($p in $info.Partitions) {
        if ($byName.ContainsKey($p.Name)) {
            throw "Image contains duplicate GPT partition name: $($p.Name)"
        }
        $byName[$p.Name] = $p
    }
    foreach ($req in $script:RequiredImagePartitions) {
        if (-not $byName.ContainsKey($req)) {
            throw "Image is missing required Googlebook OS partition: $req"
        }
    }
    if ($byName['SQ2PROBE'].TypeGuid -ne $script:GptEspGuid) {
        throw 'Image SQ2PROBE partition does not have EFI System Partition GUID.'
    }
    return $info
}

function Get-DiskUnallocatedGaps([int]$DiskNumber) {
    $layout = [SpxDualBootNative]::GetDriveLayout($DiskNumber)
    $usableStart = Align-Up $layout.StartingUsableOffset 1048576
    $usableEnd = $layout.StartingUsableOffset + $layout.UsableLength
    $sorted = @($layout.Partitions | Sort-Object StartingOffset)
    $gaps = @()
    $cursor = $usableStart
    foreach ($p in $sorted) {
        if ($p.StartingOffset -gt $cursor) {
            $gapEnd = Align-Down $p.StartingOffset 4096
            if ($gapEnd -gt $cursor) {
                $gaps += [pscustomobject]@{
                    StartOffset       = [long]$cursor
                    EndOffset         = [long]$gapEnd
                    SizeBytes         = [long]($gapEnd - $cursor)
                    HasPartitionAfter = $true
                }
            }
        }
        $pEnd = Align-Up ($p.StartingOffset + $p.PartitionLength) 1048576
        if ($pEnd -gt $cursor) { $cursor = $pEnd }
    }
    $tailEnd = Align-Down $usableEnd 4096
    if ($tailEnd -gt $cursor) {
        $gaps += [pscustomobject]@{
            StartOffset       = [long]$cursor
            EndOffset         = [long]$tailEnd
            SizeBytes         = [long]($tailEnd - $cursor)
            HasPartitionAfter = $false
        }
    }
    return [pscustomobject]@{
        Layout = $layout
        Gaps   = $gaps
    }
}

function Get-MinimumSameDiskBytes($ImageInfo) {
    $total = 0L
    foreach ($p in $ImageInfo.Partitions) {
        $total += Align-Up $p.SizeBytes 1048576
    }
    return $total
}

function Build-SameDiskPartitionPlan($ImageInfo, $Gap, [int]$RequestedDataSizeGB) {
    $nonUserdata = @($ImageInfo.Partitions | Where-Object { $_.Name -ne 'userdata' } | Sort-Object OffsetBytes)
    $userdataImg = @($ImageInfo.Partitions | Where-Object { $_.Name -eq 'userdata' })[0]

    $fixedBytes = 0L
    foreach ($p in $nonUserdata) {
        $fixedBytes += Align-Up $p.SizeBytes 1048576
    }
    $minTotal = $fixedBytes + $userdataImg.SizeBytes
    if ($Gap.SizeBytes -lt $minTotal) {
        throw "Selected unallocated gap ($(Format-ByteSize $Gap.SizeBytes)) is smaller than minimum required ($(Format-ByteSize $minTotal))."
    }

    $usableEnd = $Gap.EndOffset
    if ($RequestedDataSizeGB -gt 0) {
        $requestedTotal = [long]$RequestedDataSizeGB * 1GB
        if ($requestedTotal -lt $minTotal) {
            throw "Requested DataSizeGB ($RequestedDataSizeGB GB) is smaller than minimum required ($([math]::Ceiling($minTotal / 1GB)) GB)."
        }
        $cappedEnd = Align-Down ($Gap.StartOffset + $requestedTotal) 4096
        if ($cappedEnd -lt $usableEnd) { $usableEnd = $cappedEnd }
    }

    $planned = @()
    $cursor = $Gap.StartOffset
    foreach ($p in $nonUserdata) {
        $cursor = Align-Up $cursor 1048576
        $planned += [pscustomobject]@{
            Name              = $p.Name
            SourceOffsetBytes = $p.OffsetBytes
            SourceSizeBytes   = $p.SizeBytes
            TargetOffsetBytes = [long]$cursor
            TargetSizeBytes   = [long]$p.SizeBytes
            FinalTypeGuid     = $p.TypeGuid
            InitialTypeGuid   = $(if ($p.Name -eq 'SQ2PROBE') { $script:GptBasicDataGuid } else { $p.TypeGuid })
            Attributes        = $p.Attributes
        }
        $cursor += $p.SizeBytes
    }

    $udStart = Align-Up $cursor 1048576
    $udSize = Align-Down ($usableEnd - $udStart) 4096
    if ($udSize -lt $userdataImg.SizeBytes) {
        throw 'Insufficient space for userdata partition after fixed partitions.'
    }
    $planned += [pscustomobject]@{
        Name              = 'userdata'
        SourceOffsetBytes = $userdataImg.OffsetBytes
        SourceSizeBytes   = $userdataImg.SizeBytes
        TargetOffsetBytes = [long]$udStart
        TargetSizeBytes   = [long]$udSize
        FinalTypeGuid     = $userdataImg.TypeGuid
        InitialTypeGuid   = $userdataImg.TypeGuid
        Attributes        = $userdataImg.Attributes
    }
    return $planned
}

function New-DualBootGrubConfig {
    param(
        [string]$Sq2ProbeUuid,
        [string]$WindowsEspUuid,
        [ValidateSet('GooglebookOS', 'Windows')]
        [string]$DefaultOS = 'GooglebookOS',
        [int]$TimeoutSeconds = 5
    )
    $defaultId = if ($DefaultOS -eq 'Windows') { 'windows' } else { 'spx-single-user' }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("set timeout=$TimeoutSeconds")
    $lines.Add("set default=$defaultId")
    $lines.Add('')
    $lines.Add("menuentry 'Googlebook OS (Surface Pro X 6.18 Hybrid)' --id spx-single-user {")
    if ($Sq2ProbeUuid) {
        $lines.Add("    if search --no-floppy --fs-uuid --set=root $Sq2ProbeUuid; then")
        $lines.Add('        true')
        $lines.Add('    else')
        $lines.Add('        search --no-floppy --file --set=root /SPX-IMAGE.txt')
        $lines.Add('    fi')
    } else {
        $lines.Add('    search --no-floppy --file --set=root /SPX-IMAGE.txt')
    }
    $lines.Add('    chainloader /EFI/SQ2Nat/start.efi')
    $lines.Add('}')
    $lines.Add('')
    $lines.Add("menuentry 'Windows' --id windows {")
    if ($WindowsEspUuid) {
        $lines.Add("    if search --no-floppy --fs-uuid --set=root $WindowsEspUuid; then")
        $lines.Add('        true')
        $lines.Add('    else')
        $lines.Add('        search --no-floppy --file --set=root /EFI/Microsoft/Boot/bootmgfw.efi')
        $lines.Add('    fi')
    } else {
        $lines.Add('    search --no-floppy --file --set=root /EFI/Microsoft/Boot/bootmgfw.efi')
    }
    $lines.Add('    chainloader /EFI/Microsoft/Boot/bootmgfw.efi')
    $lines.Add('}')
    $lines.Add('')
    $lines.Add("menuentry 'Googlebook OS - rescue (SSH / Wi-Fi / USB)' --id spx-rescue {")
    if ($Sq2ProbeUuid) {
        $lines.Add("    if search --no-floppy --fs-uuid --set=root $Sq2ProbeUuid; then")
        $lines.Add('        true')
        $lines.Add('    else')
        $lines.Add('        search --no-floppy --file --set=root /SPX-IMAGE.txt')
        $lines.Add('    fi')
    } else {
        $lines.Add('    search --no-floppy --file --set=root /SPX-IMAGE.txt')
    }
    $lines.Add('    chainloader /EFI/SQ2Res/start.efi')
    $lines.Add('}')
    $lines.Add('')
    $lines.Add("menuentry 'Reboot' --id reboot {")
    $lines.Add('    reboot')
    $lines.Add('}')
    $lines.Add('')
    $lines.Add("menuentry 'Power off' --id halt {")
    $lines.Add('    halt')
    $lines.Add('}')
    $lines.Add('')
    # GRUB requires LF line endings and UTF-8 without BOM.
    return ($lines -join "`n")
}

function Write-Utf8NoBomLf([string]$Path, [string]$Text) {
    $enc = [System.Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText($Path, ($Text -replace "`r`n", "`n"), $enc)
}

function Get-BcdFirmwareEntries {
    $raw = (& bcdedit.exe /enum firmware 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw ("bcdedit /enum firmware failed: " + ($raw -join "`n"))
    }
    $text = $raw -join "`n"
    $blocks = $text -split "(\r?\n){2,}"
    $entries = @()
    foreach ($b in $blocks) {
        $trimmed = $b.Trim()
        if (-not $trimmed) { continue }
        $idMatch = [regex]::Match($trimmed, '\{([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}|fwbootmgr|bootmgr)\}')
        if (-not $idMatch.Success) { continue }
        $entries += [pscustomobject]@{
            Identifier       = $idMatch.Value
            RawText          = $trimmed
            IsShimPath       = ($trimmed -match '(?i)\\EFI\\GooglebookOS\\BOOTAA64\.EFI')
            IsDirectBootPath = ($trimmed -match '(?i)\\EFI\\BOOT\\BOOTAA64\.EFI')
            HasTitle         = ($trimmed -match '(?i)Googlebook OS')
        }
    }
    return $entries
}

function Find-ExistingGooglebookBcdGuid([string]$RecordedGuid) {
    $entries = Get-BcdFirmwareEntries
    if ($RecordedGuid) {
        $match = @($entries | Where-Object { $_.Identifier -ieq $RecordedGuid })
        if ($match.Count -gt 0) { return $match[0].Identifier }
    }
    $byPath = @($entries | Where-Object { $_.IsShimPath -or ($_.HasTitle -and $_.IsDirectBootPath) })
    if ($byPath.Count -gt 0) { return $byPath[0].Identifier }
    $byTitle = @($entries | Where-Object { $_.HasTitle })
    if ($byTitle.Count -gt 0) { return $byTitle[0].Identifier }
    return $null
}

function Register-GooglebookUefiBootEntry {
    param(
        [string]$EfiPath,
        [string]$PartitionDeviceArg,
        [string]$RecordedGuid,
        [ValidateSet('GooglebookOS', 'Windows')]
        [string]$DefaultOS = 'GooglebookOS'
    )
    $guid = Find-ExistingGooglebookBcdGuid $RecordedGuid
    if (-not $guid) {
        $copyOut = (& bcdedit.exe /copy '{bootmgr}' /d $script:BootEntryTitle 2>&1) -join "`n"
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /copy {bootmgr} failed: $copyOut"
        }
        $m = [regex]::Match($copyOut, '\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}')
        if (-not $m.Success) {
            throw "Could not parse new BCD GUID from: $copyOut"
        }
        $guid = $m.Value
    }

    if ($PartitionDeviceArg) {
        $devOut = (& bcdedit.exe /set $guid device $PartitionDeviceArg 2>&1) -join "`n"
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /set $guid device $PartitionDeviceArg failed: $devOut"
        }
    }

    $pathOut = (& bcdedit.exe /set $guid path $EfiPath 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "bcdedit /set $guid path $EfiPath failed: $pathOut"
    }

    if ($DefaultOS -eq 'GooglebookOS') {
        $ordOut = (& bcdedit.exe /set '{fwbootmgr}' displayorder $guid /addfirst 2>&1) -join "`n"
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit displayorder addfirst failed: $ordOut"
        }
    } else {
        $null = & bcdedit.exe /set '{fwbootmgr}' displayorder $guid /addlast 2>&1
        $ordOut = (& bcdedit.exe /set '{fwbootmgr}' displayorder '{bootmgr}' /addfirst 2>&1) -join "`n"
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit displayorder windows-first failed: $ordOut"
        }
    }
    return $guid
}

function Remove-AllDriveLettersOnPartition([int]$DiskNumber, [int]$PartitionNumber) {
    try {
        $part = Get-Partition -DiskNumber $DiskNumber -PartitionNumber $PartitionNumber -ErrorAction SilentlyContinue
        if ($part -and $part.AccessPaths) {
            foreach ($ap in @($part.AccessPaths)) {
                if ($ap -match '^[A-Za-z]:\\?$') {
                    $clean = $ap.TrimEnd('\')
                    Remove-PartitionAccessPath -DiskNumber $DiskNumber -PartitionNumber $PartitionNumber -AccessPath $clean -ErrorAction SilentlyContinue
                }
            }
        }
    } catch {}
}

function Mount-PartitionTemporarily([int]$DiskNumber, [int]$PartitionNumber) {
    for ($attempt = 0; $attempt -lt 6; $attempt++) {
        $part = Get-Partition -DiskNumber $DiskNumber -PartitionNumber $PartitionNumber -ErrorAction SilentlyContinue
        if ($part) {
            $existing = @($part.AccessPaths | Where-Object { $_ -match '^[A-Za-z]:\\?$' })
            if ($existing.Count -gt 0) {
                $path = $existing[0].TrimEnd('\') + '\'
                if (Test-Path -LiteralPath $path) {
                    return [pscustomobject]@{
                        Path      = $path
                        Temporary = $false
                        Disk      = $DiskNumber
                        Partition = $PartitionNumber
                        AddedPath = $null
                    }
                }
            }
            $letter = Find-FreeDriveLetter
            $access = "${letter}:"
            try {
                Add-PartitionAccessPath -DiskNumber $DiskNumber -PartitionNumber $PartitionNumber -AccessPath $access -ErrorAction Stop
                Start-Sleep -Milliseconds 400
                if (Test-Path -LiteralPath "$access\") {
                    return [pscustomobject]@{
                        Path      = "$access\"
                        Temporary = $true
                        Disk      = $DiskNumber
                        Partition = $PartitionNumber
                        AddedPath = $access
                    }
                }
                Remove-PartitionAccessPath -DiskNumber $DiskNumber -PartitionNumber $PartitionNumber -AccessPath $access -ErrorAction SilentlyContinue
            } catch {}
        }
        Update-Disk -Number $DiskNumber
        Start-Sleep -Milliseconds 500
    }
    throw "Failed to mount Disk $DiskNumber Partition $PartitionNumber temporarily."
}

function Dismount-TemporaryPartition($MountInfo) {
    if ($MountInfo -and $MountInfo.Temporary -and $MountInfo.AddedPath) {
        try {
            Remove-PartitionAccessPath -DiskNumber $MountInfo.Disk -PartitionNumber $MountInfo.Partition -AccessPath $MountInfo.AddedPath -ErrorAction SilentlyContinue
        } catch {}
    }
}

function Find-Sq2ProbePartitions {
    $found = @()
    foreach ($d in (Get-Disk | Where-Object { $_.PartitionStyle -eq 'GPT' })) {
        try {
            $layout = [SpxDualBootNative]::GetDriveLayout($d.Number)
            foreach ($p in $layout.Partitions) {
                if ($p.Name -eq 'SQ2PROBE') {
                    $found += [pscustomobject]@{
                        DiskNumber      = $d.Number
                        FriendlyName    = $d.FriendlyName
                        BusType         = $d.BusType.ToString()
                        IsSystem        = $d.IsSystem
                        IsBoot          = $d.IsBoot
                        PartitionNumber = $p.PartitionNumber
                        Offset          = $p.StartingOffset
                        Size            = $p.PartitionLength
                        TypeGuid        = $p.PartitionType
                    }
                }
            }
        } catch {}
    }
    return $found
}

function Show-DualBootStatus {
    Write-Host ''
    Write-Host '=== Surface Pro X / Windows + Googlebook OS Dual-Boot Status ===' -ForegroundColor Cyan
    Write-Host ("Secure Boot : {0}" -f (Get-SecureBootState))
    Write-Host ("OS Drive    : {0} (BitLocker: {1})" -f $env:SystemDrive, (Get-BitLockerState $env:SystemDrive.TrimEnd(':')))
    Write-Host ''
    Write-Host 'Disks:' -ForegroundColor Yellow
    foreach ($d in (Get-Disk | Sort-Object Number)) {
        $role = if ($d.IsBoot -or $d.IsSystem) { 'Windows OS' } else { $d.BusType }
        Write-Host ("  Disk {0}: {1} | {2} | {3} | {4} | Sector={5}" -f `
            $d.Number, $d.FriendlyName, (Format-ByteSize $d.Size), $d.PartitionStyle, $role, $d.LogicalSectorSize)
        if ($d.PartitionStyle -eq 'GPT') {
            try {
                $info = Get-DiskUnallocatedGaps $d.Number
                foreach ($p in ($info.Layout.Partitions | Sort-Object StartingOffset)) {
                    $tag = if ($script:KnownGooglebookPartNames -contains $p.Name) { ' [GooglebookOS]' } else { '' }
                    Write-Host ("    Part {0,2}: {1,-26} {2,10}  @{3}{4}" -f `
                        $p.PartitionNumber, $p.Name, (Format-ByteSize $p.PartitionLength), (Format-ByteSize $p.StartingOffset), $tag)
                }
                foreach ($g in $info.Gaps) {
                    if ($g.SizeBytes -ge 64MB) {
                        $afterNote = if ($g.HasPartitionAfter) { ' (partition follows)' } else { ' (end of disk)' }
                        Write-Host ("    [FREE]   {0,-26} {1,10}  @{2}{3}" -f `
                            '<unallocated>', (Format-ByteSize $g.SizeBytes), (Format-ByteSize $g.StartOffset), $afterNote) -ForegroundColor Green
                    }
                }
            } catch {
                Write-Host ("    (Unable to inspect GPT layout: {0})" -f $_.Exception.Message) -ForegroundColor DarkYellow
            }
        }
    }

    $probes = @(Find-Sq2ProbePartitions)
    Write-Host ''
    if ($probes.Count -gt 0) {
        Write-Host 'Detected SQ2PROBE partitions:' -ForegroundColor Yellow
        foreach ($pr in $probes) {
            Write-Host ("  Disk {0} Part {1}: {2} ({3})" -f $pr.DiskNumber, $pr.PartitionNumber, (Format-ByteSize $pr.Size), $pr.BusType)
        }
    } else {
        Write-Host 'Detected SQ2PROBE partitions: None'
    }

    if (Test-IsAdministrator) {
        try {
            $bcd = Get-BcdFirmwareEntries
            $gb = @($bcd | Where-Object { $_.IsShimPath -or $_.HasTitle })
            Write-Host ("UEFI Googlebook Boot Entries: {0}" -f $gb.Count)
            foreach ($e in $gb) {
                Write-Host ("  {0}" -f $e.Identifier)
            }
        } catch {
            Write-Host ("UEFI BCD check skipped: {0}" -f $_.Exception.Message) -ForegroundColor DarkYellow
        }
    } else {
        Write-Host 'UEFI BCD check: Run as Administrator to inspect firmware entries.' -ForegroundColor DarkGray
    }
    Write-Host ''
}

function Invoke-ConfigureDualBoot {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [int]$Sq2DiskNumber,
        [int]$Sq2PartNumber,
        [ValidateSet('GooglebookOS', 'Windows')]
        [string]$SelectedDefaultOS = 'GooglebookOS',
        [int]$SelectedTimeout = 5,
        [ValidateSet('Auto', 'WindowsEspShim', 'Sq2ProbeDirect')]
        [string]$SelectedEspMode = 'Auto'
    )
    Assert-Administrator

    $layout = [SpxDualBootNative]::GetDriveLayout($Sq2DiskNumber)
    $sq2Entry = @($layout.Partitions | Where-Object { $_.PartitionNumber -eq $Sq2PartNumber })[0]
    if (-not $sq2Entry) { throw "SQ2PROBE partition $Sq2PartNumber not found on disk $Sq2DiskNumber." }
    $wasEsp = ($sq2Entry.PartitionType -eq $script:GptEspGuid)

    # Temporarily switch to Basic Data GUID if Windows blocks mounting a second ESP.
    if ($wasEsp) {
        foreach ($p in $layout.Partitions) {
            if ($p.PartitionNumber -eq $Sq2PartNumber) {
                $p.PartitionType = $script:GptBasicDataGuid
                $p.RewritePartition = $true
            } else {
                $p.RewritePartition = $false
            }
        }
        [SpxDualBootNative]::SetDriveLayout($Sq2DiskNumber, $layout)
        Update-Disk -Number $Sq2DiskNumber
        Start-Sleep -Milliseconds 500
    }

    $sq2Mount = $null
    $winEsp = $null
    $recordedGuid = $null
    try {
        $sq2Mount = Mount-PartitionTemporarily -DiskNumber $Sq2DiskNumber -PartitionNumber $Sq2PartNumber
        $spxMarker = Join-Path $sq2Mount.Path 'SPX-IMAGE.txt'
        $bootEfi = Join-Path $sq2Mount.Path 'EFI\BOOT\BOOTAA64.EFI'
        $startEfi = Join-Path $sq2Mount.Path 'EFI\SQ2Nat\start.efi'
        if (-not (Test-Path -LiteralPath $spxMarker) -or -not (Test-Path -LiteralPath $bootEfi) -or -not (Test-Path -LiteralPath $startEfi)) {
            throw "SQ2PROBE on Disk $Sq2DiskNumber Part $Sq2PartNumber is missing SPX-IMAGE.txt, BOOTAA64.EFI, or start.efi."
        }

        $sq2Uuid = [SpxDualBootNative]::ReadFat32VolumeUuid("\\?\GLOBALROOT\Device\Harddisk${Sq2DiskNumber}\Partition${Sq2PartNumber}")

        $winEspUuid = $null
        $useShim = $false
        try {
            $winEsp = Mount-WindowsEsp
            $bootmgfw = Join-Path $winEsp.Root 'EFI\Microsoft\Boot\bootmgfw.efi'
            if (Test-Path -LiteralPath $bootmgfw) {
                try {
                    $winEspUuid = [SpxDualBootNative]::ReadFat32VolumeUuid("\\.\" + $winEsp.Letter + ':')
                } catch {}
                if ($SelectedEspMode -eq 'WindowsEspShim' -or $SelectedEspMode -eq 'Auto') {
                    $freeBytes = (Get-PSDrive -Name $winEsp.Letter).Free
                    $needed = (Get-Item -LiteralPath $bootEfi).Length + 1048576
                    if ($freeBytes -ge $needed) {
                        $useShim = $true
                    } elseif ($SelectedEspMode -eq 'WindowsEspShim') {
                        throw "Windows ESP free space ($(Format-ByteSize $freeBytes)) is insufficient for GRUB shim."
                    }
                }
            }
        } catch {
            if ($SelectedEspMode -eq 'WindowsEspShim') { throw }
            Write-Warning "Windows ESP not accessible ($($_.Exception.Message)); falling back to SQ2PROBE direct UEFI entry."
        }

        $grubCfgText = New-DualBootGrubConfig -Sq2ProbeUuid $sq2Uuid -WindowsEspUuid $winEspUuid -DefaultOS $SelectedDefaultOS -TimeoutSeconds $SelectedTimeout
        $sq2GrubPath = Join-Path $sq2Mount.Path 'EFI\BOOT\grub.cfg'
        if ($PSCmdlet.ShouldProcess($sq2GrubPath, 'Write dual-boot grub.cfg')) {
            Write-Utf8NoBomLf $sq2GrubPath $grubCfgText
        }

        if ($useShim -and $winEsp) {
            $shimDir = Join-Path $winEsp.Root $script:ShimRelativeDir
            $statePath = Join-Path $shimDir 'dualboot-state.json'
            if (Test-Path -LiteralPath $statePath) {
                try {
                    $prev = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
                    if ($prev.bcd_guid) { $recordedGuid = [string]$prev.bcd_guid }
                } catch {}
            }
            if ($PSCmdlet.ShouldProcess($shimDir, 'Install GRUB dual-boot shim to Windows ESP')) {
                $null = New-Item -ItemType Directory -Path $shimDir -Force
                Copy-Item -LiteralPath $bootEfi -Destination (Join-Path $shimDir 'BOOTAA64.EFI') -Force
                Write-Utf8NoBomLf (Join-Path $shimDir 'grub.cfg') $grubCfgText
                $guid = Register-GooglebookUefiBootEntry -EfiPath $script:ShimRelativeEfi -PartitionDeviceArg $null -RecordedGuid $recordedGuid -DefaultOS $SelectedDefaultOS
                $state = [ordered]@{
                    schema             = 1
                    updated_at         = (Get-Date -Format o)
                    sq2probe_disk      = $Sq2DiskNumber
                    sq2probe_partition = $Sq2PartNumber
                    sq2probe_uuid      = $sq2Uuid
                    windows_esp_uuid   = $winEspUuid
                    default_os         = $SelectedDefaultOS
                    timeout_seconds    = $SelectedTimeout
                    esp_mode           = 'WindowsEspShim'
                    bcd_guid           = $guid
                }
                Write-Utf8NoBomLf $statePath (($state | ConvertTo-Json -Depth 4) + "`n")
                Write-Host "Registered UEFI boot entry $guid -> WindowsESP$($script:ShimRelativeEfi) (Default=$SelectedDefaultOS)" -ForegroundColor Green
            }
        } else {
            if ($PSCmdlet.ShouldProcess("Disk $Sq2DiskNumber Part $Sq2PartNumber", 'Register direct SQ2PROBE UEFI boot entry')) {
                $drivePart = $sq2Mount.Path.TrimEnd('\')
                $guid = Register-GooglebookUefiBootEntry -EfiPath $script:DirectRelativeEfi -PartitionDeviceArg "partition=$drivePart" -RecordedGuid $recordedGuid -DefaultOS $SelectedDefaultOS
                Write-Host "Registered UEFI boot entry $guid -> SQ2PROBE$($script:DirectRelativeEfi) (Default=$SelectedDefaultOS)" -ForegroundColor Green
            }
        }
    } finally {
        Dismount-TemporaryPartition $sq2Mount
        Remove-AllDriveLettersOnPartition -DiskNumber $Sq2DiskNumber -PartitionNumber $Sq2PartNumber
        if ($winEsp) { Dismount-WindowsEsp $winEsp.Letter }

        # Restore SQ2PROBE GPT partition type to EFI System Partition.
        $restoreLayout = [SpxDualBootNative]::GetDriveLayout($Sq2DiskNumber)
        $changed = $false
        foreach ($p in $restoreLayout.Partitions) {
            if ($p.PartitionNumber -eq $Sq2PartNumber -and $p.PartitionType -ne $script:GptEspGuid) {
                $p.PartitionType = $script:GptEspGuid
                $p.RewritePartition = $true
                $changed = $true
            } else {
                $p.RewritePartition = $false
            }
        }
        if ($changed) {
            [SpxDualBootNative]::SetDriveLayout($Sq2DiskNumber, $restoreLayout)
            Update-Disk -Number $Sq2DiskNumber
        }
    }
}

function Invoke-SameDiskInstall {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        $ImageInfo,
        [int]$DiskNumber,
        [string]$SelectedDefaultOS = 'GooglebookOS',
        [int]$SelectedTimeout = 5,
        [string]$SelectedEspMode = 'Auto'
    )
    Assert-Administrator
    $disk = Get-Disk -Number $DiskNumber -ErrorAction Stop
    if ($disk.PartitionStyle -ne 'GPT') { throw "Disk $DiskNumber must use GPT partition style." }
    if ($disk.LogicalSectorSize -ne 512) { throw "Disk $DiskNumber must have 512-byte logical sectors." }

    $existingLayout = [SpxDualBootNative]::GetDriveLayout($DiskNumber)
    $existingGoogle = @($existingLayout.Partitions | Where-Object { $script:KnownGooglebookPartNames -contains $_.Name })
    if ($existingGoogle.Count -gt 0) {
        throw ("Disk $DiskNumber already contains Googlebook OS partitions (" + (($existingGoogle | ForEach-Object { $_.Name }) -join ', ') + '). Run -Action Uninstall first if reinstalling.')
    }

    $minBytes = Get-MinimumSameDiskBytes $ImageInfo
    $desiredGapBytes = if ($DataSizeGB -gt 0) { [Math]::Max($minBytes, [long]$DataSizeGB * 1GB) } else { $minBytes }
    $gapInfo = Get-DiskUnallocatedGaps $DiskNumber
    $gap = if ($ShrinkOSVolumeGB -gt 0) { $null } else {
        @($gapInfo.Gaps | Where-Object { $_.SizeBytes -ge $desiredGapBytes } | Sort-Object SizeBytes -Descending)[0]
    }

    if (-not $gap) {
        $osDrive = $env:SystemDrive.TrimEnd(':')
        $osPart = Get-Partition -DriveLetter $osDrive -ErrorAction SilentlyContinue
        if (-not $osPart -or $osPart.DiskNumber -ne $DiskNumber) {
            throw "No unallocated gap >= $(Format-ByteSize $desiredGapBytes) on Disk $DiskNumber. Free up space in Disk Management first."
        }
        $supported = Get-PartitionSupportedSize -DiskNumber $DiskNumber -PartitionNumber $osPart.PartitionNumber
        $maxShrinkBytes = $osPart.Size - $supported.SizeMin
        $osEnd = [long]$osPart.Offset + [long]$osPart.Size
        $adjacentGap = @($gapInfo.Gaps | Where-Object { $_.StartOffset -ge $osEnd -and ($_.StartOffset - $osEnd) -le 1048576 } | Select-Object -First 1)[0]
        $adjacentBytes = if ($adjacentGap) { $adjacentGap.SizeBytes } else { 0L }

        if ($ShrinkOSVolumeGB -gt 0) {
            $neededBytes = [long]$ShrinkOSVolumeGB * 1GB
            if (($neededBytes + $adjacentBytes) -lt $minBytes) {
                throw "ShrinkOSVolumeGB ($ShrinkOSVolumeGB GB) plus adjacent free space is smaller than minimum required ($([math]::Ceiling($minBytes / 1GB)) GB)."
            }
            if ($neededBytes -gt $maxShrinkBytes) {
                throw "Requested ShrinkOSVolumeGB ($ShrinkOSVolumeGB GB) exceeds maximum shrinkable space ($(Format-ByteSize $maxShrinkBytes)) on ${osDrive}:."
            }
        } elseif ($DataSizeGB -gt 0) {
            $neededBytes = [Math]::Max(0L, $desiredGapBytes - $adjacentBytes + 16MB)
            if ($neededBytes -gt $maxShrinkBytes) {
                throw "Requested DataSizeGB ($DataSizeGB GB) exceeds available space on ${osDrive}: (max shrinkable $(Format-ByteSize $maxShrinkBytes))."
            }
        } else {
            $neededBytes = $minBytes + 16MB
            $preferred = 32GB
            if ($maxShrinkBytes -ge $preferred) { $neededBytes = $preferred }
            if ($maxShrinkBytes -lt ($minBytes + 16MB)) {
                throw "Windows OS partition (${osDrive}:) can only shrink by $(Format-ByteSize $maxShrinkBytes), which is below the minimum $(Format-ByteSize $minBytes) required."
            }
            if ($neededBytes -gt $maxShrinkBytes) {
                $neededBytes = Align-Down $maxShrinkBytes 1048576
            }
        }
        $newOsSize = Align-Down ($osPart.Size - $neededBytes) 4096
        Write-Host ("Shrinking Windows volume {0}: from {1} to {2} (freeing {3})..." -f `
            $osDrive, (Format-ByteSize $osPart.Size), (Format-ByteSize $newOsSize), (Format-ByteSize ($osPart.Size - $newOsSize))) -ForegroundColor Yellow
        if (-not $PSCmdlet.ShouldProcess("Partition ${osDrive}: on Disk $DiskNumber", "Resize-Partition to $(Format-ByteSize $newOsSize) and install Googlebook OS dual-boot")) {
            return
        }
        Resize-Partition -DiskNumber $DiskNumber -PartitionNumber $osPart.PartitionNumber -Size $newOsSize
        Update-Disk -Number $DiskNumber
        Start-Sleep -Seconds 1
        $gapInfo = Get-DiskUnallocatedGaps $DiskNumber
        $gap = @($gapInfo.Gaps | Where-Object { $_.SizeBytes -ge $desiredGapBytes } | Sort-Object SizeBytes -Descending)[0]
        if (-not $gap) {
            throw "Failed to locate sufficient unallocated space after shrinking ${osDrive}:."
        }
    }

    if ($gap.HasPartitionAfter) {
        Write-Warning 'A Windows partition (such as Recovery/WinRE) exists physically after the unallocated gap. userdata will fill the selected gap; latest main grow-image-userdata supports this layout.'
    }

    $plan = Build-SameDiskPartitionPlan -ImageInfo $ImageInfo -Gap $gap -RequestedDataSizeGB $DataSizeGB
    $layout = $gapInfo.Layout
    if (($layout.Partitions.Count + $plan.Count) -gt $layout.MaxPartitionCount) {
        throw "Not enough free GPT partition slots on Disk $DiskNumber."
    }

    Write-Host ''
    Write-Host "Planned Googlebook OS partitions on Disk ${DiskNumber}:" -ForegroundColor Cyan
    foreach ($p in $plan) {
        Write-Host ("  {0,-26} {1,10}  @{2}" -f $p.Name, (Format-ByteSize $p.TargetSizeBytes), (Format-ByteSize $p.TargetOffsetBytes))
    }
    Write-Host ''

    if (-not $PSCmdlet.ShouldProcess("Disk $DiskNumber", "Create $($plan.Count) GPT partitions and write Googlebook OS payload")) {
        return
    }

    foreach ($p in $layout.Partitions) { $p.RewritePartition = $false }
    foreach ($np in $plan) {
        $entry = New-Object SpxDiskPartitionEntry
        $entry.StartingOffset = $np.TargetOffsetBytes
        $entry.PartitionLength = $np.TargetSizeBytes
        $entry.PartitionNumber = 0
        $entry.RewritePartition = $true
        $entry.IsServicePartition = $false
        $entry.PartitionType = $np.InitialTypeGuid
        $entry.PartitionId = [Guid]::NewGuid()
        $entry.Attributes = $np.Attributes
        $entry.Name = $np.Name
        $layout.Partitions.Add($entry)
    }

    [SpxDualBootNative]::SetDriveLayout($DiskNumber, $layout)
    Update-Disk -Number $DiskNumber
    Start-Sleep -Seconds 1

    $createdLayout = [SpxDualBootNative]::GetDriveLayout($DiskNumber)
    $copyTargets = New-Object System.Collections.Generic.List[SpxPartitionCopyTarget]
    $sq2PartNum = -1
    foreach ($np in $plan) {
        $matched = @($createdLayout.Partitions | Where-Object { $_.StartingOffset -eq $np.TargetOffsetBytes -and $_.PartitionLength -eq $np.TargetSizeBytes -and $_.Name -eq $np.Name })
        if ($matched.Count -ne 1) {
            throw "Post-create GPT verification failed for partition $($np.Name)."
        }
        $pNum = [int]$matched[0].PartitionNumber
        if ($np.Name -eq 'SQ2PROBE') { $sq2PartNum = $pNum }
        $t = New-Object SpxPartitionCopyTarget
        $t.Name = $np.Name
        $t.SourceOffsetBytes = $np.SourceOffsetBytes
        $t.CopyBytes = $np.SourceSizeBytes
        $t.TargetDevicePath = "\\?\GLOBALROOT\Device\Harddisk${DiskNumber}\Partition${pNum}"
        $copyTargets.Add($t)
    }
    if ($sq2PartNum -le 0) { throw 'Could not resolve SQ2PROBE partition number.' }

    Write-Host 'Streaming Googlebook OS partitions from image...' -ForegroundColor Yellow
    $script:CopyProgressState = @{
        Name  = ''
        Watch = [Diagnostics.Stopwatch]::StartNew()
    }
    [SpxDualBootNative]::CopyPartitionsFromImage($ImageInfo.Path, $copyTargets.ToArray(), {
        param($name, $done, $total)
        if ($name -ne $script:CopyProgressState.Name -or $script:CopyProgressState.Watch.ElapsedMilliseconds -ge 1000 -or $done -eq $total) {
            $script:CopyProgressState.Name = $name
            $pct = if ($total -gt 0) { [int](($done * 100) / $total) } else { 100 }
            Write-Progress -Activity 'Writing Googlebook OS Partitions' -Status "$name ($pct%)" -PercentComplete $pct
            $script:CopyProgressState.Watch.Restart()
        }
    })
    Write-Progress -Activity 'Writing Googlebook OS Partitions' -Completed
    Update-Disk -Number $DiskNumber
    Start-Sleep -Milliseconds 500

    Invoke-ConfigureDualBoot -Sq2DiskNumber $DiskNumber -Sq2PartNumber $sq2PartNum -SelectedDefaultOS $SelectedDefaultOS -SelectedTimeout $SelectedTimeout -SelectedEspMode $SelectedEspMode
    Write-Host ''
    Write-Host "SUCCESS: Windows + Googlebook OS dual-boot is installed on Disk $DiskNumber." -ForegroundColor Green
}

function Invoke-FullDiskInstall {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        $ImageInfo,
        [int]$DiskNumber,
        [string]$SelectedDefaultOS = 'GooglebookOS',
        [int]$SelectedTimeout = 5,
        [string]$SelectedEspMode = 'Auto'
    )
    Assert-Administrator
    $disk = Get-Disk -Number $DiskNumber -ErrorAction Stop
    if ($disk.IsBoot -or $disk.IsSystem) {
        throw "Refusing FullDisk mode on Windows boot/system Disk $DiskNumber! Use SameDisk mode for the internal OS disk."
    }
    if ($disk.LogicalSectorSize -ne 512) { throw 'Target disk must have 512-byte logical sectors.' }
    if ($disk.Size -lt $ImageInfo.LogicalImageBytes) {
        throw "Target disk ($(Format-ByteSize $disk.Size)) is smaller than image ($(Format-ByteSize $ImageInfo.LogicalImageBytes))."
    }

    $expectedToken = "ERASE $DiskNumber $($disk.Size)"
    Write-Host ''
    Write-Host 'FULL-DISK ERASE TARGET:' -ForegroundColor Red
    Write-Host ("  Disk {0}: {1} | Bus={2} | Size={3} ({4} bytes)" -f $DiskNumber, $disk.FriendlyName, $disk.BusType, (Format-ByteSize $disk.Size), $disk.Size) -ForegroundColor Red
    Write-Host '  ALL existing partitions and data on this disk will be permanently replaced.' -ForegroundColor Red
    if (-not $ConfirmErase) {
        $ConfirmErase = Read-Host "Type '$expectedToken' to confirm erasing Disk $DiskNumber"
    }
    if ($ConfirmErase -cne $expectedToken) {
        throw 'Erase confirmation mismatch; no disk writes performed.'
    }

    if (-not $PSCmdlet.ShouldProcess("Disk $DiskNumber", 'Overwrite full disk with Googlebook OS image and configure dual-boot menu')) {
        return
    }

    Set-Disk -Number $DiskNumber -IsOffline $true
    Set-Disk -Number $DiskNumber -IsReadOnly $false
    try {
        $script:CopyProgressState = @{
            Name  = 'full'
            Watch = [Diagnostics.Stopwatch]::StartNew()
        }
        [SpxDualBootNative]::WriteFullImageToDisk($ImageInfo.Path, $DiskNumber, $ImageInfo.LogicalImageBytes, {
            param($done, $total)
            if ($script:CopyProgressState.Watch.ElapsedMilliseconds -ge 1000 -or $done -eq $total) {
                $pct = [int](($done * 100) / $total)
                Write-Progress -Activity "Flashing Full Image to Disk $DiskNumber" -Status "$pct% ($(Format-ByteSize $done) / $(Format-ByteSize $total))" -PercentComplete $pct
                $script:CopyProgressState.Watch.Restart()
            }
        })
        Write-Progress -Activity "Flashing Full Image to Disk $DiskNumber" -Completed
    } finally {
        Set-Disk -Number $DiskNumber -IsOffline $false
        Update-Disk -Number $DiskNumber
    }

    Start-Sleep -Seconds 1
    $probes = @(Find-Sq2ProbePartitions | Where-Object { $_.DiskNumber -eq $DiskNumber })
    if ($probes.Count -ne 1) {
        throw "Expected one SQ2PROBE partition on Disk $DiskNumber after flashing."
    }
    $sq2PartNum = [int]$probes[0].PartitionNumber
    $effectiveEspMode = if ($SelectedEspMode -eq 'Auto') { 'Sq2ProbeDirect' } else { $SelectedEspMode }
    Invoke-ConfigureDualBoot -Sq2DiskNumber $DiskNumber -Sq2PartNumber $sq2PartNum -SelectedDefaultOS $SelectedDefaultOS -SelectedTimeout $SelectedTimeout -SelectedEspMode $effectiveEspMode
    Write-Host ''
    Write-Host "SUCCESS: Full Googlebook OS image flashed to Disk $DiskNumber with Windows dual-boot menu." -ForegroundColor Green
}

function Invoke-UninstallDualBoot {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param()
    Assert-Administrator
    $winEsp = $null
    $recordedGuid = $null
    try {
        $winEsp = Mount-WindowsEsp
        $shimDir = Join-Path $winEsp.Root $script:ShimRelativeDir
        $statePath = Join-Path $shimDir 'dualboot-state.json'
        if (Test-Path -LiteralPath $statePath) {
            try {
                $st = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
                if ($st.bcd_guid) { $recordedGuid = [string]$st.bcd_guid }
            } catch {}
        }
        $guid = Find-ExistingGooglebookBcdGuid $recordedGuid
        if ($guid) {
            if ($PSCmdlet.ShouldProcess($guid, 'Restore Windows Boot Manager first and delete Googlebook OS UEFI entry')) {
                $null = & bcdedit.exe /set '{fwbootmgr}' displayorder '{bootmgr}' /addfirst 2>&1
                $delOut = (& bcdedit.exe /delete $guid 2>&1) -join "`n"
                if ($LASTEXITCODE -ne 0) { Write-Warning "bcdedit /delete $guid returned: $delOut" }
                else { Write-Host "Removed UEFI boot entry $guid." -ForegroundColor Green }
            }
        } else {
            Write-Host 'No Googlebook OS UEFI boot entry found.'
        }
        if (Test-Path -LiteralPath $shimDir) {
            if ($PSCmdlet.ShouldProcess($shimDir, 'Remove GRUB shim from Windows ESP')) {
                Remove-Item -LiteralPath $shimDir -Recurse -Force
                Write-Host "Removed $shimDir." -ForegroundColor Green
            }
        }
    } finally {
        if ($winEsp) { Dismount-WindowsEsp $winEsp.Letter }
    }

    if (-not $KeepPartitionsOnUninstall) {
        $probes = @(Find-Sq2ProbePartitions)
        if ($probes.Count -gt 0) {
            $diskNum = if ($TargetDiskNumber -ge 0) { $TargetDiskNumber } else { $probes[0].DiskNumber }
            $layout = [SpxDualBootNative]::GetDriveLayout($diskNum)
            $toDelete = @($layout.Partitions | Where-Object { $script:KnownGooglebookPartNames -contains $_.Name })
            if ($toDelete.Count -gt 0) {
                Write-Host "Googlebook OS partitions on Disk ${diskNum}: $(($toDelete | ForEach-Object { $_.Name }) -join ', ')" -ForegroundColor Yellow
                if ($PSCmdlet.ShouldProcess("Disk $diskNum", "Delete $($toDelete.Count) Googlebook OS partitions")) {
                    $keep = New-Object System.Collections.Generic.List[SpxDiskPartitionEntry]
                    foreach ($p in $layout.Partitions) {
                        if ($script:KnownGooglebookPartNames -notcontains $p.Name) {
                            $p.RewritePartition = $false
                            $keep.Add($p)
                        }
                    }
                    $layout.Partitions = $keep
                    [SpxDualBootNative]::SetDriveLayout($diskNum, $layout)
                    Update-Disk -Number $diskNum
                    Write-Host "Deleted Googlebook OS partitions on Disk $diskNum. You can extend Windows C: in Disk Management." -ForegroundColor Green
                }
            }
        }
    }
}

function Invoke-MainAction {
    param(
        [string]$SelectedAction,
        [string]$SelectedMode = $InstallMode,
        [string]$SelectedDefaultOS = $DefaultOS
    )
    switch ($SelectedAction) {
        'Status' {
            Show-DualBootStatus
        }
        'Install' {
            Assert-Administrator
            $secBoot = Get-SecureBootState
            if ($secBoot -eq 'Enabled' -and -not $SkipSecureBootCheck) {
                Write-Warning 'UEFI Secure Boot on this PC is ENABLED. Surface Pro X must have Secure Boot DISABLED in Surface UEFI before booting Googlebook OS (unsigned ARM64 EFI).'
                $ans = Read-Host 'Continue installation anyway? (y/N)'
                if ($ans -notin @('y', 'Y', 'yes', 'YES')) { return }
            }
            $img = $ImagePath
            if (-not $img) {
                $img = Resolve-DefaultImagePath
                if (-not $img) {
                    $img = (Read-Host 'Enter path to surface-pro-x-6.18-hybrid-generic.img or .img.gz').Trim('"')
                }
            }
            Write-Host "Inspecting image GPT: $img" -ForegroundColor Cyan
            $imgInfo = Get-ValidatedImageInfo $img
            Write-Host ("Image valid: {0} partitions, logical size {1}" -f $imgInfo.Partitions.Count, (Format-ByteSize $imgInfo.LogicalImageBytes))

            $diskNum = $TargetDiskNumber
            if ($diskNum -lt 0) {
                Show-DualBootStatus
                $diskNum = [int](Read-Host 'Enter Target Disk Number (e.g. 0 for internal NVMe dual-boot)')
            }

            $disk = Get-Disk -Number $diskNum -ErrorAction Stop
            $mode = $SelectedMode
            if ($mode -eq 'Auto') {
                $mode = if ($disk.IsBoot -or $disk.IsSystem) { 'SameDisk' } else { 'FullDisk' }
            }
            if ($mode -eq 'SameDisk') {
                Invoke-SameDiskInstall -ImageInfo $imgInfo -DiskNumber $diskNum -SelectedDefaultOS $SelectedDefaultOS -SelectedTimeout $TimeoutSeconds -SelectedEspMode $EspMode
            } else {
                Invoke-FullDiskInstall -ImageInfo $imgInfo -DiskNumber $diskNum -SelectedDefaultOS $SelectedDefaultOS -SelectedTimeout $TimeoutSeconds -SelectedEspMode $EspMode
            }
        }
        'ConfigureBoot' {
            Assert-Administrator
            $probes = @(Find-Sq2ProbePartitions)
            if ($TargetDiskNumber -ge 0) {
                $probes = @($probes | Where-Object { $_.DiskNumber -eq $TargetDiskNumber })
            }
            if ($probes.Count -ne 1) {
                throw "Expected exactly one SQ2PROBE partition (found $($probes.Count)). Specify -TargetDiskNumber."
            }
            Invoke-ConfigureDualBoot -Sq2DiskNumber $probes[0].DiskNumber -Sq2PartNumber $probes[0].PartitionNumber -SelectedDefaultOS $SelectedDefaultOS -SelectedTimeout $TimeoutSeconds -SelectedEspMode $EspMode
        }
        'SwitchDefault' {
            Assert-Administrator
            $probes = @(Find-Sq2ProbePartitions)
            if ($TargetDiskNumber -ge 0) {
                $probes = @($probes | Where-Object { $_.DiskNumber -eq $TargetDiskNumber })
            }
            if ($probes.Count -ne 1) {
                throw "Expected exactly one SQ2PROBE partition (found $($probes.Count)). Specify -TargetDiskNumber."
            }
            Invoke-ConfigureDualBoot -Sq2DiskNumber $probes[0].DiskNumber -Sq2PartNumber $probes[0].PartitionNumber -SelectedDefaultOS $SelectedDefaultOS -SelectedTimeout $TimeoutSeconds -SelectedEspMode $EspMode
        }
        'Uninstall' {
            Invoke-UninstallDualBoot
        }
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    if ($Action -eq 'Menu') {
        while ($true) {
            Show-DualBootStatus
            Write-Host 'Select an action / 请选择操作:' -ForegroundColor Cyan
            Write-Host '  [1] Install Dual-Boot on Internal Disk / 本机硬盘安装 Windows + Googlebook OS 双系统'
            Write-Host '  [2] Flash External USB Disk with Dual-Boot / 写入外置 USB 硬盘并配置双启动'
            Write-Host '  [3] Repair Boot & Set Googlebook OS Default / 修复引导并设为默认启动 Googlebook OS'
            Write-Host '  [4] Repair Boot & Set Windows Default / 修复引导并设为默认启动 Windows'
            Write-Host '  [5] Uninstall Dual-Boot / 卸载 Googlebook OS 双启动'
            Write-Host '  [0] Exit / 退出'
            $choice = Read-Host 'Choice'
            try {
                switch ($choice) {
                    '1' { Invoke-MainAction -SelectedAction 'Install' -SelectedMode 'SameDisk' -SelectedDefaultOS $DefaultOS }
                    '2' { Invoke-MainAction -SelectedAction 'Install' -SelectedMode 'FullDisk' -SelectedDefaultOS $DefaultOS }
                    '3' { Invoke-MainAction -SelectedAction 'SwitchDefault' -SelectedDefaultOS 'GooglebookOS' }
                    '4' { Invoke-MainAction -SelectedAction 'SwitchDefault' -SelectedDefaultOS 'Windows' }
                    '5' { Invoke-MainAction -SelectedAction 'Uninstall' }
                    '0' { return }
                    default { Write-Warning "Invalid selection: $choice" }
                }
            } catch {
                Write-Host ("ERROR: {0}" -f $_.Exception.Message) -ForegroundColor Red
            }
            Write-Host ''
            $null = Read-Host 'Press Enter to continue'
        }
    } else {
        Invoke-MainAction -SelectedAction $Action -SelectedMode $InstallMode -SelectedDefaultOS $DefaultOS
    }
}
