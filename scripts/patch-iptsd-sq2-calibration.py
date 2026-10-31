#!/usr/bin/env python3
"""Add optional monotonic, output-only SQ2 finger coordinate calibration."""
from pathlib import Path
import sys

root = Path(sys.argv[1]) / 'src'
repo = Path(__file__).resolve().parents[1]
header = (repo / 'probe/test2/sq2-touch-calibration.hpp').read_text()
changes = {}

def replace(path, old, new, count=1):
    source = changes.get(path, path.read_text())
    if new in source:
        if source.count(new) != count:
            raise ValueError(f'Partial calibration patch: {path}')
        return
    if source.count(old) != count:
        raise ValueError(f'Unexpected calibration patch anchor: {path}')
    changes[path] = source.replace(old, new)

replace(root/'core/generic/config.hpp',
        'f64 touchscreen_overshoot = 0.5;',
        'f64 touchscreen_overshoot = 0.5;\n\tstd::string touchscreen_calibration_x {};\n\tstd::string touchscreen_calibration_y {};\n'
        '\tstd::string touchscreen_calibration_x_top {};\n\tf64 touchscreen_calibration_top_start = 0.1;\n\tf64 touchscreen_calibration_top_end = 0.3;')
replace(root/'core/linux/config-loader.hpp',
        'this->get(ini, "Touchscreen", "Overshoot", m_config.touchscreen_overshoot);',
        'this->get(ini, "Touchscreen", "Overshoot", m_config.touchscreen_overshoot);\n'
        '\t\tthis->get(ini, "Touchscreen", "CalibrationX", m_config.touchscreen_calibration_x);\n'
        '\t\tthis->get(ini, "Touchscreen", "CalibrationY", m_config.touchscreen_calibration_y);\n'
        '\t\tthis->get(ini, "Touchscreen", "CalibrationXTop", m_config.touchscreen_calibration_x_top);\n'
        '\t\tthis->get(ini, "Touchscreen", "CalibrationTopStart", m_config.touchscreen_calibration_top_start);\n'
        '\t\tthis->get(ini, "Touchscreen", "CalibrationTopEnd", m_config.touchscreen_calibration_top_end);')
touch = root/'apps/daemon/touch.hpp'
replace(touch, '#include <algorithm>', '#include "sq2-touch-calibration.hpp"\n#include <algorithm>')
replace(touch, 'bool m_enabled = true;',
        'bool m_enabled = true;\n\tSQ2TouchCalibration m_calibration {};')
replace(touch, '\t\tif (info.is_touchscreen())\n',
        '\t\t// Calibration profiles are specific to the measured SQ2 sensor.\n'
        '\t\tif (info.is_touchscreen() && info.vendor == 0x045e && info.product == 0x0c21 &&\n'
        '\t\t    info.meta.has_value() && info.meta->rows == 46 && info.meta->columns == 68) {\n'
        '\t\t\tm_calibration.load(config.touchscreen_calibration_x, config.touchscreen_calibration_y,\n'
        '\t\t\t                   config.touchscreen_calibration_x_top, config.touchscreen_calibration_top_start,\n'
        '\t\t\t                   config.touchscreen_calibration_top_end);\n'
        '\t\t}\n\n\t\tif (info.is_touchscreen())\n')
replace(touch, '\t\tmean.y() = std::clamp(mean.y(), 0.0, 1.0);',
        '\t\tmean.y() = std::clamp(mean.y(), 0.0, 1.0);\n'
        '\t\tconst auto calibrated = m_calibration.map(mean.x(), mean.y());\n'
        '\t\tmean.x() = calibrated.first;\n'
        '\t\tmean.y() = calibrated.second;', 2)
for path, source in changes.items():
    path.write_text(source)
(root/'apps/daemon/sq2-touch-calibration.hpp').write_text(header)
