#requires -Version 5.1
#requires -RunAsAdministrator
# Install OpenSSH Server and authorize a public key supplied by the developer.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PublicKeyPath,
    [switch]$Offline,
    [string]$MsiPath = (Join-Path $PSScriptRoot 'OpenSSH-ARM64-v10.4.0.0.msi')
)
$ErrorActionPreference = 'Stop'
$taskPublicKey = ([IO.File]::ReadAllText((Resolve-Path -LiteralPath $PublicKeyPath).ProviderPath)).Trim()
if ($taskPublicKey -notmatch '^(ssh-ed25519|ssh-rsa|ecdsa-sha2-nistp\d+) [A-Za-z0-9+/=]+(?: [^\r\n]*)?$') {
    throw 'Expected one OpenSSH public key line.'
}
$computer = Get-CimInstance Win32_ComputerSystem
$processor = @(Get-CimInstance Win32_Processor)
if ($computer.Model -ne 'Surface Pro X' -or $processor[0].Name -notmatch '\bSQ2\b') {
    throw 'Run this setup on the Surface Pro X SQ2, not the build workstation.'
}
$existingService = Get-Service sshd -ErrorAction SilentlyContinue
if (-not $existingService) {
    if ($Offline) {
        # Pinned Microsoft ARM64 Preview release; no Windows Update dependency.
        $package = (Resolve-Path -LiteralPath $MsiPath).ProviderPath
        $expectedHash = '75fa139d8975eedb706d9180f454e2148af09d417692825f383fe00610b00e21'
        if ((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'OpenSSH MSI hash mismatch. Copy the original ARM64 package again.'
        }
        $signature = Get-AuthenticodeSignature -LiteralPath $package
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') {
            throw 'The MSI must have a valid Microsoft signature. Signature verification failed.'
        }
        $installLog = Join-Path $env:TEMP ('sq2-openssh-install-' + (Get-Date -Format yyyyMMdd-HHmmss) + '.log')
        $arguments = '/i "{0}" ADDLOCAL=Server /qn /norestart /L*v "{1}"' -f $package, $installLog
        Write-Output "Installing Microsoft OpenSSH ARM64 Preview. Log: $installLog"
        $installer = Start-Process -FilePath (Join-Path $env:windir 'System32\msiexec.exe') -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
        if ($installer.ExitCode -eq 3010) {
            throw 'OpenSSH installed; restart Windows and run this script with -Offline again.'
        }
        if ($installer.ExitCode -ne 0) {
            throw "OpenSSH MSI failed (exit $($installer.ExitCode)). Return this log: $installLog"
        }
    } else {
        try {
            $capability = Get-WindowsCapability -Online -Name 'OpenSSH.Server~~~~0.0.1.0'
            if ($capability.State -ne 'Installed') {
                $result = Add-WindowsCapability -Online -Name 'OpenSSH.Server~~~~0.0.1.0'
            }
        } catch {
            throw "Windows optional-feature installation failed: $($_.Exception.Message) Use the offline bundle and run this script with -Offline."
        }
        if ($result -and $result.RestartNeeded) { throw 'OpenSSH installed; restart Windows and run this script again.' }
    }
}
# Locate the actual service executable: MSI and Windows capability use different folders.
$serviceInfo = Get-CimInstance Win32_Service -Filter "Name='sshd'"
if (-not $serviceInfo) { throw 'The installer did not register sshd.' }
$serviceCommand = [Environment]::ExpandEnvironmentVariables($serviceInfo.PathName)
if ($serviceCommand -match '^\s*"([^"]+\.exe)"\s*$' -or $serviceCommand -match '^\s*(.+?\.exe)\s*$') {
    $sshd = $Matches[1]
} else {
    throw 'Existing sshd has custom service arguments. Return its PathName for review.'
}
if (-not (Test-Path -LiteralPath $sshd -PathType Leaf)) { throw "sshd executable missing: $sshd" }
$sshDirectory = Split-Path -Parent $sshd
Set-Service sshd -StartupType Automatic
Start-Service sshd
$keyFile = Join-Path $env:ProgramData 'ssh\administrators_authorized_keys'
if (-not (Test-Path -LiteralPath $keyFile)) {
    [IO.File]::WriteAllText($keyFile, '', [Text.Encoding]::ASCII)
}
$lines = @([IO.File]::ReadAllLines($keyFile))
if (-not ($lines | Where-Object { $_ -match [regex]::Escape(($taskPublicKey -split ' ')[1]) })) {
    # Preserve existing authorizations, including a file without a trailing newline.
    [IO.File]::AppendAllText($keyFile, "`r`n$taskPublicKey`r`n", [Text.Encoding]::ASCII)
}
# Locale-independent SIDs: Builtin Administrators and LocalSystem only.
$acl = New-Object Security.AccessControl.FileSecurity
$acl.SetAccessRuleProtection($true, $false)
foreach ($sidText in @('S-1-5-32-544', 'S-1-5-18')) {
    $sid = New-Object Security.Principal.SecurityIdentifier($sidText)
    $rule = New-Object Security.AccessControl.FileSystemAccessRule($sid, 'FullControl', 'Allow')
    $acl.AddAccessRule($rule)
}
Set-Acl -LiteralPath $keyFile -AclObject $acl
$ruleName = 'OpenSSH-Server-In-TCP'
if (Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue) {
    Set-NetFirewallRule -Name $ruleName -Enabled True -Direction Inbound -Action Allow -Profile Any -RemoteAddress LocalSubnet
} else {
    New-NetFirewallRule -Name $ruleName -DisplayName 'OpenSSH Server (local subnet)' -Enabled True -Direction Inbound -Action Allow -Protocol TCP -LocalPort 22 -Profile Any -RemoteAddress LocalSubnet | Out-Null
}
# The Server-only MSI may not add its directory to PATH. Make the default
# SFTP command explicit so scp/SFTP works without installing the client feature.
$configPath = Join-Path $env:ProgramData 'ssh\sshd_config'
$originalConfig = [IO.File]::ReadAllText($configPath)
$sftpPattern = '(?m)^Subsystem[ \t]+sftp[ \t]+sftp-server\.exe[ \t]*\r?$'
$sftpBackup = $null
if ($originalConfig -match $sftpPattern) {
    $sftpServer = Join-Path $sshDirectory 'sftp-server.exe'
    if (-not (Test-Path -LiteralPath $sftpServer -PathType Leaf)) { throw 'SFTP server executable missing.' }
    $sftpBackup = $configPath + '.before-sq2-sftp-' + [Guid]::NewGuid().ToString('N')
    Copy-Item -LiteralPath $configPath -Destination $sftpBackup
    $updatedConfig = [regex]::Replace($originalConfig, $sftpPattern, ('Subsystem sftp "' + $sftpServer.Replace('\', '/') + '"'))
    [IO.File]::WriteAllText($configPath, $updatedConfig, (New-Object Text.UTF8Encoding($false)))
}
& $sshd -t
if ($LASTEXITCODE -ne 0) {
    if ($sftpBackup) { Copy-Item -LiteralPath $sftpBackup -Destination $configPath -Force }
    throw 'sshd configuration validation failed; any SFTP edit was restored.'
}
Restart-Service sshd
Write-Output '--- Return the following connection information ---'
whoami
Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway } | Select-Object InterfaceAlias,@{Name='IPv4';Expression={$_.IPv4Address.IPAddress}}
& (Join-Path $sshDirectory 'ssh-keygen.exe') -lf (Join-Path $env:ProgramData 'ssh\ssh_host_ed25519_key.pub')
if ($LASTEXITCODE -ne 0) { throw 'Unable to read the SSH host fingerprint.' }
Get-Service sshd | Select-Object Name,Status,StartType
Write-Output 'Setup complete. Keep the Surface awake and on the same LAN as the workstation.'
