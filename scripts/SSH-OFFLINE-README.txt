Surface Pro X SQ2 - offline SSH setup

1. Extract this ZIP on the Surface, not the build workstation.
2. Open Windows PowerShell as Administrator in the extracted folder.
3. Run:
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Enable-SQ2-SSH.ps1 -Offline -PublicKeyPath C:\path\to\your-key.pub
4. Return the printed account, IPv4 address, SHA256 host fingerprint and sshd status.
   Keep both computers on the same LAN and keep the Surface awake.

The script installs the Server feature of Microsoft's ARM64 OpenSSH Preview,
sets sshd automatic startup, authorizes the dedicated build-workstation public
key supplied with -PublicKeyPath for administrator accounts and restricts the
standard SSH firewall rule to LocalSubnet. No key is included in this repository.
Existing sshd installations are reused. Download the MSI separately.
Windows Update / Add-WindowsCapability is not used in offline mode.

Version: 10.4.0.0p1-Preview (manually maintained, for this LAN debugging task)
Source:
https://github.com/PowerShell/Win32-OpenSSH/releases/tag/10.4.0.0p1-Preview
MSI SHA256:
75fa139d8975eedb706d9180f454e2148af09d417692825f383fe00610b00e21
The script checks this hash and a valid Microsoft Authenticode signature.

If installation reports a restart is needed, restart and run the command again.
If MSI fails, return the error and the named sq2-openssh-install log in TEMP.
