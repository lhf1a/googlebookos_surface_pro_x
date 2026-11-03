#!/usr/bin/env python3
"""Generate a private framework APN resource for the verified SIM operator."""
import argparse
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET


def apn_resource(numeric, apn):
    if not re.fullmatch(r'[0-9]{5,6}', numeric):
        raise ValueError('Actual SIM MCC+MNC (5 or 6 digits) required')
    if not re.fullmatch(r'[A-Za-z0-9.-]{1,100}', apn):
        raise ValueError('Invalid APN')
    root = ET.Element('apns', version='8')
    ET.SubElement(root, 'apn', carrier='LTE Internet', mcc=numeric[:3], mnc=numeric[3:],
                  apn=apn, type='default,supl', protocol='IP', roaming_protocol='IP',
                  carrier_enabled='true', user_visible='true', user_editable='true')
    return ET.tostring(root, encoding='utf-8', xml_declaration=True) + b'\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operator_numeric')
    parser.add_argument('apn_file', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = root / 'out/lte-radio-20261103/overlay-res'
    (out / 'values').mkdir(parents=True, exist_ok=True)
    (out / 'xml').mkdir(exist_ok=True)
    shutil.copyfile(root / 'probe/lte/overlay/res/values/config.xml', out / 'values/config.xml')
    (out / 'xml/apns.xml').write_bytes(apn_resource(args.operator_numeric, args.apn_file.read_text().strip()))
    print('Generated private APN overlay resource; framework preserves user edits/deletions')
