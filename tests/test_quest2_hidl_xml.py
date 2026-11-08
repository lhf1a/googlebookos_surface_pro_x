import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('hidl_xml', ROOT / 'scripts/prepare-quest2-hidl-xml.py')
xml = importlib.util.module_from_spec(spec)
spec.loader.exec_module(xml)

class HidlXml(unittest.TestCase):
    def test_variant_preserves_other_codecs_and_audio(self):
        source = b'<MediaCodecs><Include href="audio.xml"/><Decoders><MediaCodec name="c2.avc" type="video/avc"><Alias name="OMX.qcom.video.decoder.avc"/><Alias name="keep"/><Limit name="size" max="640x480"/></MediaCodec></Decoders></MediaCodecs>'
        root = ET.fromstring(xml.configure_codecs(source))
        self.assertEqual([n.get('href') for n in root.findall('Include')], ['audio.xml', xml.INCLUDE])
        codec = root.find('Decoders/MediaCodec')
        self.assertEqual(codec.attrib, {'name': 'c2.avc', 'type': 'video/avc'})
        self.assertEqual([n.get('name') for n in codec.findall('Alias')], ['keep'])
        self.assertEqual(codec.find('Limit').get('max'), '640x480')
        self.assertEqual(xml.configure_codecs(ET.tostring(root)), xml.configure_codecs(source))

    def test_conflicting_native_codec_is_rejected(self):
        for name in xml.OMX:
            with self.subTest(codec=name), self.assertRaises(ValueError):
                xml.configure_codecs(('<MediaCodecs><Decoders><MediaCodec name="'+name+'"/></Decoders></MediaCodecs>').encode())

    def test_all_ported_aliases_removed_without_touching_software_codecs(self):
        root = ET.Element('MediaCodecs')
        decoders = ET.SubElement(root, 'Decoders')
        for suffix in ('avc', 'hevc', 'vp8', 'vp9', 'mpeg2'):
            codec = ET.SubElement(decoders, 'MediaCodec', name='c2.android.'+suffix+'.decoder')
            ET.SubElement(codec, 'Alias', name='OMX.qcom.video.decoder.'+suffix)
            ET.SubElement(codec, 'Alias', name='unrelated.'+suffix)
        output = ET.fromstring(xml.configure_codecs(ET.tostring(root)))
        codecs = output.findall('Decoders/MediaCodec')
        self.assertEqual(len(codecs), 5)
        for codec in codecs:
            self.assertTrue(codec.get('name').startswith('c2.android.'))
            self.assertEqual(len(codec.findall('Alias')), 1)
            self.assertTrue(codec.find('Alias').get('name').startswith('unrelated.'))

    def test_framework_only_removes_two_hidl_level_caps(self):
        source = b'<manifest type="framework"><hal format="hidl" max-level="8"><name>android.hidl.manager</name><fqname>@1.2::IServiceManager/default</fqname></hal><hal format="hidl" max-level="8"><name>android.hidl.token</name></hal><hal format="aidl" max-level="8"><name>keep</name></hal></manifest>'
        root = ET.fromstring(xml.configure_framework(source))
        self.assertEqual([node.get('max-level') for node in root], [None, None, '8'])
        self.assertEqual(root[0].findtext('fqname'), '@1.2::IServiceManager/default')
        self.assertEqual(xml.configure_framework(ET.tostring(root)), xml.configure_framework(source))

    def test_invalid_platform_inputs_rejected(self):
        for source in (b'<manifest type="device"/>', b'<manifest type="framework"/>'):
            with self.assertRaises(ValueError):
                xml.configure_framework(source)
        with self.assertRaises(ValueError):
            xml.configure_codecs(b'<manifest/>')
        with self.assertRaises(ValueError):
            xml.configure_codecs(b'<MediaCodecs><Include href="media_codecs_quest2_omx.xml"/><Include href="media_codecs_quest2_omx.xml"/></MediaCodecs>')

if __name__ == '__main__':
    unittest.main()
