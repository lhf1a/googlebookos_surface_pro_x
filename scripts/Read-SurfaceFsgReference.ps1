# Read-only lab reference collector, not a universal-image seeder. Requires the pinned original SQ1 Windows driver. No credentials or subscriber IDs printed.
param([switch]$SelfTest, [string]$OutputPath)
$ErrorActionPreference='Stop'
# Fixed FSG GUID and READ IOCTL, reconstructed from pinned original drivers.
# No raw SCM, write, erase, init, deinit, reset, or arbitrary path/command interface.
$req=New-Object byte[] 296
([Guid]'638ff8e2-22c9-e33b-8f5d-0e81686a68cb').ToByteArray().CopyTo($req,0)
[BitConverter]::GetBytes([uint32]2097152).CopyTo($req,92)
if($SelfTest){
 if($req.Length -ne 296 -or [BitConverter]::ToUInt32($req,88) -ne 0 -or [BitConverter]::ToUInt32($req,92) -ne 2097152){throw 'request layout'}
 if(($req[16..87] | Where-Object {$_ -ne 0}).Count -ne 0){throw 'unexpected selector'}
 'PASS fixed FSG GUID, offset0, length2097152, input296; READ IOCTL only'; exit 0
}
if(-not $OutputPath){throw "Specify a private local OutputPath; do not publish the binary"}
$file=[IO.Path]::GetFullPath($OutputPath)
if(Test-Path -LiteralPath $file){throw "Output already exists"}
if(-not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($file)) -PathType Container)){throw "Output parent missing"}
$drv='C:\Windows\System32\DriverStore\FileRepository\qcsocpartition.inf_arm64_ab0ef651cc2c31ea\QcSOCPartition.sys'
if((Get-FileHash $drv).Hash -ne '9c9087f671a8e8d998c65061a3826c69ef518181fde0a35f7361412794a8d77d'){throw 'unrecognized driver'}
Add-Type @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class FsgReadOnly {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 public static extern SafeFileHandle CreateFile(string p,uint a,uint s,IntPtr sec,uint d,uint f,IntPtr t);
 [DllImport("kernel32.dll",SetLastError=true)]
 public static extern bool DeviceIoControl(SafeFileHandle h,uint c,byte[] i,uint ni,[Out] byte[] o,uint no,out uint n,IntPtr ov);
}
"@
$h=[FsgReadOnly]::CreateFile('\\.\QcSOCPartitionDevice',[uint32]2147483648,3,[IntPtr]::Zero,3,0,[IntPtr]::Zero)
if($h.IsInvalid){$e=[Runtime.InteropServices.Marshal]::GetLastWin32Error(); $h.Dispose(); "OPEN_FAILED win32=$e"; exit 3}
try {
 $out=New-Object byte[] (296+2097152); [uint32]$n=0
 $ok=[FsgReadOnly]::DeviceIoControl($h,[Convert]::ToUInt32("ecaf2b46",16),$req,296,$out,$out.Length,[ref]$n,[IntPtr]::Zero)
 $err=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
 "READ ok=$ok lastErrorIgnoredOnSuccess=$err returned=$n status=$([BitConverter]::ToUInt32($out,4)) dataLength=$([BitConverter]::ToUInt32($out,16))"
 if(-not $ok -or [BitConverter]::ToUInt32($out,4) -ne 0 -or [BitConverter]::ToUInt32($out,16) -ne 2097152 -or $n -lt 2097172){exit 4}
 $payload=New-Object byte[] 2097152; [Array]::Copy($out,20,$payload,0,2097152)
 $sha=[Security.Cryptography.SHA256]::Create(); try{$hex=([BitConverter]::ToString($sha.ComputeHash($payload))).Replace('-','').ToLower()}finally{$sha.Dispose()}
 $tag=[Text.Encoding]::ASCII.GetString($payload,40,7)
 "FULL sha256=$hex tag=$tag"
 if($tag -ne 'IMGEFSG'){throw 'unexpected FSG header; stop'}
 $f=[IO.File]::Open($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try{$f.Write($payload,0,$payload.Length)}finally{$f.Dispose()}
 "SAVED $file"
}finally{$h.Dispose()}
