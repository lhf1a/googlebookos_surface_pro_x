# Execute only the actual read helper in a local synthetic sharing fixture.
param([Parameter(Mandatory=$true)][string]$Collector,[Parameter(Mandatory=$true)][string]$FixtureDirectory)
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path $Collector),[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
$nodes=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Read-SharedBytes'},$true))
if($nodes.Count -ne 1){throw 'Expected one actual Read-SharedBytes definition'}
Invoke-Expression ($nodes[0].Extent.Text)
if(Test-Path -LiteralPath $FixtureDirectory){throw 'Fixture directory already exists'}
New-Item -ItemType Directory -Path $FixtureDirectory | Out-Null
$file=Join-Path $FixtureDirectory 'synthetic.bin'
$bytes=New-Object byte[] 2097152
for($i=0;$i -lt $bytes.Length;$i++){$bytes[$i]=[byte]($i%251)}
[IO.File]::WriteAllBytes($file,$bytes)
$writer=New-Object IO.FileStream($file,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite)
try{
    $failed=$false
    try{Get-FileHash -LiteralPath $file -ErrorAction Stop | Out-Null}catch{$failed=$true}
    if(-not $failed){throw 'Baseline sharing rejection not observed on this runtime'}
    $read=Read-SharedBytes $file
    $hash=[Security.Cryptography.SHA256]::Create()
    try{$a=[BitConverter]::ToString($hash.ComputeHash($bytes));$b=[BitConverter]::ToString($hash.ComputeHash($read))}finally{$hash.Dispose()}
    if($a -ne $b){throw 'Cooperative read did not match exact source bytes'}
    $failed=$false
    try{Get-FileHash -LiteralPath $file -ErrorAction Stop | Out-Null}catch{$failed=$true}
    if(-not $failed){throw 'Baseline sharing behavior changed'}
    Write-Output 'BASELINE: default hash rejected; MODIFIED: cooperative read exact; ROLLBACK observer: default hash rejected again; original file unchanged'
}finally{$writer.Dispose()}
Write-Output 'PASS: actual read helper; 2MiB synthetic source; no CIM, driver, snapshot or modem operation executed'
