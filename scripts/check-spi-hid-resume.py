#!/usr/bin/env python3
"""Exercise actual SPI-HID open/header functions with mocked hardware, offline."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r'^static [^\n]*\b' + name + r'\([^;{]*?\n\{.*?\n}\n', source, re.M | re.S)
    if not match:
        raise ValueError(f'missing function: {name}')
    return match[0]


STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint8_t __u8;
typedef uint16_t u16;
struct device { int unused; };
struct spi_hid;
struct spi_device { struct device dev; int irq; struct spi_hid *data; };
struct hid_device { struct spi_device *driver_data; };
struct spi_hid_input_header { u8 version, report_type, fragment_id; u16 report_length; u8 sync_const; };
struct spi_hid_input_buf { u8 header[4], body[8192]; };
struct spi_hid {
    struct spi_device *spi;
    bool powered, pm_suspended, refresh_in_progress, irq_enabled;
    u8 power_state;
    int input_lock, error_work, input_stage, input_transfer_pending;
    int logic_error_count, logic_last_error, bus_error_count, bus_last_error;
    int pinctrl, pinctrl_sleep;
    struct { int status; } input_message;
    struct { int max_input_length; } desc;
    struct spi_hid_input_buf input, response;
};
#define dev_err(dev, ...) ((void)(dev))
#define dev_warn(dev, ...) ((void)(dev))
#define trace_spi_hid_input_header_complete(...) ((void)0)
#define print_hex_dump(...) ((void)0)
#define spin_lock_irqsave(lock, flags) ((void)(lock), (flags)=0)
#define spin_unlock_irqrestore(lock, flags) ((void)(lock), (void)(flags))
#define READ_ONCE(x) (x)
#define spi_get_drvdata(spi) ((spi)->data)
static int fail_step, step, irq_step, recovery, bodies, async_error, sleep_calls, down_calls;
static bool stable, early_irq;
static void *body_buffer;
static int body_length;
static int spi_hid_assert_reset(struct spi_hid *s) { (void)s; step++; return fail_step==1?-EIO:0; }
static int spi_hid_power_up(struct spi_hid *s) { step++; s->powered=fail_step!=2; return fail_step==2?-EIO:0; }
static int spi_hid_deassert_reset(struct spi_hid *s) { (void)s; step++; stable=fail_step!=3; return fail_step==3?-EIO:0; }
static int spi_hid_power_down(struct spi_hid *s) { down_calls++; s->powered=false; return 0; }
static int pinctrl_select_state(int p, int s) { (void)p; (void)s; sleep_calls++; return 0; }
static void enable_irq(int irq) { (void)irq; irq_step=step; early_irq=!stable; }
static void schedule_work(int *w) { (void)w; recovery++; }
static void spi_hid_input_body_complete(void *p) { (void)p; }
static int spi_hid_input_async(struct spi_hid *s, void *buf, u16 len, void (*cb)(void *)) {
    (void)s; (void)cb; bodies++; body_buffer=buf; body_length=len; return async_error;
}
'''

CHECKS = r'''
static struct spi_hid s;
static struct spi_device spi;
static struct hid_device hid;
static void init(void) {
    memset(&s, 0, sizeof(s)); memset(&spi, 0, sizeof(spi));
    spi.data=&s; s.spi=&spi; hid.driver_data=&spi;
    s.power_state=SPI_HID_POWER_MODE_OFF;
    fail_step=step=irq_step=recovery=bodies=async_error=sleep_calls=down_calls=0;
    stable=early_irq=false; body_buffer=NULL; body_length=0;
}
static void header(u8 first, u8 length, u8 sync) {
    init(); s.powered=true; s.power_state=SPI_HID_POWER_MODE_ACTIVE;
    s.desc.max_input_length=64; s.input_transfer_pending=1;
    s.input.header[0]=first; s.input.header[1]=length; s.input.header[3]=sync;
}
int main(void) {
    init(); assert(spi_hid_ll_open(&hid)==0);
    assert(s.powered && s.irq_enabled && stable);
    assert(early_irq==!FIXED); assert(irq_step==(FIXED?3:1));
    for (int f=1; f<=3; f++) {
        init(); fail_step=f; assert(spi_hid_ll_open(&hid)==-EIO);
        assert(!s.powered && s.power_state==SPI_HID_POWER_MODE_OFF);
        assert(s.irq_enabled==(!FIXED && f>1));
        assert(down_calls==(f==3)); assert(sleep_calls==(f>1));
    }
    init(); s.refresh_in_progress=true; assert(spi_hid_ll_open(&hid)==0 && step==0);
    init(); s.power_state=SPI_HID_POWER_MODE_ACTIVE; assert(spi_hid_ll_open(&hid)==0 && step==0);
    init(); s.irq_enabled=true; assert(spi_hid_ll_open(&hid)==0);
    assert(irq_step==0 && s.logic_error_count==1 && s.logic_last_error==-EEXIST);
    /* Captured zero header, unsupported version, and oversized body. */
    for (int fault=0; fault<3; fault++) {
        header(fault==0?0:0x12, fault==2?0xf0:0, fault==0?0:0x5a);
        if (fault==1) s.input.header[0]=0x11;
        if (fault==2) s.input.header[2]=1;
        spi_hid_input_header_complete(&s);
        assert(recovery==FIXED && bodies==0 && s.input_transfer_pending==0);
        assert(s.bus_error_count==1 && s.bus_last_error==(fault==2?-EMSGSIZE:-EINVAL));
    }
    for (int type=1; type<=8; type++) {
        header((type<<4)|2, 0x10, 0x5a); spi_hid_input_header_complete(&s);
        bool response=type==4 || type==5 || type==8;
        assert(recovery==0 && bodies==1 && body_length==4);
        assert(body_buffer==(response?s.response.body:s.input.body));
        assert(s.input_stage==SPI_HID_INPUT_STAGE_BODY && s.input_transfer_pending==1);
        if(response) assert(!memcmp(s.input.header,s.response.header,4));
    }
    header(0,0,0); s.powered=false; spi_hid_input_header_complete(&s);
    assert(!recovery && !bodies && !s.bus_error_count);
#if HAS_PM
    header(0,0,0); s.pm_suspended=true; spi_hid_input_header_complete(&s);
    assert(!recovery && !bodies && !s.bus_error_count);
#endif
    header(0,0,0); s.input_message.status=-EIO; spi_hid_input_header_complete(&s);
    assert(recovery==1 && bodies==0 && s.bus_last_error==-EIO);
    header(0x12,0x10,0x5a); async_error=-EIO; spi_hid_input_header_complete(&s);
    assert(bodies==1 && s.input_transfer_pending==0);
    printf("%s: open order/failures, malformed/valid headers, PM/off guards passed\n", FIXED?"modified":"baseline");
}
'''


def check(source, directory, fixed):
    c = (source / 'spi-hid-core.c').read_text()
    h = (source / 'spi-hid-core.h').read_text()
    defines = '\n'.join(line for line in h.splitlines() if line.startswith('#define SPI_HID_'))
    names = ['spi_hid_ll_open', 'spi_hid_populate_input_header',
             'spi_hid_bus_validate_header', 'spi_hid_input_header_complete']
    code = STUBS + '\n' + defines + '\n' + '\n'.join(function(c, name) for name in names) + CHECKS
    unit = directory / 'check.c'
    unit.write_text(code)
    binary = directory / 'check'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
                    '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                    f'-DFIXED={int(fixed)}', f'-DHAS_PM={int("SQ2_SPI_PM_V1" in c)}',
                    str(unit), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Unpatched matching spi-hid directory')
    parser.add_argument('--v1-source', type=Path, help='Optional existing V1 source to test upgrade')
    args = parser.parse_args()
    patch = Path(__file__).with_name('patch-sq2-spi-pm.py').resolve()
    with tempfile.TemporaryDirectory(prefix='spi-resume-check-') as td:
        root = Path(td)
        modified = root / 'modified'
        modified.mkdir()
        files = ['spi-hid-core.c', 'spi-hid-core.h']
        original = {f: (args.source/f).read_bytes() for f in files}
        assert b'SQ2_SPI_PM_V1' not in original[files[0]], 'supply unpatched source'
        check(args.source, root, False)
        for f in files:
            (modified/f).write_bytes(original[f])
        def apply():
            return subprocess.run(['python3', str(patch), str(modified)], capture_output=True, text=True)
        result = apply()
        assert result.returncode == 0, result.stderr
        expected = {f: (modified/f).read_bytes() for f in files}
        check(modified, root, True)
        assert apply().returncode == 0
        assert all((modified/f).read_bytes()==expected[f] for f in files), 'not idempotent'
        if args.v1_source:
            check(args.v1_source, root, False)
            for f in files:
                (modified/f).write_bytes((args.v1_source/f).read_bytes())
            result = apply()
            assert result.returncode == 0, result.stderr
            assert all((modified/f).read_bytes()==expected[f] for f in files), 'V1 upgrade differs'
        for f in files:
            (modified/f).write_bytes(original[f])
        changed = original[files[0]].replace(b'ret = spi_hid_deassert_reset(shid);', b'ret = changed_reset(shid);')
        (modified/files[0]).write_bytes(changed)
        assert apply().returncode != 0, 'unknown source must fail closed'
        assert (modified/files[0]).read_bytes()==changed
        assert (modified/files[1]).read_bytes()==original[files[1]], 'partial header mutation'
        print('fresh patch, idempotence, optional V1 upgrade, fail-closed/no partial write passed')


if __name__ == '__main__':
    main()
