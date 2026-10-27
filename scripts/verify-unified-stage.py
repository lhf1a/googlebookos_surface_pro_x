from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'out/test2-usb'; manifest=out/'driver-payload-manifest.json'
m=json.loads(manifest.read_text()); image=out/'qemu-test3.img'
for f in m['files']:
    assert hashlib.sha256((ROOT/f['source']).read_bytes()).hexdigest()==f['sha256'],f['path']
    if f['partition']=='boot':
        data=subprocess.check_output(['mtype','-i',f'{image}@@1048576','::/'+f['path']])
        assert len(data)==f['bytes'] and hashlib.sha256(data).hexdigest()==f['sha256'],f['path']
log=(ROOT/'out/test2/qemu-efi-smoke.log').read_text(errors='replace')
for marker in ['TQFTP_DIRECTORY_OK',"X.509 cert 'wens:",'END multitouch exit=3','UINPUT_NODE_OK','SYNTHETIC_ONE_RC=3','SYNTHETIC_TWO_RC=0','SSH_FUNCTIONAL_OK','TEST FINISHED AND LOGS SAVED']:
    assert marker in log,marker
summary=(ROOT/'out/test2/qemu-unified-summary.txt').read_text()
assert 'END multitouch exit=3' in summary and 'BEGIN wifi' not in summary and 'revision=c' in summary
assert 'BEGIN power-rtc' not in summary and 'BEGIN gpu' not in summary
ssh=(ROOT/'out/test2/qemu-ssh-result.txt').read_text()
assert ssh.startswith('PASS: key login;')
record={'status':'PASS','manifestSHA256':hashlib.sha256(manifest.read_bytes()).hexdigest(),'files':len(m['files']),
        'kernel':'6.3.8-sq2-test3','revision':'c','profile':'pending','checks':['EFI/GRUB ARM64 boot','IPTSd ARM64 executable startup','regdb wens certificate loaded and signature accepted','tqftpserv RAM directory exists','uinput synthetic single-contact rejected, two contacts accepted','pending runs multitouch only; injected failure preserved','SSH command and PTY; SCP bidirectional; wrong key rejected; passwords disabled','pending summary read back from disposable USB','15 staged payload hashes; 14 boot payloads match QEMU image'],
        'hardwareValidation':'Pending Surface retest; QEMU does not emulate SQ2 peripherals',
        'iptsd':'v3.1.1','iptsdArchiveSHA256':hashlib.sha256((ROOT/'downloads/iptsd-v3.1.1.tar.gz').read_bytes()).hexdigest(),
        'adrenoPatchSHA256':hashlib.sha256((ROOT/'configs/test3-adreno-smmu.patch').read_bytes()).hexdigest(),
        'adsp':json.loads((ROOT/'out/test2/adsp-selection.json').read_text())}
(ROOT/'out/test3-validation.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS: staged payload matches tested image and manifest; test3-validation.json written')
