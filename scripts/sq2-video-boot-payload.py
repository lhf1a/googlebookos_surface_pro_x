"""Import only the pinned, tested video payload; never copy credentials or init."""
import gzip, hashlib
from pathlib import Path

PAYLOAD_SHA='12f1c0d506d5dc654461e07bba96869ad601bf493e2b56d1ffc3ad3c64a299fc'
FIRMWARE_SHA='4e93d34504da96320775476bbd509aa06f985b48fb781d4436c8eb812b929928'
HELPERS={'video-codec-manifest-stage':'video-runtime-stage',
         'video-runtime-framework-boot':'video-runtime-framework-boot',
         'video-decoder-lab-start':'video-decoder-lab-start'}

def verify_helpers(entries, root):
    for dest, source in HELPERS.items():
        expected=(root/'probe/test2'/source).read_bytes().replace(b'\r\n',b'\n')
        if entries.get('bin/'+dest)!=(0o100755,expected):
            raise ValueError('Runtime video helper overwritten or missing: '+dest)

def default_menu(text):
    lines=[]; normal=0; rescue=0
    for line in text.splitlines():
        if line.lstrip().startswith('linux '):
            tokens=line.split()
            if 'sq2.rescue=1' in tokens:
                rescue+=1
                if any(t.startswith(('sq2.video.c2=','sq2.video.autostart=')) for t in tokens):
                    raise ValueError('rescue must not enable hardware video')
            else:
                normal+=1
                if 'sq2.video=0' not in tokens:
                    tokens=[t for t in tokens if not t.startswith(('sq2.video.c2=','sq2.video.autostart='))]
                    tokens+=['sq2.video.c2=experimental','sq2.video.autostart=1']
                    line=' '+ ' '.join(tokens)
        lines.append(line)
    if not normal or not rescue:raise ValueError('normal and rescue entries required')
    return '\n'.join(lines)+'\n'

def integrate(root, cpio, put, init, path=None, expected_sha=PAYLOAD_SHA):
    path=path or root/'out/video-decode/vp8-limits/boot-vp8-limits.cpio.gz'
    blob=path.read_bytes()
    if hashlib.sha256(blob).hexdigest()!=expected_sha:
        raise ValueError('unverified video payload')
    entries=cpio.entries(gzip.decompress(blob))
    if entries['etc/sq2-c2-runtime-boot/media_codecs_c2.xml'][1] != (root/'configs/sq2-media-codecs-c2.xml').read_bytes().replace(b'\r\n',b'\n'):
        raise ValueError('Video payload XML differs from current source')
    names={n for n in entries if n.startswith(('etc/sq2-video-native/', 'etc/sq2-c2-runtime-boot/'))}
    fw='lib/firmware/qcom/venus-5.2/venus.mbn'
    if hashlib.sha256(entries[fw][1]).hexdigest()!=FIRMWARE_SHA:
        raise ValueError('unverified Venus firmware')
    names.add(fw)
    for name in sorted(names):
        mode,data=entries[name]
        put(name,data,mode)
    for dest,source in HELPERS.items():
        body=(root/'probe/test2'/source).read_bytes().replace(b'\r\n',b'\n')
        put('bin/'+dest,body,0o100755)
    early=b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n'
    late=b'lockscreen-framework-boot > /logs/lockscreen-framework-boot.txt 2>&1 &\n'
    if init.count(early)!=1 or init.count(late)!=1 or b'video-runtime-framework-boot >' in init:
        raise ValueError('video boot hook mismatch')
    init=init.replace(early,b'video-codec-manifest-stage > /logs/video-c2-stage.txt 2>&1 || fail video-c2-stage\n'+early)
    init=init.replace(late,late+b'    video-runtime-framework-boot > /logs/video-runtime-boot.txt 2>&1 &\n')
    return init
