#requires -Version 5.1
<# Read-only cooperative snapshots of the two explicitly identified SQ1 caches.
No service stop, handle duplication, ACL change, lock bypass, or modem command.
The complete snapshots are private analysis inputs, NOT a deployment package.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$CheckOnly,
    [switch]$ExistingShadow
)
$ErrorActionPreference='Stop'
function Read-SharedBytes([string]$Path) {
    $share=[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
    $stream=New-Object IO.FileStream($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,$share)
    try {
        if($stream.Length -ne 2097152){throw 'Expected exactly 2 MiB reference cache'}
        $buffer=New-Object byte[] 2097152
        $offset=0
        while($offset -lt $buffer.Length){
            $read=$stream.Read($buffer,$offset,$buffer.Length-$offset)
            if($read -le 0){throw 'Short read'}
            $offset+=$read
        }
        if($stream.Length -ne $buffer.Length){throw 'Source length changed'}
        return ,$buffer
    } finally {$stream.Dispose()}
}
function Bytes-Sha256([byte[]]$Bytes) {
    $hash=[Security.Cryptography.SHA256]::Create()
    try {return ([BitConverter]::ToString($hash.ComputeHash($Bytes))).Replace('-','').ToLower()}
    finally {$hash.Dispose()}
}
$computer=Get-CimInstance Win32_ComputerSystem
$cpu=Get-CimInstance Win32_Processor
if($computer.Model -ne 'Surface Pro X' -or $cpu.Name -notmatch '\bSQ1\b' -or $cpu.Architecture -ne 12){throw 'SQ1 ARM64 identity mismatch'}
if($CheckOnly){[ordered]@{model=$computer.Model;cpu=$cpu.Name;architecture=$cpu.Architecture;readOnly=$true}|ConvertTo-Json;return}
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output){throw 'Use a fresh private output directory'}
$windows=[IO.Path]::GetFullPath($env:windir).TrimEnd('\')
if($output.StartsWith($windows+'\',[StringComparison]::OrdinalIgnoreCase) -or $output.TrimEnd('\') -eq [IO.Path]::GetPathRoot($output).TrimEnd('\')){throw 'Output must be outside Windows and the drive root'}
$source=Join-Path $env:windir 'System32\DriverStore\FileRepository\qcremotefs8180.inf_arm64_8a629fd5db8bab22'
$snapshot=$null
if($ExistingShadow){
    # Existing snapshots only. Never invoke Create(), VSS writers or a service.
    $drive=[IO.Path]::GetPathRoot($env:windir).TrimEnd('\')
    $volume=Get-CimInstance Win32_Volume -Filter ("DriveLetter='"+$drive+"'")
    if(-not $volume.DeviceID){throw 'System volume identity unavailable'}
    $suffix=$source.Substring($drive.Length)
    $shadows=@(Get-CimInstance Win32_ShadowCopy | Where-Object {$_.VolumeName -eq $volume.DeviceID} | Sort-Object InstallDate -Descending | Select-Object -First 3)
    foreach($candidate in $shadows){
        $path=$candidate.DeviceObject+$suffix
        if([IO.File]::Exists($path+'\bootmodem_fs1') -and [IO.File]::Exists($path+'\bootmodem_fs2')){
            $source=$path
            $snapshot=[ordered]@{id=$candidate.ID;created=$candidate.InstallDate;method='existing read-only volume snapshot; not created by collector'}
            break
        }
    }
    if(-not $snapshot){throw 'No existing system-volume snapshot contains both whitelisted files'}
}
New-Item -ItemType Directory -Path $output | Out-Null
$results=@()
foreach($name in @('bootmodem_fs1','bootmodem_fs2')) {
    try {
        $path=Join-Path $source $name
        if(([IO.File]::GetAttributes($path)) -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse source refused'}
        $first=Read-SharedBytes $path
        $second=Read-SharedBytes $path
        $a=Bytes-Sha256 $first;$b=Bytes-Sha256 $second
        if($a -ne $b){throw 'Two cooperative reads differed; not accepted as stable input'}
        [IO.File]::WriteAllBytes((Join-Path $output ($name+'.bin')),$first)
        $results+=[ordered]@{name=$name;bytes=$first.Length;sha256=$a;twoReadsEqual=$true;status='copied';sourceAccess='FileAccess.Read; FileShare.ReadWrite|Delete'}
    } catch {
        $results+=[ordered]@{name=$name;status='unavailable';error=$_.Exception.Message;hresult=$_.Exception.HResult;lockBypass=$false}
    }
}
[ordered]@{model=$computer.Model;cpu=$cpu.Name;snapshot=$snapshot;results=$results;originalWrites=$false;driverChanges=$false;limit='Two equal reads are not an application-consistent FS1/FS2 pair snapshot. Raw images may include identities/keys; private analysis only, no deployment or RF enable.'} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'manifest.json') -Encoding UTF8
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($output,$output+'.zip',[IO.Compression.CompressionLevel]::Optimal,$true)
((Get-FileHash -LiteralPath ($output+'.zip') -Algorithm SHA256).Hash.ToLower()+'  '+[IO.Path]::GetFileName($output)+'.zip') | Set-Content -LiteralPath ($output+'.zip.sha256.txt') -Encoding ASCII
$results | ForEach-Object {[pscustomobject]$_} | Select-Object name,status,bytes,twoReadsEqual | ConvertTo-Json
