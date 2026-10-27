#!/usr/bin/env python3
"""Keep the SQ2 touchscreen's BTN_TOUCH consistent with emitted MT slots."""
from pathlib import Path
import sys

source = Path(sys.argv[1]) / 'src/apps/daemon/touch.hpp'
s = source.read_text()
if 'void process_sq2_touchscreen(' in s:
    raise SystemExit(0)
assert '#include <memory>' in s
s = s.replace('#include <memory>', '#include <memory>\n#include <map>')
s = s.replace('\t// Whether the device is enabled.', '''\t// SQ2 contacts actually emitted to uinput, retaining the last stable sample.
\tstd::map<usize, contacts::Contact<f64>> m_sq2_reported {};

\t// Whether the device is enabled.''', 1)
anchor = '\tvoid process(const std::vector<contacts::Contact<f64>> &contacts)\n\t{'
assert anchor in s
s = s.replace(anchor, anchor + '''
\t\tif (m_info.is_touchscreen() && m_info.vendor == 0x045e && m_info.product == 0x0c21) {
\t\t\tthis->process_sq2_touchscreen(contacts);
\t\t\treturn;
\t\t}
''', 1)
new = '''
\t// Android uses BTN_TOUCH to distinguish touching from hovering, including MT.
\t// An unstable sample must not lift BTN_TOUCH while its MT slot stays active.
\t// Never create a new contact from an unstable sample. Never retain contacts
\t// that disappeared or became invalid, and do not add a temporal release delay.
\tvoid process_sq2_touchscreen(const std::vector<contacts::Contact<f64>> &contacts)
\t{
\t\tfor (const usize index : m_lift) {
\t\t\tif (m_sq2_reported.erase(index))
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
\t\t\t\tif (m_sq2_reported.erase(index))
\t\t\t\t\tthis->lift_multitouch(index);
\t\t\t\tcontinue;
\t\t\t}
\t\t\tif (!contact.stable.value_or(true))
\t\t\t\tcontinue;
\t\t\tm_sq2_reported[index] = contact;
\t\t\tthis->emit_multitouch(contact);
\t\t}
\t\tif (m_sq2_reported.empty()) {
\t\t\tthis->lift_singletouch();
\t\t\treturn;
\t\t}
\t\tif (m_sq2_reported.find(m_single_index) == m_sq2_reported.end())
\t\t\tm_single_index = m_sq2_reported.begin()->first;
\t\tthis->emit_singletouch(m_sq2_reported.at(m_single_index));
\t}

'''
anchor = '\tvoid lift_multitouch(const usize index) const'
assert anchor in s
s = s.replace(anchor, new + anchor, 1)
assert '\tvoid lift_all() const\n\t{' in s
s = s.replace('\tvoid lift_all() const\n\t{', '''\tvoid lift_all()
\t{
\t\tfor (const auto &entry : m_sq2_reported)
\t\t\tthis->lift_multitouch(entry.first);
\t\tm_sq2_reported.clear();''', 1)
source.write_text(s)
