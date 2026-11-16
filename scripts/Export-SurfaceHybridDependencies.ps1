#requires -Version 5.1
<#+
Read only OEM DriverStore files, never live registry/NV/user profiles/partitions.
WindowsRoot also accepts an extracted official recovery Windows directory.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$WindowsRoot = $env:windir,
    [switch]$RequireSensors
)
. "$PSScriptRoot/HybridDependencies.Common.ps1"
$windows = (Get-Item -LiteralPath $WindowsRoot).FullName
$store = Join-Path $windows 'System32/DriverStore/FileRepository'
if (-not (Test-Path -LiteralPath $store -PathType Container)) { throw "Missing DriverStore: $store" }
$out = New-DependencyDirectory $Output
$sourceMode = 'offline-windows-tree'
if ($windows.TrimEnd('\') -ieq $env:windir.TrimEnd('\')) {
    $system = Get-CimInstance Win32_ComputerSystem
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    if ($system.Model -notmatch 'Surface Pro X' -or $cpu.Architecture -ne 12) {
        throw 'Run on Surface Pro X ARM64, or pass an extracted Surface recovery WindowsRoot.'
    }
    $sourceMode = 'surface-pro-x-windows'
}
# Original INF names, not unstable oemNN.inf numbers. Export all installed
# versions separately; downstream refuses ambiguous different payloads.
$prefix = '^(qcbtfmuart8180|qcdx8180|qcwlan8180|surfaceprox_(mcfg|subextadsp|subextcdsp|subextmpss|subextscss|sensorsconfig)|surfacelightsensor)\.inf_'
$packages = @()
foreach ($dir in Get-ChildItem -LiteralPath $store -Directory | Sort-Object Name) {
    if ($dir.Name -notmatch $prefix) { continue }
    if ($dir.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Driver package is a reparse point' }
    $packages += $dir.Name
    foreach ($f in Get-DependencyFiles $dir.FullName) {
        # Package assets only. MCFG.N are factory carrier profiles, not modem EFS.
        if ($f.Name -notmatch '(?i)\.(mbn|tlv|bin|fw|elf|json|jsn|txt|inf|cat|sys|dll|qdb|dig|b[0-9a-f]{2})$|^MCFG\.\d+$') { continue }
        $relative = $f.FullName.Substring($store.Length+1)
        Copy-DependencyFile $f.FullName (Join-Path $out ('windows/System32/DriverStore/FileRepository/' + $relative))
    }
}
$tcb = Join-Path $windows 'System32/tcblaunch.exe'
if (Test-Path -LiteralPath $tcb -PathType Leaf) { Copy-DependencyFile $tcb (Join-Path $out 'windows/System32/tcblaunch.exe') }
$inventory = @(Get-DependencyInventory $out)
$checks = [ordered]@{}
foreach ($name in @('qcadsp8180.mbn','qccdsp8180.mbn','qcslpi8180.mbn','qcvss8180.mbn','wlanmdsp.mbn','crbtfw21.tlv','SurfaceLightSensor.sys','config_list.txt','tcblaunch.exe')) {
    $checks[$name] = @($inventory | Where-Object { [IO.Path]::GetFileName($_.path) -ieq $name }).Count -gt 0
}
$baseline = Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'configs/sensors-baseline.json') -Raw | ConvertFrom-Json
$sensorNames = @('qcslpi8180.mbn','SurfaceLightSensor.sys') + @(
    $baseline.files.PSObject.Properties.Name | Where-Object { $_.StartsWith('config/') } | ForEach-Object { [IO.Path]::GetFileName($_) }
)
$sensorFiles = [ordered]@{}; $sensorMissing = @(); $sensorConflicts = @()
foreach ($name in $sensorNames) {
    $matches = @($inventory | Where-Object {
        [IO.Path]::GetFileName($_.path) -ieq $name -and
        ($name -in @('qcslpi8180.mbn','SurfaceLightSensor.sys') -or $_.path -match '/surfaceprox_sensorsconfig\.inf_[^/]+/')
    })
    $hashes = @($matches | ForEach-Object { $_.sha256 } | Select-Object -Unique)
    if (-not $matches.Count) { $sensorMissing += $name }
    elseif ($hashes.Count -ne 1) { $sensorConflicts += $name }
    else { $sensorFiles[$name] = [ordered]@{sha256=$hashes[0]; paths=@($matches | ForEach-Object { $_.path })} }
}
$sensorReady = $sensorMissing.Count -eq 0 -and $sensorConflicts.Count -eq 0
$report = [ordered]@{schema=1; source_mode=$sourceMode; scope='shared OEM package assets, not per-unit state';
    packages=$packages; required_present=$checks; files=$inventory; personal_state_collected=$false;
    sensors=[ordered]@{models=@('SQ1','SQ2'); ready=$sensorReady; files=$sensorFiles;
        missing=$sensorMissing; conflicts=$sensorConflicts; calibration_source='model default embedded in SurfaceLightSensor.sys, extracted on Linux';
        excluded=@('live registry','sensor persist/registry','per-unit calibration','accounts','Wi-Fi profiles','SSH keys')}}
Write-DependencyJson (Join-Path $out 'SURFACE-EXPORT.json') $report
Write-Output $out
if ($RequireSensors -and -not $sensorReady) { throw 'Incomplete or conflicting sensor OEM inputs; see SURFACE-EXPORT.json sensors. Update Surface drivers or select one complete recovery WindowsRoot.' }
# Missing components remain explicit, but the useful partial export is retained.
if (@($checks.Values | Where-Object { -not $_ }).Count) { Write-Warning 'Partial OEM export; see required_present in SURFACE-EXPORT.json.' }
