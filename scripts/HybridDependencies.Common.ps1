# Shared by the Windows preparation and read-only export entry points. PS 5.1.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Write-DependencyJson($Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 30), (New-Object Text.UTF8Encoding($false)))
}
function New-DependencyDirectory([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (Test-Path -LiteralPath $absolute) { throw "Output already exists: $absolute" }
    $null = New-Item -ItemType Directory -Path $absolute
    return $absolute
}
function Get-DependencyFiles([string]$Root) {
    # Enumerate one level at a time: never follow junctions out of DriverStore.
    foreach ($item in Get-ChildItem -LiteralPath $Root -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point: $($item.FullName)" }
        if ($item.PSIsContainer) { Get-DependencyFiles $item.FullName } else { $item }
    }
}
function Get-DependencyInventory([string]$Root) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    @(Get-DependencyFiles $base | Sort-Object FullName | ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($base.Length+1).Replace('\','/'); bytes=$_.Length;
            sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
}
function Copy-DependencyFile([string]$Source, [string]$Destination) {
    $item = Get-Item -LiteralPath $Source
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Not a plain file: $Source" }
    $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Destination)) -Force
    Copy-Item -LiteralPath $Source -Destination $Destination
}
function Get-PinnedDependency($Spec, [string]$Destination, [string]$CacheDirectory) {
    $cache = if ($CacheDirectory) { Join-Path $CacheDirectory $Spec.file } else { $null }
    if ($cache -and (Test-Path -LiteralPath $cache)) {
        Copy-DependencyFile $cache $Destination
    } else {
        if ($Spec.url -notmatch '^https://') { throw 'Downloads require HTTPS' }
        $downloadUrl = $Spec.url
        if ($Spec.PSObject.Properties.Name -contains 'resolver' -and $Spec.resolver -eq 'android-ci-artifact') {
            $page = $Destination + '.artifact-page'
            & curl.exe --fail --location --retry 3 --connect-timeout 30 --output $page $Spec.url
            if ($LASTEXITCODE) { throw 'Android CI artifact page download failed' }
            $downloadUrl = Resolve-AndroidArtifactUrl ([IO.File]::ReadAllText($page))
            Remove-Item -LiteralPath $page
        }
        $partial = $Destination + '.part'
        $rangeArgs = @()
        if ($Spec.PSObject.Properties.Name -contains 'range') { $rangeArgs = @('--range', [string]$Spec.range) }
        & curl.exe --fail --location --retry 3 --connect-timeout 30 @rangeArgs --output $partial $downloadUrl
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($Spec.id), curl=$LASTEXITCODE" }
        if ($Spec.PSObject.Properties.Name -contains 'encoding') {
            if ($Spec.encoding -ne 'base64') { throw "Unsupported encoding: $($Spec.encoding)" }
            $decoded = [Convert]::FromBase64String([IO.File]::ReadAllText($partial))
            [IO.File]::WriteAllBytes($partial, $decoded)
        }
        Move-Item -LiteralPath $partial -Destination $Destination
    }
    $actual = (Get-FileHash -LiteralPath $Destination -Algorithm $Spec.algorithm).Hash
    if ($actual -ine $Spec.hash) { throw "Hash mismatch: $($Spec.id); keep failed directory for inspection" }
}
function Resolve-AndroidArtifactUrl([string]$Page) {
    # ci.android.com serves an artifact HTML page, not the ZIP bytes themselves.
    $match = [regex]::Match($Page, 'var\s+JSVariables\s*=\s*(\{.*?\});', [Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) { throw 'Android CI artifact metadata missing' }
    $metadata = $match.Groups[1].Value | ConvertFrom-Json
    $url = [Uri]$metadata.artifactUrl
    if ($url.Scheme -ne 'https' -or -not ($url.Host -eq 'storage.googleapis.com' -or $url.Host.EndsWith('.googleusercontent.com'))) {
        throw 'Unexpected Android CI artifact host'
    }
    return $url.AbsoluteUri
}
