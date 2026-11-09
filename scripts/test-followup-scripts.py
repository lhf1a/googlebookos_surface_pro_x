"""Isolated shell integration tests; no real devices, Wi-Fi or root paths."""
from pathlib import Path
import os, signal, subprocess, tempfile, threading, time
ROOT=Path(__file__).resolve().parents[1]
def setup(base,name):
    for d in ['sys','dev','run','logs','etc','bin']: (base/d).mkdir(exist_ok=True)
    text=(ROOT/'probe/test2'/name).read_text()
    for prefix in ['/sys/','/dev/','/run/','/logs/','/etc/']:
        # Keep /dev/null real; all device paths and state paths are isolated.
        text=text.replace(prefix,str(base)+prefix)
    text=text.replace(str(base)+'/dev/null','/dev/null')
    path=base/name;path.write_text(text)
    return path
def stub(base,name,text):
    p=base/'bin'/name;p.write_text('#!/bin/sh\n'+text+'\n');p.chmod(0o755)
def run(base,path,args=(),after=None):
    env=dict(os.environ,PATH=str(base/'bin')+':/usr/bin:/bin',TEST_ROOT=str(base))
    p=subprocess.Popen(['/bin/sh',str(path),*args],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
    if after: threading.Thread(target=after,daemon=True).start()
    try:
        rc=p.wait(timeout=25)
    finally:
        try: os.killpg(p.pid,signal.SIGTERM)
        except ProcessLookupError: pass
    p.stdout.close()
    return rc
for ready_later in [True,False]:
    with tempfile.TemporaryDirectory(prefix='sq2-touch-test-') as temp:
        b=Path(temp);p=setup(b,'touch-services')
        device=b/'sys/class/hidraw/hidraw4/device';device.mkdir(parents=True)
        (device/'uevent').write_text('HID_ID=001C:0000045E:00000C21\n')
        (device/'report_descriptor').write_bytes(b'fixture')
        (b/'dev/hidraw4').touch()
        # The real character-device requirement is exercised by the QEMU test.
        (b/'dev/uinput').symlink_to('/dev/null')
        ready=b/'sys/bus/spi/devices/spi0.0/ready';ready.parent.mkdir(parents=True);ready.write_text('not ready\n')
        stub(b,'modprobe','exit 0')
        stub(b,'iptsd-check-device','test -e /proc/$$/fd/3 || exit 91\n[ "$(cat "$TEST_ROOT/sys/bus/spi/devices/spi0.0/ready")" = ready ] || exit 92\necho CHECK_AFTER_READY > "$TEST_ROOT/logs/check.txt"')
        stub(b,'iptsd','exec /usr/bin/sleep 60')
        def wake():
            time.sleep(1.5);ready.write_text('ready\n')
        rc=run(b,p,after=wake if ready_later else None)
        log=(b/'logs/touch-service.txt').read_text()
        if ready_later:
            assert rc==0 and 'IPTSD_RUNNING' in log and (b/'logs/check.txt').exists(),log
        else:
            assert rc==3 and 'SPI_READY_TIMEOUT' in log and not (b/'logs/check.txt').exists(),log
        print('PASS touch delayed-ready' if ready_later else 'PASS touch ready-timeout avoids feature ioctl')
