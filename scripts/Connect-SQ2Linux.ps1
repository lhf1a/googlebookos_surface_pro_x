param(
    [Parameter(Mandatory=$true)][string]$IPAddress,
    [string]$Command,
    [string]$IdentityFile = $(if ($env:SQ2_SSH_KEY) { $env:SQ2_SSH_KEY } else { Join-Path $env:USERPROFILE '.ssh\id_ed25519' })
)
$ErrorActionPreference='Stop'
$parsed=$null
if(-not [System.Net.IPAddress]::TryParse($IPAddress,[ref]$parsed)){throw 'Expected an IP address from the Surface SSH_READY screen'}
$root=Split-Path -Parent $PSScriptRoot
$identity=$IdentityFile
$known=Join-Path $root 'private\sq2-ssh\known_hosts'
$sshArgs=@('-i',$identity,'-o','IdentitiesOnly=yes','-o','StrictHostKeyChecking=yes','-o','HostKeyAlias=sq2-probe','-o',"UserKnownHostsFile=$known",'-o','ConnectTimeout=8','-o','ServerAliveInterval=15','-o','ServerAliveCountMax=2',"root@$IPAddress")
if($Command){$sshArgs+=$Command}
& ssh.exe @sshArgs
exit $LASTEXITCODE
