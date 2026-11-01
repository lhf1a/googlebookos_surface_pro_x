"""Pinned, device-tested KernelSU payload and rescue-menu policy."""
import hashlib
import re

IMAGE_SHA = '074cf3057a471b090a2dcdddced2b5db6612e22cc0391b499e59da933898d52b'
RESCUE_SHA = 'e7c7a3e5399a6be93a363afc5f880fea30f17cb8a59f176807d3620f967b7ca8'
FILES = {
    'ksud': 'bb231b0f77d6ee78a4362ff751b3cc54c934293a4b0acde177afa59c4a64da00',
    'manager-verified.apk': '96c2bbbf1b973461fe82dd1ed17f89deb86a6a5a9d7c4cf079bd32091131ef57',
}
sha = lambda b: hashlib.sha256(b).hexdigest()

def payload(root, enabled=True):
    rescue = (root/'out/screen-only/Image').read_bytes()
    if b'SQ2 screen-only policy: system suspend blocked' not in rescue:
        raise ValueError('Rescue kernel missing screen-only guard')
    if sha(rescue) != RESCUE_SHA:
        raise ValueError('Rescue must remain the pinned original non-KSU kernel')
    if not enabled:
        return rescue, rescue, {}, {'enabled': False}
    image = (root/'out/ksunext/Image-candidate').read_bytes()
    if sha(image) != IMAGE_SHA:
        raise ValueError('KSU kernel differs from hardware-tested candidate; validate before updating pin')
    files = {}
    for name, digest in FILES.items():
        body = (root/'out/ksunext'/name).read_bytes()
        if sha(body) != digest:
            raise ValueError('KSU input checksum mismatch: '+name)
        files['etc/sq2-ksu/'+name] = (0o100755 if name == 'ksud' else 0o100644, body)
    helper = (root/'probe/test2/ksu-framework-boot').read_bytes().replace(b'\r\n', b'\n')
    files['bin/ksu-framework-boot'] = (0o100755, helper)
    return image, rescue, files, dict(enabled=True, version=33129, image_sha256=sha(image),
        rescue_image_sha256=sha(rescue), payload_sha256={n:sha(b) for n,(_,b) in files.items()},
        modules_preinstalled=False)

def menu(text, enabled):
    if not enabled:
        return text
    found = 0
    def entry(m):
        nonlocal found
        block = m[0]
        if 'sq2.rescue=1' in block:
            block, n = re.subn(r'(?m)^(\s*linux /boot/sq2-android1/)Image ', r'\1Image-rescue ', block)
            if n != 1: raise ValueError('Unexpected rescue kernel path')
            found += 1
        elif re.search(r'^\s*linux ', block, re.M) and 'lsm=selinux' not in block:
            raise ValueError('KSU requires SELinux enabled at boot')
        return block
    text = re.sub(r'^menuentry [^\n]*\{[^{}]*\}', entry, text, flags=re.M)
    if not found: raise ValueError('Missing independent rescue entry')
    return text
