#requires -Version 5.1
<#+ Windows collection only. Build on Linux x86_64; no WSL required to collect. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$WindowsRoot = $env:windir,
    [string]$SurfaceExport,
    [string]$CacheDirectory,
    [string]$LocalInputs,
    [switch]$IncludeThirdParty,
    [switch]$FullDonorArchive,
    [string[]]$DownloadIds,
    [string[]]$RepositoryIds,
    [switch]$Plan,
    [switch]$SkipDownloads,
    [switch]$SkipRepositories,
    [switch]$SkipSurface,
    [switch]$AllowPartialSurface
)
. "$PSScriptRoot/HybridDependencies.Common.ps1"
$repo = Split-Path $PSScriptRoot -Parent
$spec = Get-Content -LiteralPath (Join-Path $repo 'configs/hybrid-dependencies.json') -Raw | ConvertFrom-Json
if ($Plan) { $spec | ConvertTo-Json -Depth 30; return }
foreach ($id in $DownloadIds) { if ($id -notin $spec.downloads.id) { throw "Unknown download id: $id" } }
foreach ($id in $RepositoryIds) { if ($id -notin $spec.repositories.id) { throw "Unknown repository id: $id" } }
$out = New-DependencyDirectory $Output
$archive = $out + '.tar'
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }
$null = Get-Command tar.exe -ErrorAction Stop
$null = New-Item -ItemType Directory -Path (Join-Path $out 'downloads')
$acquired = @(); $skipped = @()
foreach ($item in $spec.downloads) {
    if ($SkipDownloads -or ($DownloadIds -and $item.id -notin $DownloadIds)) { $skipped += $item.id; continue }
    if ($item.PSObject.Properties.Name -contains 'full_archive_only' -and $item.full_archive_only -and -not $FullDonorArchive) {
        $skipped += $item.id
        continue
    }
    if ($item.PSObject.Properties.Name -contains 'third_party' -and $item.third_party -and -not $IncludeThirdParty) {
        $skipped += $item.id
        Write-Warning "Skipped third-party archive $($item.id); use -IncludeThirdParty to acquire the pinned donor."
        continue
    }
    Write-Host "Downloading/verifying $($item.id)"
    Get-PinnedDependency $item (Join-Path $out ('downloads/'+$item.file)) $CacheDirectory
    $acquired += $item.id
}
$null = New-Item -ItemType Directory -Path (Join-Path $out 'repositories')
foreach ($item in $spec.repositories) {
    if ($SkipRepositories -or ($RepositoryIds -and $item.id -notin $RepositoryIds)) { $skipped += $item.id; continue }
    # No Windows checkout: Linux filenames/symlinks retain their Git semantics.
    $temp = Join-Path $out ('git-'+$item.id)
    & git init --bare $temp
    if ($LASTEXITCODE) { throw 'git init failed' }
    & git -C $temp -c 'url.https://github.com/.insteadOf=git@github.com:' fetch $item.url $item.commit
    if ($LASTEXITCODE) { throw "git fetch failed: $($item.id)" }
    $actual = & git -C $temp rev-parse FETCH_HEAD
    if ($LASTEXITCODE -or $actual -cne $item.commit) { throw 'Git commit mismatch' }
    & git -C $temp update-ref refs/heads/pinned $item.commit
    if ($LASTEXITCODE) { throw 'Git ref creation failed' }
    & git -C $temp bundle create (Join-Path $out ('repositories/'+$item.id+'.bundle')) refs/heads/pinned
    if ($LASTEXITCODE) { throw 'Git bundle failed' }
    # Keep work outside final archive inventory; remove only this freshly created bare repo.
    $resolved = (Get-Item -LiteralPath $temp).FullName
    if (-not $resolved.StartsWith($out + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Bad scratch path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
    $acquired += $item.id
}
if (-not $SkipSurface) {
    if ($SurfaceExport) {
        $receipt = Get-Content -LiteralPath (Join-Path $SurfaceExport 'SURFACE-EXPORT.json') -Raw | ConvertFrom-Json
        foreach ($f in $receipt.files) {
            if ($f.path -match '(^/|\\|:|(^|/)\.\.(/|$))' -or -not $f.path.StartsWith('windows/')) { throw 'Invalid export path' }
            $src = Join-Path $SurfaceExport $f.path
            if ((Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash -ine $f.sha256) { throw 'Export hash mismatch' }
            Copy-DependencyFile $src (Join-Path $out ('surface/'+$f.path))
        }
        Copy-DependencyFile (Join-Path $SurfaceExport 'SURFACE-EXPORT.json') (Join-Path $out 'surface/SURFACE-EXPORT.json')
        if (-not $AllowPartialSurface -and
            ($receipt.PSObject.Properties.Name -notcontains 'sensors' -or -not $receipt.sensors.ready)) {
            throw 'Surface export lacks complete sensor inputs; rerun Export-SurfaceHybridDependencies.ps1 -RequireSensors on SQ1 or SQ2 Windows.'
        }
    } else {
        & "$PSScriptRoot/Export-SurfaceHybridDependencies.ps1" -Output (Join-Path $out 'surface') -WindowsRoot $WindowsRoot -RequireSensors:(-not $AllowPartialSurface)
    }
} else { $skipped += 'surface-export' }
# Explicit files only, never recursively copy private/build-inputs, runtime, or a home directory.
# Local donors stay opaque and are not executed/extracted by this Windows script.
$local = @()
if ($LocalInputs) {
    $localSpec = Get-Content -LiteralPath $LocalInputs -Raw | ConvertFrom-Json
    foreach ($item in $localSpec.files) {
        if ($item.id -notmatch '^[a-z0-9][a-z0-9-]*$' -or $item.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid local input specification' }
        if ($item.personal_state_absent -ne $true) { throw 'Only reviewed generic donor assets may be packaged' }
        $dest = Join-Path $out ('local/'+$item.id+'/'+[IO.Path]::GetFileName($item.path))
        if (Test-Path -LiteralPath $dest) { throw 'Duplicate local input' }
        if ((Get-FileHash -LiteralPath $item.path -Algorithm SHA256).Hash -ine $item.sha256) { throw 'Local input hash mismatch' }
        Copy-DependencyFile $item.path $dest
        $local += [ordered]@{id=$item.id; file='local/'+$item.id+'/'+[IO.Path]::GetFileName($item.path); sha256=$item.sha256; provenance=$item.provenance; personal_state_absent='user-declared, not an automatic content audit'}
    }
}
Copy-DependencyFile (Join-Path $repo 'configs/hybrid-dependencies.json') (Join-Path $out 'dependency-spec.json')
Copy-DependencyFile (Join-Path $repo 'docs/DEPENDENCIES.md') (Join-Path $out 'README.md')
$commit = & git -C $repo rev-parse HEAD
if ($LASTEXITCODE) { throw 'Cannot identify source commit' }
$report = [ordered]@{schema=1; product=$spec.product; source_commit=$commit; acquired=$acquired; skipped=$skipped;
    local_inputs=$local; unresolved=$spec.manual_required; build_ready=$false;
    status='collected raw inputs; platform preparation still required'; files=@(Get-DependencyInventory $out)}
Write-DependencyJson (Join-Path $out 'MANIFEST.json') $report
# tar.exe supports files larger than 4 GiB. Do not use Compress-Archive here.
& tar.exe -cf $archive -C $out .
if ($LASTEXITCODE) { throw "tar failed: $LASTEXITCODE" }
& tar.exe -tf $archive | Out-Null
if ($LASTEXITCODE) { throw 'Archive reopen failed' }
Write-DependencyJson ($archive+'.sha256.json') ([ordered]@{file=[IO.Path]::GetFileName($archive);sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $archive).Length})
Write-Output "PACKAGE=$archive"
Write-Output 'BUILD_READY=false; see MANIFEST.json unresolved. Import with scripts/import-hybrid-dependencies.py.'
