param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
$m=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'parts.json') -Raw | ConvertFrom-Json
if(Test-Path -LiteralPath $OutputPath){throw 'Output already exists; choose a fresh path'}
foreach($part in $m.parts){
    $p=Join-Path $PSScriptRoot $part.name
    if((Get-Item -LiteralPath $p).Length -ne $part.bytes -or (Get-FileHash -LiteralPath $p).Hash -ne $part.sha256){throw "Part mismatch: $($part.name)"}
}
$output=[IO.File]::Open([IO.Path]::GetFullPath($OutputPath),[IO.FileMode]::CreateNew)
try {
    foreach($part in $m.parts){
        $inputStream=[IO.File]::OpenRead((Join-Path $PSScriptRoot $part.name))
        try{$inputStream.CopyTo($output)}finally{$inputStream.Dispose()}
    }
} finally {$output.Dispose()}
if((Get-Item -LiteralPath $OutputPath).Length -ne $m.bytes -or (Get-FileHash -LiteralPath $OutputPath).Hash -ne $m.sha256){throw 'Restored ZIP mismatch'}
Write-Output ('PASS restored ZIP SHA256: '+$m.sha256)
