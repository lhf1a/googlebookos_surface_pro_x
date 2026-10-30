#requires -Version 5.1
<#
Read-only Windows reference collection for Surface Pro X SQ1/SQ2 video work.
Writes only a new output directory/archive. Does not install drivers, change
services/boot/security/network settings, or read credentials/Wi-Fi profiles.
Keep the resulting driver binaries and raw hardware data private.
#>
[CmdletBinding()]
param(
    [ValidateSet('SQ1','SQ2')][string]$ExpectedProcessor = 'SQ1',
    [string]$OutputDirectory = (Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) ('surface-video-reference-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [switch]$CheckOnly,
    [switch]$AllDrivers,
    [switch]$SkipDeviceResources,
    [switch]$NoArchive
)
$ErrorActionPreference = 'Stop'
$computer = Get-CimInstance Win32_ComputerSystem
$processors = @(Get-CimInstance Win32_Processor)
if (-not $processors.Count) { throw 'CPU identity is unavailable.' }
$eligible = $computer.Model -eq 'Surface Pro X' -and
    $processors[0].Name -match ('\b' + $ExpectedProcessor + '\b') -and
    [int]$processors[0].Architecture -eq 12
$identity = [ordered]@{
    model = $computer.Model; manufacturer = $computer.Manufacturer
    cpu = $processors[0].Name; processorArchitecture = $processors[0].Architecture
    expectedProcessor = $ExpectedProcessor; eligible = [bool]$eligible
    memoryBytes = $computer.TotalPhysicalMemory
}
if ($CheckOnly) { $identity | ConvertTo-Json; return }
if (-not $eligible) { throw "Run on the Surface Pro X $ExpectedProcessor. Identity mismatch; no collection created." }
if (-not [Environment]::Is64BitProcess) { throw 'Use a native 64-bit PowerShell process for driver collection.' }
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open PowerShell as Administrator on the Surface and run again.'
}
$output = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$windowsPath = [IO.Path]::GetFullPath($env:windir).TrimEnd('\')
if ($output.Equals($windowsPath, [StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($windowsPath + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $output -eq [IO.Path]::GetPathRoot($output).TrimEnd('\')) {
    throw 'Choose a new collection directory outside Windows and the drive root.'
}
$archive = $output + '.zip'
if ((Test-Path -LiteralPath $output) -or (Test-Path -LiteralPath $archive)) {
    throw 'Output directory or archive already exists; choose a new directory.'
}
$null = New-Item -ItemType Directory -Path $output
$issues = New-Object 'System.Collections.Generic.List[object]'
function Save-Json([string]$Name, $Value) {
    ConvertTo-Json -InputObject $Value -Depth 8 | Set-Content -LiteralPath (Join-Path $output $Name) -Encoding UTF8
}
function Record-Issue([string]$Step, [string]$Message) {
    $issues.Add([ordered]@{ step = $Step; message = $Message })
    Write-Warning ($Step + ': ' + $Message)
}
Save-Json 'identity.json' $identity
$os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture
$bios = Get-CimInstance Win32_BIOS | Select-Object Manufacturer,SMBIOSBIOSVersion,ReleaseDate
Save-Json 'versions.json' ([ordered]@{ os = $os; bios = $bios })
$signedDrivers = @(Get-CimInstance Win32_PnPSignedDriver)
Save-Json 'active-drivers.json' @($signedDrivers | Select-Object DeviceName,DeviceClass,Manufacturer,DriverVersion,DriverDate,InfName,HardwareID)
$activeInfs = @($signedDrivers | ForEach-Object { $_.InfName } | Where-Object { $_ } | Select-Object -Unique)

# Export complete third-party packages using the supported Windows operation.
# Get-WindowsDriver gives structured original/published INF mapping; do not parse
# localized pnputil text to choose a package.
$prefixPattern = '^(qcdx8180|qcvss8180|qcvenus8180|qcvpu8180|qcsmmu|qciommu|qcpep|qcscm|qcrpmh|qcpil)'
$packages = @()
try {
    $packages = @(Get-WindowsDriver -Online -All)
    Save-Json 'driver-packages.json' @($packages | Select-Object Driver,OriginalFileName,ProviderName,ClassName,Version,Date,Inbox)
} catch { Record-Issue 'driver-inventory' $_.Exception.Message }
$store = Join-Path $env:windir 'System32\DriverStore\FileRepository'
$packageResults = New-Object 'System.Collections.Generic.List[object]'
$exportedSources = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$driverOutput = Join-Path $output 'drivers'
$null = New-Item -ItemType Directory -Path $driverOutput
$nativePnp = Join-Path $env:windir 'System32\pnputil.exe'
foreach ($package in $packages) {
    $original = [IO.Path]::GetFileName([string]$package.OriginalFileName)
    $selected = $AllDrivers -or $original -match $prefixPattern -or $package.ClassName -eq 'Display'
    if (-not $selected -or $package.Driver -notmatch '^oem\d+\.inf$') { continue }
    $target = Join-Path $driverOutput $package.Driver
    $null = New-Item -ItemType Directory -Path $target
    Write-Output ('Exporting ' + $package.Driver + ' (' + $original + ')')
    $nativeExit = -1
    $nativeOutput = @()
    try {
        $nativeOutput = @(& $nativePnp /export-driver $package.Driver $target 2>&1)
        $nativeExit = $LASTEXITCODE
    } catch {
        $nativeOutput += $_.Exception.Message
        Record-Issue ('export exception ' + $package.Driver) $_.Exception.Message
    }
    $nativeOutput | Out-File -LiteralPath (Join-Path $target 'export-result.txt') -Encoding UTF8
    $packageResults.Add([ordered]@{
        publishedInf = $package.Driver; originalInf = $original
        provider = $package.ProviderName; version = [string]$package.Version
        active = $activeInfs -contains $package.Driver
        method = 'pnputil-export'; exitCode = $nativeExit
        destination = 'drivers/' + $package.Driver
    })
    if ($nativeExit -eq 0) {
        $null = $exportedSources.Add([IO.Path]::GetFullPath((Split-Path -Parent $package.OriginalFileName)))
    } else { Record-Issue ('export ' + $package.Driver) ('pnputil exit ' + $nativeExit) }
}

# Preserve versions not represented by successful exports. Read only explicitly
# scoped package directories; never follow junctions out of the Driver Store.
$rawOutput = Join-Path $output 'driver-store'
$null = New-Item -ItemType Directory -Path $rawOutput
foreach ($directory in @(Get-ChildItem -LiteralPath $store -Directory | Where-Object { $AllDrivers -or $_.Name -match $prefixPattern })) {
    if ($exportedSources.Contains($directory.FullName)) { continue }
    try {
        if ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Package contains a reparse point; skipped.'
        }
        $pending = New-Object 'System.Collections.Generic.Stack[string]'
        $pending.Push($directory.FullName)
        while ($pending.Count) {
            foreach ($entry in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)) {
                if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw 'Package contains a reparse point; skipped before traversal.'
                }
                if ($entry.PSIsContainer) { $pending.Push($entry.FullName) }
            }
        }
        Copy-Item -LiteralPath $directory.FullName -Destination $rawOutput -Recurse
        $packageResults.Add([ordered]@{ sourceDirectory = $directory.Name; method = 'read-only-store-copy'; destination = 'driver-store/' + $directory.Name })
    } catch { Record-Issue ('copy ' + $directory.Name) $_.Exception.Message }
}
Save-Json 'exported-packages.json' @($packageResults.ToArray())

# PnP resources help identify the real VPU/GPU register and interrupt mappings.
$devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.PNPDeviceID -like 'ACPI\QCOM*' -or $_.PNPClass -eq 'Display' -or
    $_.Name -match 'Adreno|Venus|SMMU|IOMMU|Qualcomm.*(Video|Graphic|Platform|Power)|Surface.*(System|Firmware)'
})
$deviceResults = New-Object 'System.Collections.Generic.List[object]'
foreach ($device in $devices) {
    $resources = @()
    if (-not $SkipDeviceResources) {
        try {
            $resources = @(Get-CimAssociatedInstance -InputObject $device -Association Win32_PNPAllocatedResource | ForEach-Object {
                [ordered]@{ type = $_.CimClass.CimClassName; name = $_.Name
                    start = $_.StartingAddress; end = $_.EndingAddress; irq = $_.IRQNumber; dmaChannel = $_.DMAChannel }
            })
        } catch { Record-Issue ('resources ' + $device.PNPDeviceID) $_.Exception.Message }
    }
    $deviceResults.Add([ordered]@{ name = $device.Name; instanceId = $device.PNPDeviceID
        hardwareIds = $device.HardwareID; compatibleIds = $device.CompatibleID
        service = $device.Service; status = $device.Status; pnpClass = $device.PNPClass; resources = $resources })
}
Save-Json 'device-resources.json' @($deviceResults.ToArray())

# Use documented read-only firmware-table APIs. Whitelist excludes licensing
# tables (MSDM/SLIC), raw SMBIOS, credentials and arbitrary physical memory.
if (-not ('SurfaceVideoReference.FirmwareTables' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace SurfaceVideoReference {
 public static class FirmwareTables {
  const uint ACPI = 0x41435049;
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern uint EnumSystemFirmwareTables(uint provider, IntPtr buffer, uint size);
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern uint GetSystemFirmwareTable(uint provider, uint id, IntPtr buffer, uint size);
  static byte[] Read(uint id, bool list) {
   uint size = list ? EnumSystemFirmwareTables(ACPI, IntPtr.Zero, 0) : GetSystemFirmwareTable(ACPI, id, IntPtr.Zero, 0);
   if (size == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
   if (size > 16*1024*1024) throw new InvalidOperationException("Firmware table exceeds 16 MiB cap");
   IntPtr buffer = Marshal.AllocHGlobal((int)size);
   try {
    uint actual = list ? EnumSystemFirmwareTables(ACPI, buffer, size) : GetSystemFirmwareTable(ACPI, id, buffer, size);
    if (actual == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
    if (actual > size) throw new InvalidOperationException("Firmware table size changed; rerun collection");
    byte[] result = new byte[actual]; Marshal.Copy(buffer, result, 0, (int)actual); return result;
   } finally { Marshal.FreeHGlobal(buffer); }
  }
  public static byte[] EnumerateAcpi() { return Read(0, true); }
  public static byte[] GetAcpi(uint id) { return Read(id, false); }
 }
}
'@
}
$acpiOutput = Join-Path $output 'acpi'
$null = New-Item -ItemType Directory -Path $acpiOutput
$allowedTables = @('DSDT','SSDT','IORT','APIC','GTDT','FACP','MCFG','CSRT','DBG2','SPCR')
$acpiResults = New-Object 'System.Collections.Generic.List[object]'
$seenAcpi = New-Object 'System.Collections.Generic.HashSet[string]'
function Save-AcpiTable([byte[]]$Data, [string]$Method) {
    if ($Data.Length -lt 36) { throw 'ACPI header is truncated.' }
    $signature = [Text.Encoding]::ASCII.GetString($Data, 0, 4)
    if ($allowedTables -notcontains $signature) { return }
    $declared = [BitConverter]::ToUInt32($Data, 4)
    if ($declared -lt 36 -or $declared -gt $Data.Length) { throw 'Invalid ACPI declared length.' }
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $hash = ([BitConverter]::ToString($hasher.ComputeHash($Data))).Replace('-','').ToLowerInvariant() } finally { $hasher.Dispose() }
    if (-not $seenAcpi.Add($hash)) { return }
    [long]$checksum = 0
    for ($index = 0; $index -lt $declared; $index++) { $checksum += $Data[$index] }
    $name = $signature + '-' + $hash.Substring(0,16) + '.dat'
    [IO.File]::WriteAllBytes((Join-Path $acpiOutput $name), $Data)
    $acpiResults.Add([ordered]@{ signature = $signature; method = $Method; file = 'acpi/' + $name
        bytes = $Data.Length; declaredBytes = $declared; checksumValid = ($checksum % 256 -eq 0); sha256 = $hash })
}
try {
    $ids = [SurfaceVideoReference.FirmwareTables]::EnumerateAcpi()
    if ($ids.Length % 4) { throw 'ACPI enumeration size is not a multiple of four.' }
    $enumeration = @()
    $seenIds = New-Object 'System.Collections.Generic.HashSet[uint32]'
    for ($index = 0; $index -lt $ids.Length; $index += 4) {
        $id = [BitConverter]::ToUInt32($ids, $index)
        $signature = [Text.Encoding]::ASCII.GetString($ids, $index, 4)
        $enumeration += $signature
        if ($allowedTables -contains $signature -and $seenIds.Add($id)) {
            try { Save-AcpiTable ([SurfaceVideoReference.FirmwareTables]::GetAcpi($id)) 'GetSystemFirmwareTable' }
            catch { Record-Issue ('ACPI ' + $signature) $_.Exception.Message }
        }
    }
    Save-Json 'acpi-enumeration.json' ([ordered]@{ signatures = $enumeration
        limit = 'GetSystemFirmwareTable returns only the first table for duplicate signatures. Registry copies are attempted separately; completeness is not guaranteed.' })
} catch { Record-Issue 'ACPI API' $_.Exception.Message }
# Some Windows builds also expose additional SSDTs as registry binary values.
foreach ($signature in @('DSDT','SSDT','IORT')) {
    $acpiRegistry = 'Registry::HKEY_LOCAL_MACHINE\HARDWARE\ACPI\' + $signature
    if (-not (Test-Path -LiteralPath $acpiRegistry)) { continue }
    try {
        foreach ($key in @(Get-Item -LiteralPath $acpiRegistry) + @(Get-ChildItem -LiteralPath $acpiRegistry -Recurse)) {
            foreach ($valueName in $key.GetValueNames()) {
                $data = $key.GetValue($valueName)
                if ($data -is [byte[]]) {
                    try { Save-AcpiTable $data 'ACPI registry binary' }
                    catch { Record-Issue ('ACPI registry ' + $signature) $_.Exception.Message }
                }
            }
        }
    } catch { Record-Issue ('ACPI registry ' + $signature) $_.Exception.Message }
}
Save-Json 'acpi-tables.json' @($acpiResults.ToArray())
$firmwareFiles = @(Get-ChildItem -LiteralPath $output -Recurse -File -Filter '*.mbn')
Save-Json 'summary.json' ([ordered]@{
    status = 'Collected; inspect issues and missing data before claiming completeness'
    expectedProcessor = $ExpectedProcessor; driverPackageCount = $packageResults.Count
    selectedDeviceCount = $devices.Count; acpiTableCount = $acpiResults.Count
    videoFirmwareFiles = @($firmwareFiles | Where-Object { $_.Name -match 'venus|vss|vpu' } | ForEach-Object { $_.FullName.Substring($output.Length + 1).Replace('\','/') })
    resourcesSkipped = [bool]$SkipDeviceResources; allDriverExportRequested = [bool]$AllDrivers
    issues = @($issues.ToArray())
})
@'
Private Surface video reference. Keep original binaries and raw hardware data out of Git.
No drivers were installed/removed; no boot, security, network or account settings were changed.
Start analysis with identity.json, versions.json, summary.json and manifest.json.
ACPI duplicate-signature capture may be incomplete. No MSDM/SLIC/license table or raw SMBIOS was requested.
SQ1/SQ2 firmware equivalence must be established by identity/version/hash; this is not a deployment package.
'@ | Set-Content -LiteralPath (Join-Path $output 'README.txt') -Encoding UTF8
$files = @(Get-ChildItem -LiteralPath $output -Recurse -File | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($output.Length + 1).Replace('\','/'); bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
Save-Json 'manifest.json' ([ordered]@{ files = $files; excludesManifestItself = $true })
Write-Output ('Collection saved to: ' + $output)
if (-not $NoArchive) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($output, $archive, [IO.Compression.CompressionLevel]::Optimal, $true)
    $archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    ($archiveHash + '  ' + [IO.Path]::GetFileName($archive)) | Set-Content -LiteralPath ($archive + '.sha256.txt') -Encoding ASCII
    Write-Output ('Archive saved to: ' + $archive)
    Write-Output ('SHA256: ' + $archiveHash)
}
