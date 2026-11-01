#!/usr/bin/env python3
"""Exercise the modem-start decision with private fake sysfs and an argv stub."""
from pathlib import Path
import os
import stat
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sq2-el2-rmtfs-') as directory:
    temp = Path(directory)
    (temp/'bin').mkdir()
    (temp/'remoteproc/remoteproc0').mkdir(parents=True)
    node = temp/'remoteproc/remoteproc0'
    (temp/'bin/rmtfs').write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$ARGV_CAPTURE"\n')
    (temp/'bin/rmtfs').chmod(0o755)
    os.mknod(temp/'rmtfs_mem1', stat.S_IFCHR | 0o600, os.makedev(1, 3))
    text=(root/'probe/virtualization/el2-rmtfs-start').read_text()
    text=text.replace('/proc/cmdline',str(temp/'cmdline'))
    text=text.replace('/sys/class/remoteproc',str(temp/'remoteproc'))
    text=text.replace('/dev/qcom_rmtfs_mem1',str(temp/'rmtfs_mem1'))
    (temp/'helper').write_text(text)
    env=dict(os.environ,PATH=str(temp/'bin')+':'+os.environ['PATH'],ARGV_CAPTURE=str(temp/'argv'))
    def run(cmdline, state, firmware, expected):
        (temp/'cmdline').write_text(cmdline+'\n')
        (node/'name').write_text('4080000.remoteproc\n')
        (node/'state').write_text(state+'\n')
        (node/'firmware').write_text(firmware+'\n')
        (temp/'argv').unlink(missing_ok=True)
        result=subprocess.run(['sh',str(temp/'helper')],env=env,capture_output=True)
        if expected is None:
            assert result.returncode != 0 and not (temp/'argv').exists()
        else:
            assert result.returncode == 0, result.stderr
            assert (temp/'argv').read_text().splitlines()==expected
    firmware='qcom/msft/surface/pro-x-sq2/qcmpss8180.mbn'
    run('normal','offline',firmware,['-r','-o','/run/rmtfs','-s','-v'])
    run('sq2.el2=experimental','attached',firmware,['-r','-o','/run/rmtfs','-v'])
    run('sq2.el2=1','attached',firmware,['-r','-o','/run/rmtfs','-v'])
    run('sq2.el2=1','offline',firmware,None)
    run('sq2.el2=experimental','offline',firmware,None)
    run('sq2.el2=experimental','running',firmware,None)
    run('sq2.el2=experimental','attached','unknown',None)
    (temp/'rmtfs_mem1').unlink()
    run('sq2.el2=experimental','attached',firmware,None)
print('PASS: normal start preserved, attached modem not restarted, invalid handoffs rejected')
