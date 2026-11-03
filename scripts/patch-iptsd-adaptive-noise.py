#!/usr/bin/env python3
"""Add optional per-device idle impulse learning; keep accepted SQ2 fallback."""
from pathlib import Path
import sys
r=Path(__file__).resolve().parents[1];s=Path(sys.argv[1])/'src/core/generic'
for name in ('adaptive-noise.hpp','noise-runtime.hpp'):
    (s/name).write_bytes((r/'probe/touch-calibration'/name).read_bytes())
p=s/'application.hpp';t=p.read_text()
if 'NoiseRuntime m_surface_noise' not in t:
    t=t.replace('#include "dft.hpp"','#include "dft.hpp"\n#include "noise-runtime.hpp"',1)
    t=t.replace('Image<f64> m_heatmap {};','Image<f64> m_heatmap {};\n\tsurface_touch::NoiseRuntime m_surface_noise {};',1)
    anchor='\t\tm_parser.on_button = [&](const auto &data) { this->process_button(data); };'
    assert t.count(anchor)==1
    t=t.replace(anchor,anchor+'\n\t\tm_surface_noise.configure(m_info);',1)
    anchor='\t\t// Search for contacts\n\t\tm_finder.find(m_heatmap, m_contacts);'
    assert t.count(anchor)==1
    t=t.replace(anchor,'\t\tm_surface_noise.process(m_info, m_heatmap);\n\n'+anchor)
    p.write_text(t)
