#!/usr/bin/env python3
"""Wire the C2 hardware encoder include cleanly into the selected media_codecs XML."""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

INCLUDE = 'media_codecs_sq2_c2_enc.xml'
OMX_INCLUDE = 'media_codecs_quest2_omx.xml'


def configure_enc_codecs(data):
    """Attach media_codecs_sq2_c2_enc.xml without disturbing existing OMX decoders."""
    root = ET.fromstring(data)
    if root.tag != 'MediaCodecs':
        raise ValueError('Expected MediaCodecs root')
    includes = root.findall('./Include')
    if sum(node.get('href') == INCLUDE for node in includes) > 1:
        raise ValueError('Duplicate C2 encoder include')
    if not any(node.get('href') == INCLUDE for node in includes):
        ET.SubElement(root, 'Include', href=INCLUDE)
    # Preserve OMX decoder include if already present.
    _ = OMX_INCLUDE
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    updated = configure_enc_codecs(args.variant.read_bytes())
    args.output.write_bytes(updated)


if __name__ == '__main__':
    main()
