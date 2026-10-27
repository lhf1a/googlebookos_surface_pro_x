#requires -Version 5.1
#requires -RunAsAdministrator
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$result=Join-Path $root 'out\test2-usb\log-read-result.json'
try {
    $disks=@(Get-Disk | Where-Object {$_.BusType -eq 'USB' -and $_.SerialNumber.Trim() -eq 'FC2425D40C3E1' -and $_.Size -eq 31457280000})
    if($disks.Count -ne 1 -or $disks[0].IsBoot -or $disks[0].IsSystem){throw 'USB identity mismatch'}
    $part=Get-Partition -DiskNumber $disks[0].Number | Where-Object {$_.Size -eq 2GB -and $_.DriveLetter}
    if(-not $part){throw 'Expected boot partition not mounted'}
    $drive="$($part.DriveLetter):\"
    if((Get-Volume -DriveLetter $part.DriveLetter).FileSystemLabel -ne 'SQ2PROBE'){throw 'USB label mismatch'}
    $source=Join-Path $drive 'logs'
    $dest=Join-Path $root ('private\surface-test2-logs-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $dest | Out-Null
    $files=@()
    foreach($f in Get-ChildItem -LiteralPath $source -Recurse -File){
        $relative=$f.FullName.Substring($source.Length+1)
        $target=[IO.Path]::GetFullPath((Join-Path $dest $relative))
        if(-not $target.StartsWith($dest+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid log path'}
        $parent=Split-Path -Parent $target
        if(-not(Test-Path -LiteralPath $parent)){New-Item -ItemType Directory -Path $parent | Out-Null}
        $hash=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
        Copy-Item -LiteralPath $f.FullName -Destination $target
        if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $hash){throw 'Log copy hash mismatch'}
        $files+=,[pscustomobject]@{path=$relative;bytes=$f.Length;sha256=$hash.ToLowerInvariant()}
    }
    $files | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $dest 'copy-manifest.json') -Encoding UTF8
    [ordered]@{status='PASS';directory=$dest;files=$files.Count;bytes=($files|Measure-Object bytes -Sum).Sum} | ConvertTo-Json | Set-Content -LiteralPath $result -Encoding UTF8
}catch{
    [ordered]@{status='FAILED';error=($_|Out-String)} | ConvertTo-Json | Set-Content -LiteralPath $result -Encoding UTF8
    exit 1
}
