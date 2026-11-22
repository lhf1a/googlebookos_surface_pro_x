"""Unit and portability tests for probe/test2/spx-irq-balance-boot and full_image wiring."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import full_image as fi_mod


class SpxIrqBalanceTests(unittest.TestCase):
    def test_script_syntax_line_endings_and_portability(self):
        script = ROOT / 'probe' / 'test2' / 'spx-irq-balance-boot'
        raw = script.read_bytes()
        self.assertNotIn(b'\r\n', raw, 'spx-irq-balance-boot must use LF line endings')
        subprocess.run(['sh', '-n', str(script)], check=True)
        text = raw.decode('utf-8')
        self.assertNotRegex(text, re.compile(r'/home/[a-zA-Z0-9._-]+/'))
        self.assertNotRegex(text, re.compile(r'\b10\.\d+\.\d+\.\d+\b'))
        # Do not hardcode numeric IRQ numbers in matching logic; must match via chip_name/actions.
        self.assertIn('chip_name', text)
        self.assertIn('actions', text)
        self.assertNotRegex(text, re.compile(r'^\s*[0-9]{2,3}\)', re.M))

    def test_balances_gicv3_peripheral_irqs_by_action_and_skips_others(self):
        script = ROOT / 'probe' / 'test2' / 'spx-irq-balance-boot'
        cases = [
            ('101', 'GICv3', '884000.spi', '0-7'),
            ('102', 'GICv3', 'WLAN_CE_1', '0-7'),
            ('103', 'GICv3', 'xhci-hcd:usb1', '0-7'),
            ('104', 'GICv3', 'dwc3', '0-7'),
            ('105', 'GICv3', 'glink-smem', '0-7'),
            ('106', 'GICv3', '888000.i2c', '0-7'),
            ('107', 'GICv3', 'gpu-irq', '0-7'),
            ('108', 'GICv3', 'qcom_geni_serial_uart0', '0-7'),
            ('109', 'GICv3', 'bam_dma', '0-7'),
            ('110', 'GICv3', 'slim-ngd', '0-7'),
            ('111', 'GICv3', 'acb3000.isp_0', '0-7'),
            ('112', 'GICv3', 'ac4a000.cci', '0-7'),
            ('113', 'GICv3', 'venus', '0-7'),
            ('115', 'GICv3', 'sq2-venus-owner', '0'),
            ('114', 'GICv3', 'apps_rsc', '0'),
            ('127', 'PDC', 'c440000.spmi', '0-7'),
            ('200', 'msmgpio', 'spi0.0', '0'),
            ('300', 'DW-PCI-MSIX', 'nvme0q1', '1'),
        ]
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            mock_bin = root / 'mockbin'
            mock_bin.mkdir()
            nsenter_mock = mock_bin / 'nsenter'
            nsenter_mock.write_text(
                """#!/bin/sh
while [ $# -gt 0 ]; do
    case "$1" in
        --) shift; break;;
        *) shift;;
    esac
done
if [ "$1" = /system/bin/getprop ] && [ "$2" = sys.boot_completed ]; then
    echo 1
    exit 0
fi
exit 1
"""
            )
            nsenter_mock.chmod(0o755)
            for tool in ('grep', 'mkdir', 'rmdir', 'seq', 'sleep', 'timeout', 'tr'):
                for cand in (Path('/bin') / tool, Path('/usr/bin') / tool):
                    if cand.exists():
                        (mock_bin / tool).symlink_to(cand)
                        break

            sys_cpu = root / 'sys_cpu'
            for cpu in range(8):
                (sys_cpu / f'cpu{cpu}').mkdir(parents=True)

            sys_irq = root / 'sys_irq'
            proc_irq = root / 'proc_irq'
            for irq, chip, action, initial in cases:
                idir = sys_irq / irq
                idir.mkdir(parents=True)
                (idir / 'chip_name').write_text(chip + '\n')
                (idir / 'actions').write_text(action + '\n')
                pdir = proc_irq / irq
                pdir.mkdir(parents=True)
                (pdir / 'smp_affinity_list').write_text(initial + '\n')

            cmdline = root / 'cmdline'
            cmdline.write_text('root=/dev/ram0 sq2.vulkan=qcom\n')

            env = dict(
                os.environ,
                SQ2_TEST_PATH=f'{mock_bin}:/bin:/usr/bin',
                SQ2_CMDLINE=str(cmdline),
                SQ2_IRQ_BALANCE_LOCK=str(root / 'lock'),
                SQ2_SYS_CPU_DIR=str(sys_cpu),
                SQ2_SYS_IRQ_DIR=str(sys_irq),
                SQ2_PROC_IRQ_DIR=str(proc_irq),
                SQ2_BOOT_POLL_RETRIES='2',
                SQ2_BOOT_POLL_SLEEP='0',
                SQ2_IRQ_BALANCE_SETTLE_SLEEP='0',
            )
            res = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            lines = res.stdout.strip().splitlines()
            expected_logs = [
                'IRQ_BALANCED:101:884000.spi:cpu1',
                'IRQ_BALANCED:102:WLAN_CE_1:cpu2',
                'IRQ_BALANCED:103:xhci-hcd:usb1:cpu2',
                'IRQ_BALANCED:104:dwc3:cpu2',
                'IRQ_BALANCED:105:glink-smem:cpu3',
                'IRQ_BALANCED:106:888000.i2c:cpu3',
                'IRQ_BALANCED:107:gpu-irq:cpu3',
                'IRQ_BALANCED:108:qcom_geni_serial_uart0:cpu3',
                'IRQ_BALANCED:109:bam_dma:cpu3',
                'IRQ_BALANCED:110:slim-ngd:cpu3',
                'IRQ_BALANCED:111:acb3000.isp_0:cpu3',
                'IRQ_BALANCED:112:ac4a000.cci:cpu3',
                'IRQ_BALANCED:113:venus:cpu3',
                'IRQ_BALANCED:115:sq2-venus-owner:cpu3',
            ]
            self.assertEqual(lines, expected_logs)

            expected_affinities = {
                '101': '1',
                '102': '2',
                '103': '2',
                '104': '2',
                '105': '3',
                '106': '3',
                '107': '3',
                '108': '3',
                '109': '3',
                '110': '3',
                '111': '3',
                '112': '3',
                '113': '3',
                '115': '3',
                '114': '0',
                '127': '0-7',
                '200': '0',
                '300': '1',
            }
            for irq, exp in expected_affinities.items():
                got = (proc_irq / irq / 'smp_affinity_list').read_text().strip()
                self.assertEqual(got, exp, f'irq {irq} affinity mismatch')

            # Second pass is an idempotent no-op when affinities already match targets.
            res2 = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertEqual(res2.stdout.strip(), '')

            # Rescue mode skips cleanly with zero output.
            cmdline.write_text('root=/dev/ram0 sq2.rescue=1\n')
            (proc_irq / '101' / 'smp_affinity_list').write_text('0\n')
            res_rescue = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertEqual(res_rescue.stdout.strip(), '')
            self.assertEqual((proc_irq / '101' / 'smp_affinity_list').read_text().strip(), '0')

    def test_full_image_install_irq_balance_wires_helper_before_anchor(self):
        anchor = fi_mod.ANCHOR
        bodies = {'init': b'#!/bin/busybox sh\n' + anchor}
        records = {'init': fi_mod.new_record(0o100755)}
        fi_mod.install_irq_balance(records, bodies)
        self.assertIn('bin/spx-irq-balance-boot', bodies)
        self.assertEqual(records['bin/spx-irq-balance-boot'][1][1], 0o100755)
        self.assertEqual(bodies['init'].count(fi_mod.IRQ_BALANCE_HOOK), 1)
        self.assertLess(bodies['init'].index(fi_mod.IRQ_BALANCE_HOOK), bodies['init'].index(anchor))


if __name__ == '__main__':
    unittest.main()
