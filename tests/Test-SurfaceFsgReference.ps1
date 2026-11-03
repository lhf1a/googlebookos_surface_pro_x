$ErrorActionPreference='Stop'
$tool=Join-Path $PSScriptRoot '..\scripts\Read-SurfaceFsgReference.ps1'
& powershell.exe -NoProfile -File $tool -SelfTest
if($LASTEXITCODE -ne 0){throw 'selftest failed'}
# Both failures must occur before driver probing or opening a device.
$ErrorActionPreference="Continue"
$r=& powershell.exe -NoProfile -File $tool 2>&1
$ErrorActionPreference="Stop"
if($LASTEXITCODE -eq 0 -or ($r -join "`n") -notmatch 'Specify a private local OutputPath'){throw 'missing destination not rejected'}
$temp=[IO.Path]::GetTempFileName()
try {
 $hash=(Get-FileHash $temp).Hash
 $ErrorActionPreference="Continue"
$r=& powershell.exe -NoProfile -File $tool -OutputPath $temp 2>&1
 $ErrorActionPreference="Stop"
if($LASTEXITCODE -eq 0 -or ($r -join "`n") -notmatch 'Output already exists'){throw 'existing destination not rejected'}
 if((Get-FileHash $temp).Hash -ne $hash){throw 'existing destination changed'}
}finally{Remove-Item -LiteralPath $temp}
'PASS layout, missing destination, existing-file preservation; no hardware access'
