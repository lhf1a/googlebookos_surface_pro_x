#!/usr/bin/env python3
"""Rebuild the matching Hybrid mapper/allocator with camera BLOB and flexible YUV."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline', type=Path, help='matching Hybrid BUILD.json')
p.add_argument('source', type=Path, help='original probe/graphics source directory')
p.add_argument('output', type=Path, help='new build directory')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
baseline = json.loads(a.baseline.read_text())
source = a.source.resolve()
out = a.output.resolve()
out.mkdir()
staged = out/'source'; staged.mkdir()
inputs = {}
used = {'quest2-native-mapper5.cpp', 'quest2-allocator5-service.cpp',
        'quest2-gralloc-backend.h', 'quest2-gralloc-metadata.h', 'quest2-platform-cxx.h',
        'quest2-color-metadata.h', 'quest2-gralloc-utils-abi.h', 'quest2-rgb-layout.h',
        'quest2-yv12-layout.h'}
# BUILD.json supplies the known platform ABI/link commands. Record the actual
# source closure, including the newer platform-libc++ compatibility header.
for name in used:
    f = source/name
    inputs[str(f)] = hashlib.sha256(f.read_bytes()).hexdigest()
for name in used:
    shutil.copy2(source/name, staged/name)
targets = ('quest2-native-mapper5.cpp', 'quest2-allocator5-service.cpp')
for name in targets:
    shutil.copy2(source/name, staged/name)
for patch in ('0004-hybrid-camera-blob.patch', '0005-hybrid-camera-flex-yuv.patch', '0006-hybrid-camera-encoder-usage.patch'):
    data = (root/'patches/camera'/patch).read_bytes()
    subprocess.run(['patch', '--batch', '--forward', '-p1', '-d', str(staged)], input=data, check=True)
commands = []
for original in baseline['commands']:
    if not any(v.endswith('/'+name) for v in original for name in targets):
        continue
    cmd = [str(staged/Path(v).name) if '/probe/graphics/' in v else v for v in original]
    index = cmd.index('-o')+1
    cmd[index] = str(out/Path(cmd[index]).name)
    subprocess.run(cmd, check=True)
    commands.append(cmd)
assert len(commands) == 2
test = next(c[:] for c in commands if any(v.endswith('/quest2-allocator5-service.cpp') for v in c))
test = [str(root/'probe/camera/hybrid-buffer-test.cpp') if v.endswith('/quest2-allocator5-service.cpp') else v for v in test]
test += ['-I'+str(staged)]
test[test.index('-o')+1] = str(out/'hybrid-buffer-test')
subprocess.run(test, check=True)
hashes = {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in out.iterdir() if f.is_file()}
(out/'BUILD.json').write_text(json.dumps({'inputs': inputs, 'commands': commands,
    'test_command': test, 'sha256': hashes}, indent=2)+'\n')
print(json.dumps(hashes, indent=2))
