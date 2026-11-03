[CmdletBinding()]
param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
if ($root -ne 'C:\Users\Administrator\Documents\ChatGPT\chromebookOS') { throw 'Unexpected workspace; refusing cleanup.' }
$source = '\\wsl.localhost\Ubuntu-24.04\home\kanoyami\.local\share\spx-shared-lte-20261103-v1\surface-pro-x-sq1-sq2.img.gz'
$expected = '7020340a7cba4b4b69f33a800cc35256e707340b893c8f20d9b25d8e698cefc2'
$inputs = Join-Path $root 'private\build-inputs'
$release = Join-Path $root 'out\release'
$ledger = $release
$destination = Join-Path $release 'surface-pro-x-sq1-sq2.img.gz'

function Assert-LocalPath([string]$Path) {
    $p = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (!$p.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Outside workspace: $p" }
    return $p
}
function Remove-OwnedTree([string]$Path) {
    $p = Assert-LocalPath $Path
    if ($p -eq $inputs -or $p.StartsWith($inputs + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Protected private input.' }
    if (!(Test-Path -LiteralPath $p)) { return }
    $item = Get-Item -LiteralPath $p -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        Remove-Item -LiteralPath $p -Force
    } else {
        # PowerShell 7 does not follow directory links unless explicitly requested.
        foreach ($link in @(Get-ChildItem -LiteralPath $p -Recurse -Force -Attributes ReparsePoint)) {
            $lp = Assert-LocalPath $link.FullName
            Remove-Item -LiteralPath $lp -Force
        }
        Remove-Item -LiteralPath $p -Recurse -Force
    }
    if (Test-Path -LiteralPath $p) { throw "Removal incomplete: $p" }
    $script:removed.Add($p)
    Write-Host "Removed: $p"
}

if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use PowerShell 7 (pwsh), not Windows PowerShell 5.' }
if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw 'Latest verified image unavailable. Nothing deleted.' }
foreach ($n in @('template.img','runtime','ksunext','touch','lte')) {
    if (!(Test-Path -LiteralPath (Join-Path $inputs $n))) { throw "Missing retained input: $n" }
}
$targets = @('out','downloads','upstream','work','artifacts') | ForEach-Object { Assert-LocalPath (Join-Path $root $_) }
Write-Host "Workspace: $root"
Write-Host "Keep: .git, source/configuration/docs/tests/reference, $inputs"
Write-Host "Latest image will be copied to: $destination"
Write-Host 'Remove old contents of:'
$targets | ForEach-Object { Write-Host "  $_" }
Write-Host 'Also remove private/* except build-inputs, after preserving access credentials; remove Python test caches.'
Write-Host 'No external disks, other workspaces or WSL caches will be deleted.'
if (!$Apply) { Write-Host 'Preview only. Re-run with -Apply to execute.'; exit 0 }

if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Latest image hash mismatch. Nothing deleted.'
}
$access = Join-Path $inputs 'access'
New-Item -ItemType Directory -Path $access -Force | Out-Null
$credentials = @()
$ssh = Join-Path $root 'private\sq2-ssh'
if (Test-Path -LiteralPath $ssh) { $credentials += @(Get-ChildItem -LiteralPath $ssh -Recurse -File) }
foreach ($n in @('surface-known-hosts','sq2-android-wifi.conf','test2-wpa_supplicant.conf')) {
    $p = Join-Path $root ('private\' + $n)
    if (Test-Path -LiteralPath $p) { $credentials += Get-Item -LiteralPath $p }
}
foreach ($f in $credentials) {
    $relative = [IO.Path]::GetRelativePath((Join-Path $root 'private'), $f.FullName)
    $to = Join-Path $access $relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($to)) -Force | Out-Null
    if ((Test-Path -LiteralPath $to) -and ((Get-FileHash -LiteralPath $to).Hash -ne (Get-FileHash -LiteralPath $f.FullName).Hash)) {
        throw "Conflicting preserved credential: $relative"
    }
    Copy-Item -LiteralPath $f.FullName -Destination $to -Force
    if ((Get-FileHash -LiteralPath $to).Hash -ne (Get-FileHash -LiteralPath $f.FullName).Hash) { throw 'Credential copy mismatch.' }
}
$beforeFree = (Get-PSDrive C).Free
$removed = [Collections.Generic.List[string]]::new()
# The verified WSL image remains untouched throughout; freeing old output space precedes copying it.
foreach ($p in $targets) { Remove-OwnedTree $p }
New-Item -ItemType Directory -Path $release -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $destination
if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Copied image mismatch; original WSL image is intact.' }
foreach ($p in @(Get-ChildItem -LiteralPath (Join-Path $root 'private') -Force)) {
    if ($p.FullName -ne $inputs) { Remove-OwnedTree $p.FullName }
}
foreach ($parent in @('scripts','tests','probe','reference','configs','docs','patches')) {
    $p = Join-Path $root $parent
    if (Test-Path -LiteralPath $p) {
        $caches = @(Get-ChildItem -LiteralPath $p -Directory -Recurse -Force | Where-Object Name -In @('__pycache__','.pytest_cache','.mypy_cache'))
        foreach ($cache in $caches) { Remove-OwnedTree $cache.FullName }
    }
}
$receipt = [ordered]@{
    workspace=$root; completed=(Get-Date).ToString('o'); image=$destination; image_bytes=(Get-Item -LiteralPath $destination).Length
    image_sha256=$expected; retained_private=$inputs; removed=$removed.ToArray()
    free_before=$beforeFree; free_after=(Get-PSDrive C).Free
    source_and_git_preserved=$true; original_wsl_image_preserved=$true
}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ledger 'workspace-cleanup-completed.json') -Encoding utf8
Write-Host 'Cleanup complete. Latest image verified; necessary private inputs and source retained.'
Write-Host $destination
