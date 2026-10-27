param([Parameter(Mandatory=$true)][string]$IPAddress)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$identity=$(if ($env:SQ2_SSH_KEY) { $env:SQ2_SSH_KEY } else { Join-Path $env:USERPROFILE '.ssh\id_ed25519' })
$known=Join-Path $root 'private\sq2-ssh\known_hosts'
$opts=@('-i',$identity,'-o','IdentitiesOnly=yes','-o','StrictHostKeyChecking=yes','-o','HostKeyAlias=sq2-probe','-o',"UserKnownHostsFile=$known",'-o','ServerAliveInterval=15')
$records=@()
foreach($name in @('system_a','vendor_a','product_a','system_ext_a','system_dlkm_a','vendor_dlkm_a')) {
    $src=Join-Path $root "out\googlebook-stage\$name.img"
    $hash=(Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Output "Uploading $name ($(Get-Date -Format o))"
    & scp.exe -O @opts $src "root@${IPAddress}:/media/sq2data/googlebook-stage/$name.img.partial"
    if($LASTEXITCODE -ne 0){throw "Upload failed: $name"}
    $remote=& ssh.exe @opts "root@$IPAddress" "sha256sum /media/sq2data/googlebook-stage/$name.img.partial"
    if($LASTEXITCODE -ne 0 -or -not $remote.StartsWith($hash)){throw "Remote hash failed: $name"}
    & ssh.exe @opts "root@$IPAddress" "mv /media/sq2data/googlebook-stage/$name.img.partial /media/sq2data/googlebook-stage/$name.img; sync"
    if($LASTEXITCODE -ne 0){throw 'Remote finalize failed'}
    $records+=[ordered]@{file="$name.img";sha256=$hash;bytes=(Get-Item $src).Length}
    Write-Output "Verified $name $hash"
    $records | ConvertTo-Json | Set-Content (Join-Path $root 'out\googlebook-stage\transfer-progress.json') -Encoding UTF8
}
Write-Output 'PASS: all six Googlebook partitions transferred and verified'
