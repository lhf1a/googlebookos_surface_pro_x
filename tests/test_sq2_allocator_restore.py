"""Exercise the actual rollback script in an isolated fake vendor tree."""
from pathlib import Path
import hashlib, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'probe/video/restore-sq2-allocator.sh').read_text()
original=b'original executable fixture';launcher=b'launcher fixture'
for scenario in ('restore','restore-scoped','already','unknown','bad-backup'):
 with tempfile.TemporaryDirectory() as tmp:
  d=Path(tmp);target=d/'allocator';backup=d/'backup';guard=d/'guard';events=d/'events'
  target.write_bytes(original if scenario=='already' else b'unknown' if scenario=='unknown' else b'scoped fixture' if scenario=='restore-scoped' else launcher)
  backup.write_bytes(b'bad' if scenario=='bad-backup' else original);guard.write_text('Y')
  script=source.replace('/vendor/bin/hw/android.hardware.graphics.allocator-service.minigbm',str(target)).replace('/vendor/bin/hw/sq2-allocator-original',str(backup)).replace('/sys/module/suspend/parameters/screen_only',str(guard))
  script=script.replace('7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da',hashlib.sha256(original).hexdigest()).replace('249da7cae6e999059e0e03a14728b7249681a9c2ecbf57751e8ded4ed276e02e',hashlib.sha256(launcher).hexdigest())
  script=script.replace('c859a01459841b8e7a8caff77981dd5d4bd30f13311c0f50e12bc07e6c7416b1',hashlib.sha256(b'scoped fixture').hexdigest())
  mock=f'id() {{ echo 0; }}; setprop() {{ echo "$*" >> {events}; }}; getprop() {{ echo running; }}; sleep() {{ :; }}; sync() {{ :; }};\n'
  p=subprocess.run(['bash','-c',mock+script],capture_output=True,text=True)
  if scenario in ('restore','restore-scoped'):
   assert p.returncode==0 and target.read_bytes()==original and events.read_text()=='ctl.restart vendor.graphics.allocator\n',p
  elif scenario=='already':assert p.returncode==0 and not events.exists() and target.read_bytes()==original,p
  else:assert p.returncode in (3,4) and not events.exists() and target.read_bytes()!=(original),p
  assert not Path(str(target)+'.restore-new').exists()
  print('RESTORE_GUARD_PASS',scenario)
