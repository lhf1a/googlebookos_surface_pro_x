#!/usr/bin/env python3
import argparse,pathlib,struct,hashlib,json,uuid
parser=argparse.ArgumentParser(description="Verify pinned Windows FSG read ABI offline; no hardware access")
parser.add_argument("--soc-driver",type=pathlib.Path,required=True)
parser.add_argument("--remotefs-driver",type=pathlib.Path,required=True)
args=parser.parse_args()
def pe(path,expected):
 b=path.read_bytes();assert hashlib.sha256(b).hexdigest()==expected
 p=struct.unpack_from('<I',b,60)[0];assert b[p:p+4]==b'PE\0\0'
 machine,n=struct.unpack_from('<HH',b,p+4);assert machine==0xaa64
 opt=struct.unpack_from('<H',b,p+20)[0];s=p+24+opt
 sections=[struct.unpack_from('<IIII',b,s+i*40+8) for i in range(n)]
 def at(rva,size):
  for _,va,length,off in sections:
   if va<=rva and rva+size<=va+length:return b[off+rva-va:off+rva-va+size]
  raise ValueError(hex(rva))
 return at
soc=pe(args.soc_driver,'9c9087f671a8e8d998c65061a3826c69ef518181fde0a35f7361412794a8d77d')
rfs=pe(args.remotefs_driver,'1ad254e98e1e154f8382ab728fad8f23a7188b81c6db18736439e53cab067127')
checks={0x3dfb0:0x52800001,0x3dfb8:0x940012fe,0x42e90:0x97fffc60,0x422bc:0x52800021,0x422c4:0x97ffea91,0x3cf80:0x52804288,0x3d178:0x52800405,0x3d180:0x52800b03,0x3d188:0x52800041,0x3d190:0x97fffdac,0x3cd00:0x00568004,0x3d4f0:0x02001a01,0x22a98:0x52800908,0x22b8c:0xd2800028,0x22bcc:0xf9400fa8,0x22bd4:0xf9000109,0x22c30:0xd2800508,0x22c5c:0xd2800408,0x3f08c:0xecaf2b46}
for a,w in checks.items():assert struct.unpack('<I',soc(a,4))[0]==w,hex(a)
remote={0x13920:0xd2802504,0x1396c:0xb900599b,0x13970:0xb9005d98,0x13a34:0xd2802502,0x13ab4:0xb94012a2,0x13ab8:0x910052a1,0x13b64:0xecaf2b46}
for a,w in remote.items():assert struct.unpack('<I',rfs(a,4))[0]==w,hex(a)
g={hex(a):str(uuid.UUID(bytes_le=soc(a,16))) for a in (0x14760,0x14770)}
assert g['0x14760']=='eb3c7242-1b1a-4d51-ab55-f268f018f860'
assert g['0x14770']=='ae865c08-4a07-404d-be51-d9a0465e23e5'
report={'verified_instruction_or_literal_sites':len(checks)+len(remote),'guid_rvas':g,'read_call_chain_rvas':['qcremotefs:0x13870','QcSOCPartition:0x3dfb8->0x42bb0','0x42e90->0x42010','0x422c4->0x3cd08 (operation1/read)','0x3d190->0x3c840 (service selector2)','0x3cb74->0x59c8 (IOCTL0x568004)'],'scm_command':'0x02001a01','arginfo':'0x214','storage_type':1,'command_bytes':40,'details_bytes':32,'source':'https://lists.openwall.net/linux-kernel/2026/01/26/752','limits':['Static chain, not a runtime trace of branch choice.','Read operation matched by copies and public SCM ABI; no decryption/authentication conclusion.','Windows live READ retrieved fixed FSG; no raw SCM/TrEE IOCTL issued by our tool.']}
print(json.dumps(report,indent=2))
