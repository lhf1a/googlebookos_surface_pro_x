#!/usr/bin/env python3
"""Keep every IPTS touchscreen BTN_TOUCH consistent with emitted MT slots."""
from pathlib import Path
import sys

source = Path(sys.argv[1]) / 'src/apps/daemon/touch.hpp'
s = source.read_text()
old_gates=['m_info.is_touchscreen() && m_info.vendor == 0x045e && m_info.product == 0x0c21',
           'm_info.is_touchscreen() && m_info.vendor == 0x045e && (m_info.product == 0x0c21 || m_info.product == 0x0996)']
if 'void process_sq2_touchscreen(' in s or 'void process_touchscreen(' in s:
    for gate in old_gates:
        if gate in s:s=s.replace(gate,'m_info.is_touchscreen()',1);break
    if 'if (m_info.is_touchscreen()) {\n\t\t\tthis->process_' not in s:
        raise ValueError('Unexpected touchscreen state gate')
    s=s.replace('process_sq2_touchscreen','process_touchscreen').replace('m_sq2_reported','m_reported_touchscreen').replace('// Touchscreen contacts','// Touchscreen contacts')
    source.write_text(s)
    raise SystemExit(0)
assert '#include <memory>' in s
s = s.replace('#include <memory>', '#include <memory>\n#include <map>')
s = s.replace('\t// Whether the device is enabled.', '''\t// Touchscreen contacts actually emitted to uinput, retaining the last stable sample.
\tstd::map<usize, contacts::Contact<f64>> m_reported_touchscreen {};

\t// Whether the device is enabled.''', 1)
anchor = '\tvoid process(const std::vector<contacts::Contact<f64>> &contacts)\n\t{'
assert anchor in s
s = s.replace(anchor, anchor + '''
\t\tif (m_info.is_touchscreen()) {
\t\t\tthis->process_touchscreen(contacts);
\t\t\treturn;
\t\t}
''', 1)
new = '''
\t// Android uses BTN_TOUCH to distinguish touching from hovering, including MT.
\t// An unstable sample must not lift BTN_TOUCH while its MT slot stays active.
\t// Never create a new contact from an unstable sample. Never retain contacts
\t// that disappeared or became invalid, and do not add a temporal release delay.
\tvoid process_touchscreen(const std::vector<contacts::Contact<f64>> &contacts)
\t{
\t\tfor (const usize index : m_lift) {
\t\t\tif (m_reported_touchscreen.erase(index))
\t\t\t\tthis->lift_multitouch(index);
\t\t}
\t\tconst f64 ox = m_overshoot / m_config.width;
\t\tconst f64 oy = m_overshoot / m_config.height;
\t\tfor (const auto &contact : contacts) {
\t\t\tif (!contact.index.has_value())
\t\t\t\tcontinue;
\t\t\tconst usize index = contact.index.value();
\t\t\tconst bool lift = !contact.valid.value_or(true) ||
\t\t\t    contact.mean.x() < -ox || contact.mean.x() > 1 + ox ||
\t\t\t    contact.mean.y() < -oy || contact.mean.y() > 1 + oy;
\t\t\tif (lift) {
\t\t\t\tif (m_reported_touchscreen.erase(index))
\t\t\t\t\tthis->lift_multitouch(index);
\t\t\t\tcontinue;
\t\t\t}
\t\t\tif (!contact.stable.value_or(true))
\t\t\t\tcontinue;
\t\t\tm_reported_touchscreen[index] = contact;
\t\t\tthis->emit_multitouch(contact);
\t\t}
\t\tif (m_reported_touchscreen.empty()) {
\t\t\tthis->lift_singletouch();
\t\t\treturn;
\t\t}
\t\tif (m_reported_touchscreen.find(m_single_index) == m_reported_touchscreen.end())
\t\t\tm_single_index = m_reported_touchscreen.begin()->first;
\t\tthis->emit_singletouch(m_reported_touchscreen.at(m_single_index));
\t}

'''
anchor = '\tvoid lift_multitouch(const usize index) const'
assert anchor in s
s = s.replace(anchor, new + anchor, 1)
assert '\tvoid lift_all() const\n\t{' in s
s = s.replace('\tvoid lift_all() const\n\t{', '''\tvoid lift_all()
\t{
\t\tfor (const auto &entry : m_reported_touchscreen)
\t\t\tthis->lift_multitouch(entry.first);
\t\tm_reported_touchscreen.clear();''', 1)
source.write_text(s)
