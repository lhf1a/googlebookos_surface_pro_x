#requires -Version 5.1
param([string]$TempDir)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
. "$repo/tools/Install-GooglebookDualBoot.ps1"

if (-not $TempDir) {
    $TempDir = Join-Path ([IO.Path]::GetTempPath()) ('spx-dualboot-test-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
}
$null = New-Item -ItemType Directory -Path $TempDir -Force

try {
    # 1. Build a minimal valid synthetic GPT disk image (.img and .img.gz).
    $sectorSize = 512
    $totalSectors = 20480
    $rawPath = Join-Path $TempDir 'fixture.img'
    $gzPath = Join-Path $TempDir 'fixture.img.gz'

    $buf = New-Object byte[] ($totalSectors * $sectorSize)
    $buf[510] = 0x55
    $buf[511] = 0xaa

    $parts = @(
        @{ Index = 1; Name = 'super';    First = 2048;  Last = 6143;  Type = $script:GptBasicDataGuid },
        @{ Index = 2; Name = 'metadata'; First = 6144;  Last = 6271;  Type = $script:GptBasicDataGuid },
        @{ Index = 3; Name = 'misc';     First = 6272;  Last = 6399;  Type = $script:GptBasicDataGuid },
        @{ Index = 4; Name = 'SQ2PROBE'; First = 8192;  Last = 12287; Type = $script:GptEspGuid },
        @{ Index = 5; Name = 'userdata'; First = 12288; Last = 20440; Type = $script:GptBasicDataGuid }
    )

    $tableBytes = New-Object byte[] (128 * 128)
    foreach ($p in $parts) {
        $pos = ($p.Index - 1) * 128
        [Buffer]::BlockCopy($p.Type.ToByteArray(), 0, $tableBytes, $pos + 0, 16)
        [Buffer]::BlockCopy([Guid]::NewGuid().ToByteArray(), 0, $tableBytes, $pos + 16, 16)
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint64]$p.First), 0, $tableBytes, $pos + 32, 8)
        [Buffer]::BlockCopy([BitConverter]::GetBytes([uint64]$p.Last), 0, $tableBytes, $pos + 40, 8)
        $nameBytes = [Text.Encoding]::Unicode.GetBytes($p.Name)
        [Buffer]::BlockCopy($nameBytes, 0, $tableBytes, $pos + 56, [Math]::Min($nameBytes.Length, 72))
    }
    $tableCrc = [SpxDualBootNative]::ComputeCrc32($tableBytes, 0, $tableBytes.Length)
    [Buffer]::BlockCopy($tableBytes, 0, $buf, 1024, $tableBytes.Length)

    $hdr = New-Object byte[] 92
    [Buffer]::BlockCopy([Text.Encoding]::ASCII.GetBytes('EFI PART'), 0, $hdr, 0, 8)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]0x00010000), 0, $hdr, 8, 4)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]92), 0, $hdr, 12, 4)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint64]1), 0, $hdr, 24, 8)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint64]($totalSectors - 1)), 0, $hdr, 32, 8)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint64]34), 0, $hdr, 40, 8)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint64]($totalSectors - 34)), 0, $hdr, 48, 8)
    [Buffer]::BlockCopy([Guid]::NewGuid().ToByteArray(), 0, $hdr, 56, 16)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint64]2), 0, $hdr, 72, 8)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]128), 0, $hdr, 80, 4)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]128), 0, $hdr, 84, 4)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]$tableCrc), 0, $hdr, 88, 4)
    $hdrCrc = [SpxDualBootNative]::ComputeCrc32($hdr, 0, 92)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]$hdrCrc), 0, $hdr, 16, 4)
    [Buffer]::BlockCopy($hdr, 0, $buf, 512, 92)

    [IO.File]::WriteAllBytes($rawPath, $buf)
    $fsOut = [IO.File]::Create($gzPath)
    try {
        $gz = New-Object System.IO.Compression.GZipStream($fsOut, [System.IO.Compression.CompressionMode]::Compress)
        try { $gz.Write($buf, 0, $buf.Length) } finally { $gz.Dispose() }
    } finally {
        $fsOut.Dispose()
    }

    # 2. Verify GPT parsing for both .img and .img.gz.
    foreach ($imgFile in @($rawPath, $gzPath)) {
        $parsed = Get-ValidatedImageInfo $imgFile
        if ($parsed.Partitions.Count -ne 5) { throw "Expected 5 partitions in $imgFile" }
        if ($parsed.LogicalImageBytes -ne ($totalSectors * $sectorSize)) { throw "Wrong logical size in $imgFile" }
    }

    # 3. Verify SameDisk partition planning puts userdata last and respects gap bounds.
    $parsedImg = Get-ValidatedImageInfo $gzPath
    $gap = [pscustomobject]@{
        StartOffset = [long](100 * 1MB)
        EndOffset   = [long](132 * 1MB)
        SizeBytes   = [long](32 * 1MB)
        HasPartitionAfter = $true
    }
    $plan = Build-SameDiskPartitionPlan -ImageInfo $parsedImg -Gap $gap -RequestedDataSizeGB 0
    if ($plan[-1].Name -ne 'userdata') { throw 'userdata must be placed last in plan' }
    if (($plan[-1].TargetOffsetBytes + $plan[-1].TargetSizeBytes) -gt $gap.EndOffset) {
        throw 'Planned userdata exceeds gap end'
    }
    $sq2Plan = @($plan | Where-Object { $_.Name -eq 'SQ2PROBE' })[0]
    if ($sq2Plan.InitialTypeGuid -ne $script:GptBasicDataGuid -or $sq2Plan.FinalTypeGuid -ne $script:GptEspGuid) {
        throw 'SQ2PROBE initial/final GPT type mismatch'
    }

    # 4. Verify dual-boot grub.cfg generation (LF only, no BOM, both OS entries).
    $cfg = New-DualBootGrubConfig -Sq2ProbeUuid '9CF3-A1A5' -WindowsEspUuid 'B027-0512' -DefaultOS 'Windows' -TimeoutSeconds 7
    if ($cfg.Contains("`r")) { throw 'grub.cfg must use LF line endings only' }
    if ($cfg -notmatch 'set default=windows' -or $cfg -notmatch 'set timeout=7') { throw 'GRUB default/timeout mismatch' }
    if ($cfg -notmatch 'bootmgfw\.efi' -or $cfg -notmatch '/EFI/SQ2Nat/start\.efi') { throw 'Missing OS menuentry' }

    $cfgOut = Join-Path $TempDir 'grub.cfg'
    Write-Utf8NoBomLf $cfgOut $cfg
    $rawBytes = [IO.File]::ReadAllBytes($cfgOut)
    if ($rawBytes.Length -ge 3 -and $rawBytes[0] -eq 0xef -and $rawBytes[1] -eq 0xbb -and $rawBytes[2] -eq 0xbf) {
        throw 'grub.cfg must NOT have a UTF-8 BOM'
    }

    Write-Output 'PASS: Googlebook dual-boot PowerShell/C# parser, planner, and GRUB tests passed'
} finally {
    Remove-Item -LiteralPath $TempDir -Recurse -Force -ErrorAction SilentlyContinue
}
