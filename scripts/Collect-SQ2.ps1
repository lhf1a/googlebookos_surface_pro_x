#requires -Version 5.1
<#
Read-only collection from a Surface Pro X SQ2 Windows installation.
Writes only to a new output directory. Does not change partitions, boot entries,
BitLocker or firmware. Firmware stays private; do not upload it to a public repo.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path $env:USERPROFILE ('Desktop\sq2-collection-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [switch]$CheckOnly,
    [switch]$SkipFirmware
)
$ErrorActionPreference = 'Stop'
$computer = Get-CimInstance Win32_ComputerSystem
$processor = @(Get-CimInstance Win32_Processor)
$eligible = ($computer.Model -eq 'Surface Pro X') -and ($processor[0].Name -match '\bSQ2\b')
$identity = [ordered]@{
    model = $computer.Model
    manufacturer = $computer.Manufacturer
    sku = $computer.SystemSKUNumber
    cpu = $processor[0].Name
    memoryBytes = $computer.TotalPhysicalMemory
    eligibleSQ2 = $eligible
}
if ($CheckOnly) { $identity | ConvertTo-Json; return }
if (-not $eligible) { throw 'This collector is for Surface Pro X SQ2 only. No files collected.' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Output directory already exists. Choose a new directory.' }
$null = New-Item -ItemType Directory -Path $output
$identity | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'identity.json') -Encoding UTF8
$osInfo = Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture
$biosInfo = Get-CimInstance Win32_BIOS | Select-Object Manufacturer,SMBIOSBIOSVersion,ReleaseDate
[ordered]@{ os = $osInfo; bios = $biosInfo } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'versions.json') -Encoding UTF8
Get-CimInstance Win32_PnPSignedDriver | Select-Object DeviceName,DeviceClass,Manufacturer,DriverVersion,DriverDate,InfName,HardwareID | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'drivers.json') -Encoding UTF8
Get-Disk | Select-Object Number,FriendlyName,BusType,PartitionStyle,Size,IsBoot,IsSystem | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'disks.json') -Encoding UTF8
Get-Partition | Select-Object DiskNumber,PartitionNumber,DriveLetter,Type,GptType,Size,Offset | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'partitions.json') -Encoding UTF8
try { $secureBoot = [string](Confirm-SecureBootUEFI) } catch { $secureBoot = 'Not readable: ' + $_.Exception.Message }
$secureBoot | Set-Content -LiteralPath (Join-Path $output 'secure-boot.txt') -Encoding UTF8
$manifest = @()
$missing = @()
if (-not $SkipFirmware) {
    $store = Join-Path $env:windir 'System32\DriverStore\FileRepository'
    $prefixes = @('qcbtfmuart8180', 'qcdx8180', 'qcwlan8180', 'surfaceprox_mcfg', 'surfaceprox_subextadsp', 'surfaceprox_subextcdsp', 'surfaceprox_subextmpss')
    $destination = Join-Path $output 'windows-root\Windows\System32\DriverStore\FileRepository'
    $null = New-Item -ItemType Directory -Path $destination -Force
    $directories = @(Get-ChildItem -LiteralPath $store -Directory)
    foreach ($prefix in $prefixes) {
        $matches = @($directories | Where-Object { $_.Name.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) })
        if ($matches.Count -eq 0) { $missing += $prefix; continue }
        foreach ($directory in $matches) {
            # Preserve every installed version for explicit selection during extraction.
            Copy-Item -LiteralPath $directory.FullName -Destination $destination -Recurse
        }
    }
    $manifest = @(Get-ChildItem -LiteralPath $destination -Recurse -File | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($output.Length + 1); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
[ordered]@{ files = $manifest; missingPackages = $missing; firmwareSkipped = [bool]$SkipFirmware } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'firmware-manifest.json') -Encoding UTF8
if ($missing.Count) { Write-Warning ('Missing packages: ' + ($missing -join ', ')) }
Write-Output ('Collection saved to: ' + $output)
Write-Output 'Send identity.json, versions.json and firmware-manifest.json first. Keep firmware private.'
