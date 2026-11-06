#!/usr/bin/env python3
"""Copy SPI-HID sources and validate the buffer that actually received the body.

Preserves length checks, calibration, thresholds and PM logic. Does not deploy.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

OLD = '\tret = spi_hid_process_input_report(shid, &shid->input);'
NEW = '''\t/* The header callback directs command/feature/descriptor bodies to
\t * response. Do not validate their new header against stale touch data.
\t * spi_unmap_msg() restores rx_buf before the completion callback.
\t */
\tif (shid->input_transfer[1].rx_buf == shid->response.body)
\t\tret = spi_hid_process_input_report(shid, &shid->response);
\telse
\t\tret = spi_hid_process_input_report(shid, &shid->input);'''

def fix(source):
    assert 'spi_unmap_msg() restores rx_buf' not in source
    start = source.index('static void spi_hid_input_body_complete(void *_shid)\n{')
    end = source.index('static void spi_hid_input_header_complete(void *_shid)\n{', start)
    body = source[start:end]
    assert body.count(OLD) == 1
    return source[:start]+body.replace(OLD, NEW, 1)+source[end:]

def external_build_compat(output, kernel_source):
    """Keep private HID IDs local; use the 6.18 SPI chip-select accessor."""
    target = output / 'spi-hid-core.c'
    text = target.read_text()
    target.write_text(text.replace('#include "../hid-ids.h"', '#include "hid-ids.h"'))
    shutil.copyfile(kernel_source / 'drivers/hid/hid-ids.h', output / 'hid-ids.h')
    trace = output / 'spi-hid_trace.h'
    trace.write_text(trace.read_text().replace('shid->spi->chip_select;',
                                              'spi_get_chipselect(shid->spi, 0);'))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('kernel_source',type=Path)
    p.add_argument('output',type=Path)
    a=p.parse_args();source=a.kernel_source/'drivers/hid/spi-hid'
    assert not a.output.exists()
    a.output.mkdir(parents=True)
    hashes={}
    for f in source.iterdir():
        if f.suffix in ('.c','.h') or f.name in ('Makefile','Kconfig'):
            hashes[f.name]=hashlib.sha256(f.read_bytes()).hexdigest()
            shutil.copyfile(f,a.output/f.name)
    target=a.output/'spi-hid-core.c';target.write_text(fix(target.read_text()))
    external_build_compat(a.output, a.kernel_source)
    for name,digest in hashes.items():
        assert hashlib.sha256((source/name).read_bytes()).hexdigest()==digest
    report={'source':str(source.resolve()),'output':str(a.output.resolve()),'original_sha256':hashes,
            'changed_symbol':'spi_hid_input_body_complete','changed_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
            'build_required':True,'hardware_verified':False}
    (a.output/'source-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
