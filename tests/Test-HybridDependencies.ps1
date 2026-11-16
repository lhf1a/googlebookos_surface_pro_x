#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
. "$repo/scripts/HybridDependencies.Common.ps1"
$artifact = Resolve-AndroidArtifactUrl 'var JSVariables = {"artifactUrl":"https://storage.googleapis.com/fixture/archive.zip"};'
if ($artifact -ne 'https://storage.googleapis.com/fixture/archive.zip') { throw 'CI artifact resolver failed' }
$badHost = $false
try { Resolve-AndroidArtifactUrl 'var JSVariables = {"artifactUrl":"https://example.invalid/archive.zip"};' } catch { $badHost = $true }
if (-not $badHost) { throw 'CI artifact unexpected host accepted' }
$root = New-DependencyDirectory $OutputDirectory
$windows = Join-Path $root 'Windows'
$store = Join-Path $windows 'System32/DriverStore/FileRepository'
$pkg = Join-Path $store 'surfaceprox_subextscss.inf_arm64_fixture'
$null = New-Item -ItemType Directory -Path $pkg -Force
[IO.File]::WriteAllBytes((Join-Path $pkg 'qcslpi8180.mbn'), [byte[]](1,2,3,4))
[IO.File]::WriteAllText((Join-Path $pkg 'not-a-dependency.key'), 'SECRET FIXTURE')
$other = Join-Path $store 'unrelated.inf_arm64_fixture'
$null = New-Item -ItemType Directory -Path $other
[IO.File]::WriteAllText((Join-Path $other 'personal.txt'), 'MUST NOT COPY')
[IO.File]::WriteAllText((Join-Path $windows 'System32/tcblaunch.exe'), 'FIXTURE NOT EXECUTABLE')
$export = Join-Path $root 'oem'
& "$repo/scripts/Export-SurfaceHybridDependencies.ps1" -WindowsRoot $windows -Output $export
$receipt = Get-Content -LiteralPath (Join-Path $export 'SURFACE-EXPORT.json') -Raw | ConvertFrom-Json
if ($receipt.files.Count -ne 2) { throw "Unexpected collected file count: $($receipt.files.Count)" }
if (-not $receipt.required_present.'qcslpi8180.mbn') { throw 'SLPI missing' }
if ($receipt.required_present.'qcadsp8180.mbn') { throw 'False audio readiness' }
if ($receipt.source_mode -ne 'offline-windows-tree') { throw 'Wrong provenance' }
$cache = Join-Path $root 'cache'; $null = New-Item -ItemType Directory -Path $cache
[IO.File]::WriteAllText((Join-Path $cache 'test.bin'), 'download fixture')
$hash = (Get-FileHash -LiteralPath (Join-Path $cache 'test.bin') -Algorithm SHA256).Hash
$spec = [pscustomobject]@{id='fixture';file='test.bin';url='https://example.invalid/test.bin';algorithm='SHA256';hash=$hash}
Get-PinnedDependency $spec (Join-Path $root 'download.bin') $cache
$spec.hash = '0' * 64
$refused = $false
try { Get-PinnedDependency $spec (Join-Path $root 'bad-download.bin') $cache } catch { $refused = $_.Exception.Message -match 'Hash mismatch' }
if (-not $refused) { throw 'Bad download hash was accepted' }
$package = Join-Path $root 'package'
& "$repo/scripts/Prepare-HybridDependencies.ps1" -Output $package -SurfaceExport $export -SkipDownloads -SkipRepositories -AllowPartialSurface
$manifest = Get-Content -LiteralPath (Join-Path $package 'MANIFEST.json') -Raw | ConvertFrom-Json
if ($manifest.build_ready -ne $false -or $manifest.skipped.Count -lt 8) { throw 'False complete package' }
$refused = $false
try { & "$repo/scripts/Prepare-HybridDependencies.ps1" -Output $package -SkipDownloads -SkipRepositories -SkipSurface } catch { $refused = $_.Exception.Message -match 'already exists' }
if (-not $refused) { throw 'Existing package not protected' }
# Exercise the actual opt-in branch without fetching gigabytes: a separate
# source fixture with two content-pinned cached downloads, one third-party.
$mini = Join-Path $root 'source-fixture'
foreach ($dir in @('scripts','configs','docs')) { $null = New-Item -ItemType Directory -Path (Join-Path $mini $dir) -Force }
foreach ($name in @('Prepare-HybridDependencies.ps1','HybridDependencies.Common.ps1')) {
    Copy-Item -LiteralPath (Join-Path $repo ('scripts/'+$name)) -Destination (Join-Path $mini ('scripts/'+$name))
}
[IO.File]::WriteAllText((Join-Path $mini 'docs/DEPENDENCIES.md'), 'Fixture only')
$sourceSpec = [ordered]@{schema=1;product='hybrid-sq1-sq2-6.18';repositories=@();manual_required=@();downloads=@(
    [ordered]@{id='official-fixture';file='test.bin';url='https://example.invalid/test.bin';algorithm='SHA256';hash=$hash},
    [ordered]@{id='donor-fixture';file='donor.bin';url='https://example.invalid/donor.bin';algorithm='SHA256';hash=$hash;third_party=$true}
)}
Copy-Item -LiteralPath (Join-Path $cache 'test.bin') -Destination (Join-Path $cache 'donor.bin')
Write-DependencyJson (Join-Path $mini 'configs/hybrid-dependencies.json') $sourceSpec
& "$mini/scripts/Prepare-HybridDependencies.ps1" -Output (Join-Path $root 'no-third-party') -CacheDirectory $cache -SkipSurface
$without = Get-Content (Join-Path $root 'no-third-party/MANIFEST.json') -Raw | ConvertFrom-Json
if ($without.acquired -contains 'donor-fixture' -or $without.skipped -notcontains 'donor-fixture') { throw 'Third-party default policy failed' }
& "$mini/scripts/Prepare-HybridDependencies.ps1" -Output (Join-Path $root 'with-third-party') -CacheDirectory $cache -SkipSurface -IncludeThirdParty
$with = Get-Content (Join-Path $root 'with-third-party/MANIFEST.json') -Raw | ConvertFrom-Json
if ($with.acquired -notcontains 'donor-fixture' -or $with.acquired.Count -ne 2) { throw 'Third-party opt-in failed' }
Write-DependencyJson (Join-Path $root 'RESULT.json') ([ordered]@{
    fixture=$true; windows_powershell=$PSVersionTable.PSVersion.ToString();
    export_allowlist=$true; absent_audio_reported=$true; cache_hash_checked=$true;
    wrong_hash_rejected=$true; fresh_output_required=$true; archive_reopened=$true;
    third_party_default_skips=$true; third_party_opt_in_acquires=$true;
    package=$package+'.tar'; package_sha256=(Get-FileHash -LiteralPath ($package+'.tar') -Algorithm SHA256).Hash.ToLowerInvariant()
})
Write-Output 'PASS: Windows export/download-cache/package fixtures (not hardware or full build)'
