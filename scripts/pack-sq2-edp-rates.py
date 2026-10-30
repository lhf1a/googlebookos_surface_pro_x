#!/usr/bin/env python3
"""Pack isolated eDP candidate. Never deploy; preserve every other final entry."""
import gzip, hashlib, importlib.util, json, zlib, subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root/'out/display-refresh'
base_path = root/'out/quest2-gpu/boot-runtime-v8.cpio.gz'
base_hash = '5ba59bd91c14208442a15792dedff236b0f1789c4d8fe10d00ee50f5e0a4953c'
module = out/'msm-edp-rates.ko'
revision = int(sys.argv[1]) if len(sys.argv) > 1 else 4
assert 4 <= revision <= 20
target = out/f'native-edp-rates-v{revision}.cpio.gz'
sha = lambda b: hashlib.sha256(b).hexdigest()
base = base_path.read_bytes()
assert sha(base) == base_hash and not target.exists()
spec = importlib.util.spec_from_file_location('cpio', root/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec); spec.loader.exec_module(cpio)
before = cpio.entries(gzip.decompress(base))
name = 'lib/modules/6.3.8-sq2-android1/kernel/drivers/gpu/drm/msm/msm.ko'
assert sha(before[name][1]) == 'f69c91e754e2a0d66cd195549d970973ed81b4c6f1bbfcbc3723f80a26414633'
data = module.read_bytes()
assert b'vermagic=6.3.8-sq2-android1 SMP mod_unload aarch64\0' in data
assert b'SQ2 eDP rate table:' in data
assert subprocess.check_output(['modinfo','-F','depends',str(module)]).strip() == b'gpu-sched,llcc-qcom,mdt_loader'
exports = {line.split()[2].removeprefix('__ksymtab_')
           for line in (out/'exports.txt').read_text().splitlines()}
undefined = {line.split()[-1] for line in (out/'msm-undefined.txt').read_text().splitlines()}
assert undefined <= exports, sorted(undefined - exports)
rest = base; credential = None
while rest:
 dec = zlib.decompressobj(31); files = cpio.entries(dec.decompress(rest)); assert dec.eof
 if 'etc/shadow' in files and 'bin/dropbear' in files:
  credential = rest[:len(rest)-len(dec.unused_data)]
 rest = dec.unused_data
assert credential
init_mode, init = before['init']
anchor = b'mount -t tmpfs tmpfs /tmp\n'
assert init.count(anchor) == 1
hook = b'''# One-shot eDP candidate only: return to the normal GRUB default unless confirmed.
if grep -qw sq2.edp_test=1 /proc/cmdline; then
    ( sleep 240
      if [ ! -f /tmp/sq2-edp-confirmed ]; then
          echo SQ2_EDP_TEST_UNCONFIRMED_REBOOT > /dev/kmsg
          sync
          /bin/busybox reboot -f
      fi
    ) </dev/null >/tmp/sq2-edp-watchdog.txt 2>&1 &
    ( mkdir -p /tmp/sq2-edp-diag
      mount -t debugfs debugfs /tmp/sq2-edp-diag
      for n in $(seq 1 22); do
          sleep 10
          dest=/media/esp/logs/sq2-edp-$(cat /proc/sys/kernel/random/boot_id)
          if grep -q '^/dev/nvme0n1p6 /media/esp vfat ' /proc/mounts; then
              mkdir -p "$dest"
              dmesg | tail -c 262144 > "$dest/kernel.txt.new"
              mv "$dest/kernel.txt.new" "$dest/kernel.txt"
              dmesg | grep -iE 'SQ2.EDP|link train|Unknown symbol|video timedout' | tail -c 65536 > "$dest/events-$n.txt"
              if [ "$n" -le 3 ]; then
                  timeout 5 cat /tmp/sq2-edp-diag/dri/0/msm_dp-eDP-1/dp_debug > "$dest/link-$n.txt" 2>&1
              fi
              timeout 5 cat /tmp/sq2-edp-diag/clk/clk_summary | grep -iE 'edp|phy.*clk' > "$dest/clocks-$n.txt"
              cat /tmp/sq2-edp-diag/dri/0/encoder31/status > "$dest/encoder-$n.txt" 2>&1
              [ ! -f /logs/gpu-native-startup.txt ] || cp /logs/gpu-native-startup.txt "$dest/gpu-startup.txt"
              sync
          fi
          [ ! -f /tmp/sq2-edp-confirmed ] || exit 0
      done
      umount /tmp/sq2-edp-diag
    ) </dev/null >/tmp/sq2-edp-collector.txt 2>&1 &
fi
'''
candidate_init = init.replace(anchor, anchor + hook)
subprocess.run(['sh','-n'], input=candidate_init, check=True)
archive = (cpio.entry(name, data, before[name][0]) + cpio.entry('init', candidate_init, init_mode)
           + cpio.entry('TRAILER!!!', b'', 0))
archive += b'\0' * (-len(archive) % 512)
result = base + gzip.compress(archive, mtime=0) + credential
after = cpio.entries(gzip.decompress(result))
assert {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)} == {name, 'init'}
with target.open('xb') as f: f.write(result)
with (out/f'edp-rates-v{revision}-addon.cpio.gz').open('xb') as f: f.write(result[len(base):])
report = dict(base_sha256=sha(base), output_sha256=sha(result), module_sha256=sha(data),
              changed=[name, 'init'], password_addon_last=True, runtime_exports_verified=len(undefined),
              kernel_and_dtb_unchanged=True, hardware_validated=False)
target.with_suffix('.audit.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
