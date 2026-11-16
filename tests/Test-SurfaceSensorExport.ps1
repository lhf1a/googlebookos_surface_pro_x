#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
. "$repo/scripts/HybridDependencies.Common.ps1"
$root = New-DependencyDirectory $OutputDirectory
$windows = Join-Path $root 'Windows'
$store = Join-Path $windows 'System32/DriverStore/FileRepository'
$baseline = Get-Content "$repo/configs/sensors-baseline.json" -Raw | ConvertFrom-Json
foreach ($pkg in @('surfaceprox_subextscss','surfacelightsensor','surfaceprox_sensorsconfig')) {
    $null = New-Item -ItemType Directory -Path (Join-Path $store ($pkg+'.inf_arm64_fixture')) -Force
}
[IO.File]::WriteAllText((Join-Path $store 'surfaceprox_subextscss.inf_arm64_fixture/qcslpi8180.mbn'),'SLPI fixture')
[IO.File]::WriteAllText((Join-Path $store 'surfacelightsensor.inf_arm64_fixture/SurfaceLightSensor.sys'),'SYS fixture')
foreach ($name in $baseline.files.PSObject.Properties.Name | Where-Object { $_.StartsWith('config/') }) {
    [IO.File]::WriteAllText((Join-Path $store ('surfaceprox_sensorsconfig.inf_arm64_fixture/'+[IO.Path]::GetFileName($name))), '{}')
}
$config = Join-Path $store 'surfaceprox_sensorsconfig.inf_arm64_fixture'
[IO.File]::WriteAllText((Join-Path $config 'personal.key'),'MUST NOT COPY')
$complete = Join-Path $root 'complete'
& "$repo/scripts/Export-SurfaceHybridDependencies.ps1" -WindowsRoot $windows -Output $complete -RequireSensors
$receipt = Get-Content (Join-Path $complete 'SURFACE-EXPORT.json') -Raw | ConvertFrom-Json
if (-not $receipt.sensors.ready -or $receipt.sensors.files.PSObject.Properties.Name.Count -ne 25) { throw 'Incomplete sensor inventory' }
if (@($receipt.files | Where-Object { $_.path -like '*.key' }).Count) { throw 'Personal file collected' }
Remove-Item -LiteralPath (Join-Path $config 'sns_rotv.json')
$rejected = $false
try { & "$repo/scripts/Export-SurfaceHybridDependencies.ps1" -WindowsRoot $windows -Output (Join-Path $root 'missing') -RequireSensors } catch { $rejected = $_.Exception.Message -match 'Incomplete or conflicting sensor' }
if (-not $rejected) { throw 'Missing rotation config accepted' }
[IO.File]::WriteAllText((Join-Path $config 'sns_rotv.json'),'{}')
$duplicate = Join-Path $store 'surfaceprox_subextscss.inf_arm64_old'
$null = New-Item -ItemType Directory -Path $duplicate
[IO.File]::WriteAllText((Join-Path $duplicate 'qcslpi8180.mbn'),'DIFFERENT firmware fixture')
$rejected = $false
try { & "$repo/scripts/Export-SurfaceHybridDependencies.ps1" -WindowsRoot $windows -Output (Join-Path $root 'conflict') -RequireSensors } catch { $rejected = $_.Exception.Message -match 'Incomplete or conflicting sensor' }
if (-not $rejected) { throw 'Conflicting firmware accepted' }
Write-Output 'PASS: 25 sensor OEM inputs; missing rotation config and conflicting SLPI rejected; personal key excluded (fixtures only)'
