#!/usr/bin/env python3
"""Repair observed SQ2 edge-cell spikes before contact fitting."""
from pathlib import Path
import sys

source = Path(sys.argv[1]) / 'src/core/generic'
header = source / 'sq2-heatmap.hpp'
header.write_text('''// SQ2 045E:0C21 heatmap quirk, based on captured 46x68 reports.
// Four edge cells produced saturated spikes while surrounding cells were idle.
// Their cause (firmware, transport or sensor) is not yet established.
#pragma once
#include "device.hpp"
#include <common/types.hpp>
#include <algorithm>
#include <array>
#include <utility>

namespace iptsd::core {
inline void repair_sq2_heatmap(const DeviceInfo &info, Image<f64> &image)
{
    if (!info.is_touchscreen() || info.vendor != 0x045e || info.product != 0x0c21 ||
        image.rows() != 46 || image.cols() != 68)
        return;
    // Use the original neighborhood for every repair, without cascading changes.
    constexpr std::array<std::pair<int, int>, 4> cells {{{0, 0}, {0, 67}, {1, 0}, {2, 0}}};
    std::array<f64, cells.size()> repaired {};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto [row, col] = cells[i];
        const f64 value = image(row, col);
        repaired[i] = value;
        // Most frames need no repair. Avoid even gathering their neighbors.
        if (value < 239.0 / 255.0)
            continue;
        std::array<f64, 8> neighbors {};
        std::size_t count = 0;
        for (int y = std::max(0, row - 1); y <= row + 1; ++y)
            for (int x = std::max(0, col - 1); x <= std::min(67, col + 1); ++x)
                if (y != row || x != col)
                    neighbors[count++] = image(y, x);
        std::nth_element(neighbors.begin(),
                         neighbors.begin() + static_cast<std::ptrdiff_t>(count / 2),
                         neighbors.begin() + static_cast<std::ptrdiff_t>(count));
        const f64 median = neighbors[count / 2];
        // Only interpolate near-saturated positive spikes with weak neighbors.
        // Real contact support in neighboring electrodes remains in the heatmap.
        if (value >= 239.0 / 255.0 && value - median > 40.0 / 255.0)
            repaired[i] = median;
    }
    // Commit only after all reads, so adjacent repairs never feed each other.
    for (std::size_t i = 0; i < cells.size(); ++i)
        image(cells[i].first, cells[i].second) = repaired[i];
}
} // namespace iptsd::core
''')
app = source / 'application.hpp'
s = app.read_text()
if '#include "sq2-heatmap.hpp"' not in s:
    assert '#include "dft.hpp"' in s
    s = s.replace('#include "dft.hpp"', '#include "dft.hpp"\n#include "sq2-heatmap.hpp"', 1)
    anchor = '\t\t// Search for contacts\n\t\tm_finder.find(m_heatmap, m_contacts);'
    assert anchor in s
    s = s.replace(anchor, '\t\trepair_sq2_heatmap(m_info, m_heatmap);\n\n' + anchor, 1)
    app.write_text(s)
