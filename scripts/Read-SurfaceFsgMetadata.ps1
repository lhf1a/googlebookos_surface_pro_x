# Fixed original-driver partition metadata query. No raw SCM or storage writes.
param([switch]$SelfTest)
$ErrorActionPreference='Stop'
function Decode-Metadata([byte[]]$b,[uint32]$returned) {
 if($returned -lt 56 -or $returned -gt $b.Length -or $b.Length -ne 296){throw 'response bounds'}
 if([BitConverter]::ToUInt32($b,0) -ne [Convert]::ToUInt32('ecaf2b4e',16) -or [BitConverter]::ToUInt32($b,4) -ne 0){throw 'response command/status'}
 if($b[16] -ne 1){throw 'not block-backed partition metadata'}
 $bytes=[BitConverter]::ToUInt64($b,24); $block=[BitConverter]::ToUInt32($b,32)
 $start=[BitConverter]::ToUInt64($b,40); $count=[BitConverter]::ToUInt64($b,48)
 if($block -eq 0 -or $block -gt 4096 -or $count -eq 0 -or $count -gt 16777216){throw 'metadata limits'}
 if([decimal]$count*[decimal]$block -ne [decimal]$bytes){throw 'metadata size mismatch'}
 [ordered]@{ioctl='0xecaf2b4e';returned=$returned;status=0;blockBacked=$true;partitionBytes=$bytes;blockBytes=$block;startLBA=$start;blockCount=$count;scope='original Windows driver cached partition metadata; not a fresh SCM GET_INFO or RF acceptance'}
}
$req=New-Object byte[] 296
([Guid]'638ff8e2-22c9-e33b-8f5d-0e81686a68cb').ToByteArray().CopyTo($req,0)
if($SelfTest){
 $fixture=New-Object byte[] 296
 [BitConverter]::GetBytes([Convert]::ToUInt32('ecaf2b4e',16)).CopyTo($fixture,0)
 $fixture[16]=1
 [BitConverter]::GetBytes([uint64]2097152).CopyTo($fixture,24)
 [BitConverter]::GetBytes([uint32]4096).CopyTo($fixture,32)
 [BitConverter]::GetBytes([uint64]100).CopyTo($fixture,40)
 [BitConverter]::GetBytes([uint64]512).CopyTo($fixture,48)
 $r=Decode-Metadata $fixture 296
 if($r.partitionBytes -ne 2097152 -or $r.blockBytes -ne 4096 -or $r.startLBA -ne 100){throw 'decode fixture'}
 $rejected=0
 foreach($kind in @('short','status','command','not-block','zero-size','large-size','count','product')){
  $b=[byte[]]$fixture.Clone(); [uint32]$n=296
  switch($kind){
   'short' {$n=55}
   'status' {$b[4]=1}
   'command' {$b[0]=0}
   'not-block' {$b[16]=0}
   'zero-size' {[BitConverter]::GetBytes([uint32]0).CopyTo($b,32)}
   'large-size' {[BitConverter]::GetBytes([uint32]8192).CopyTo($b,32)}
   'count' {[BitConverter]::GetBytes([uint64]0).CopyTo($b,48)}
   'product' {$b[24]=1}
  }
  try{$null=Decode-Metadata $b $n}catch{$rejected++}
 }
 if($rejected -ne 8){throw 'failure checks'}
 'PASS fixed FSG metadata IOCTL ecaf2b4e; input/output296; decode4096; rejected8; NO hardware calls'; exit 0
}
$drv='C:\Windows\System32\DriverStore\FileRepository\qcsocpartition.inf_arm64_ab0ef651cc2c31ea\QcSOCPartition.sys'
if((Get-FileHash -LiteralPath $drv).Hash -ne '9c9087f671a8e8d998c65061a3826c69ef518181fde0a35f7361412794a8d77d'){throw 'unrecognized driver'}
Add-Type @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class FsgMetadataOnly {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 public static extern SafeFileHandle CreateFile(string p,uint a,uint s,IntPtr sec,uint d,uint f,IntPtr t);
 [DllImport("kernel32.dll",SetLastError=true)]
 public static extern bool DeviceIoControl(SafeFileHandle h,uint c,byte[] i,uint ni,[Out] byte[] o,uint no,out uint n,IntPtr ov);
}
"@
$h=[FsgMetadataOnly]::CreateFile('\\.\QcSOCPartitionDevice',[uint32]2147483648,3,[IntPtr]::Zero,3,0,[IntPtr]::Zero)
if($h.IsInvalid){$e=[Runtime.InteropServices.Marshal]::GetLastWin32Error();$h.Dispose();throw "OPEN_FAILED win32=$e"}
try {
 $out=New-Object byte[] 296; [uint32]$n=0
 $ok=[FsgMetadataOnly]::DeviceIoControl($h,[Convert]::ToUInt32('ecaf2b4e',16),$req,296,$out,296,[ref]$n,[IntPtr]::Zero)
 $err=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
 if(-not $ok){throw "QUERY_FAILED win32=$err returned=$n; stop, no retry"}
 $r=Decode-Metadata $out $n
 $r | ConvertTo-Json -Compress
} finally {
 $h.Dispose()
 if($out){[Array]::Clear($out,0,$out.Length)}
 [Array]::Clear($req,0,$req.Length)
}
