#!/usr/bin/env python3
"""Wire the OMX allocator quirks into the actual platform XML variant.

Input is the selected media_codecs XML (ro.media.xml_variant.codecs), not an
unused generic file. Other codec entries, audio includes and limits are retained.
"""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

OMX = {'OMX.qcom.video.decoder.'+codec for codec in ('avc','hevc','vp8','vp9','mpeg2')}
INCLUDE = 'media_codecs_quest2_omx.xml'

def configure_codecs(data):
    root = ET.fromstring(data)
    if root.tag != 'MediaCodecs':
        raise ValueError('Expected MediaCodecs root')
    for codec in root.findall('.//MediaCodec'):
        if codec.get('name') in OMX:
            raise ValueError('Existing native OMX decoder entry requires explicit reconciliation')
        for alias in list(codec.findall('Alias')):
            if alias.get('name') in OMX:
                codec.remove(alias)
    includes = root.findall('./Include')
    if sum(node.get('href') == INCLUDE for node in includes) > 1:
        raise ValueError('Duplicate OMX includes')
    if not any(node.get('href') == INCLUDE for node in includes):
        ET.SubElement(root, 'Include', href=INCLUDE)
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)

def configure_framework(data):
    root = ET.fromstring(data)
    if root.tag != 'manifest' or root.get('type') != 'framework':
        raise ValueError('Expected framework manifest')
    for name in ('android.hidl.manager', 'android.hidl.token'):
        nodes = [node for node in root.findall('hal') if node.findtext('name') == name]
        if len(nodes) != 1 or nodes[0].get('format') != 'hidl':
            raise ValueError('Expected one HIDL declaration for ' + name)
        nodes[0].attrib.pop('max-level', None)
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', required=True, type=Path)
    parser.add_argument('--framework', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    variant = configure_codecs(args.variant.read_bytes())
    framework = configure_framework(args.framework.read_bytes())
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / args.variant.name).write_bytes(variant)
    (args.output / 'framework-manifest.xml').write_bytes(framework)
    config = Path(__file__).resolve().parents[1] / 'configs/quest2-media-codecs-omx.xml'
    (args.output / INCLUDE).write_bytes(config.read_bytes())

if __name__ == '__main__':
    main()
