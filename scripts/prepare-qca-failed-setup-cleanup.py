#!/usr/bin/env python3
"""Prepare a fresh HCI UART source copy with final QCA setup-failure cleanup.

Never changes the kernel source, loads/unloads a driver or writes a device.
The failed setup errno is preserved; the established shutdown path, not a flag
clear or fake PM success, closes a controller after the last failed retry.
"""
from pathlib import Path
import argparse,hashlib,json,shutil

def patch(s):
 marker='\n\t/* Setup bdaddr */\n'
 if s.count(marker)!=1 or 'if (ret && retries < MAX_INIT_RETRIES)' not in s:raise ValueError('Unknown QCA setup source')
 if 'QCA setup failed ret=' in s:raise ValueError('Cleanup already present')
 change='\n\n\t/* A final failed setup has no asynchronous downloader left to clear\n\t * IBS_DISABLED. Close the hardware using the existing QCA shutdown\n\t * path; otherwise a dead controller blocks every system suspend as\n\t * though firmware were still being downloaded. Keep the setup error.\n\t */\n\tif (ret) {\n\t\tbt_dev_err(hdev, "QCA setup failed ret=%d retries=%u; powering down failed controller", ret, retries);\n\t\tqca_power_shutdown(hu);\n\t}\n'
 return s.replace(marker,change+marker,1)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--kernel',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():raise ValueError('Fresh output required')
 src=a.kernel/'drivers/bluetooth';original=(src/'hci_qca.c').read_bytes();changed=patch(original.decode()).encode()
 a.output.mkdir(parents=True)
 for f in src.iterdir():
  if f.is_file() and (f.suffix in ('.c','.h') or f.name in ('Makefile','Kconfig')):shutil.copyfile(f,a.output/f.name)
 (a.output/'hci_qca.c').write_bytes(changed)
 (a.output/'candidate.json').write_text(json.dumps(dict(changed_symbol='qca_setup final failure cleanup',preserves_setup_errno=True,changes_success_path=False,original_sha256=hashlib.sha256(original).hexdigest(),modified_sha256=hashlib.sha256(changed).hexdigest(),hardware_tested=False),indent=2))
 print(a.output)
if __name__=='__main__':main()
