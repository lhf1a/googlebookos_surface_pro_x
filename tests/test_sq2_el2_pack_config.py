"""Reject a pre-network kernel before the packer produces an output candidate."""
from pathlib import Path
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'scripts/pack-sq2-el2-android.py').read_text()
code = source.split('# Reject old pre-network builds', 1)[1].split('\n', 1)[1]
code = code.split('# End required kernel configuration check.', 1)[0]
builtins = ('KSU KPROBES KSU_KPROBES_HOOK KVM VHOST_VSOCK '
            'NETFILTER_XT_TARGET_CT NF_CONNTRACK_FTP NF_CONNTRACK_PPTP '
            'NF_NAT_FTP NF_NAT_PPTP NF_CT_PROTO_GRE').split()
modules = ['NET_SCH_INGRESS', 'NET_CLS_BPF']
rows = [f'CONFIG_{n}=y' for n in builtins] + [f'CONFIG_{n}=m' for n in modules]
with tempfile.TemporaryDirectory() as tmp:
    build = Path(tmp)
    def check(values):
        (build / '.config').write_text('\n'.join(values) + '\n')
        exec(code, {'B': build})
    check(rows)
    for row in rows:
        try:
            check([value for value in rows if value != row])
        except AssertionError as exc:
            assert str(exc) == row.removeprefix('CONFIG_').split('=')[0]
        else:
            raise AssertionError('accepted missing dependency: ' + row)
print('PASS: full configuration accepted; every required dependency enforced')
